# AmiPilot wire protocol — SPEC v1

The line protocol spoken between a host client and `AmiPilotServer` over a
byte-stream transport (serial.device in phase 0.3; TCP in 0.4). This is
the contract the host Python client pins against; like `manifest/SPEC.md`,
changes to it are versioned here, not implied by code.

Design constraints (decided 2026-08-05, see `docs/implementation-plan.md`
"Protocol and client"): **no JSON anywhere**. Requests are the exact same
command grammar the ARexx port parses (`server/src/arexx_cmd.c` — one
parser, two transports), and responses are length-prefixed text, so both
sides stay binary-safe with zero escaping and a 68000-friendly parser.

## Transport

- Phase 0.3: serial.device, 8N1, xon/xoff disabled (the length-prefixed
  framing makes the stream binary-safe; software flow control would eat
  XON/XOFF bytes out of payloads). Baud is a deployment choice
  (`AmiPilotServer BAUD=n`, default 19200); both ends must simply agree.
- Phase 0.4: TCP via bsdsocket.library (`AmiPilotServer TCP
  TCPPORT=n`), listen-mode only — the server binds and listens,
  the host connects in. One connection is treated as active at a
  time (a new one replaces the old); see `server/README.md` for the
  full option set and what's verified.
  **SECURITY: this transport is meant for a trusted LAN or a direct
  machine-to-machine link — never expose it on an open/internet-facing
  port.** `TCPALLOW`/`TCPPASSWORD` (`server/README.md`) narrow who can
  connect and require the `AUTH` verb below to succeed first, but
  neither makes this internet-safe: there's no TLS, the `AUTH` default
  is a fixed string checked into this public repo, and there's no
  rate-limiting on repeated guesses. A guest-initiated (dial-out)
  mode — the Amiga connecting out to a configured host, useful for
  real hardware or an emulator behind NAT with no inbound path — is
  a proposed future addition, not yet built (tracked as
  [amipilot#12](https://github.com/sidick/amipilot/issues/12)); the
  framing and dispatch below apply unchanged either way, only the
  connection's *direction* would differ.
- The protocol itself is transport-agnostic: anything that carries a
  byte stream in each direction (Copperline's `[serial] mode = "tcp"`
  bridge, a null-modem cable, a TCP socket) carries it unchanged.

## Requests

One command per line, terminated by LF (`\n`). A CR before the LF is
stripped (CRLF tolerated); empty lines are ignored. Maximum request line
is 512 bytes including the terminator — the host client (`host/
amipilot/wire.py`'s `MAX_LINE`) rejects an oversized command locally
before ever sending it; a line that reaches this length on the wire
regardless (e.g. a non-Python client) is rejected explicitly by the
server with `RC 10` and an "argument too long"/"request line too
long" message, **not silently truncated into a shorter, different
command** — a truncated-but-still-well-formed line would otherwise be
indistinguishable from a genuinely shorter one, which is a
correctness risk (acting on the wrong path/pattern), not just a
cosmetic error.

The command grammar is exactly the ARexx port's verb set — same
keywords, same arguments, same `@name` manifest locators, same quoting
rules (see `server/README.md` and `userdocs/`). A quoted argument may
contain a literal `"` or `\` by escaping it as `\"`/`\\` — the same
backslash-escaping convention the server's own text replies already
use for fields that might contain one (a filename/title/label);
`host/amipilot`'s `_quote()` applies it automatically. The wire adds
no verbs of its own except that `VERSION` (below) is its handshake.

**Text encoding: Latin-1 (ISO-8859-1), matching AmigaOS's own native
convention.** Every text payload — request lines, and non-binary
response text (`TREE`, `GETTEXT`, window/gadget labels, `MENU`, error
messages) — is a sequence of raw bytes interpreted as Latin-1, not
UTF-8. This isn't an arbitrary choice: `keymap.library`'s `MapANSI()`
(what `AmipTypeString()`, `server/src/action.c`, uses to synthesize
`TYPE`'s keystrokes) is documented as encoding an "ANSI byte string,"
the same convention Amiga text/console I/O has always used, so
treating the wire as Latin-1 end-to-end matches what the Amiga side
actually does with the bytes. `host/amipilot/wire.py` encodes/decodes
accordingly (`Reply.text`'s `.decode("latin-1")`,
`WireClient.command()`'s `.encode("latin-1")`) — a non-Latin-1
character (e.g. an emoji, CJK text) raises `UnicodeEncodeError`
immediately, client-side, before it ever reaches the socket, rather
than being silently mangled or sent as multi-byte UTF-8 the Amiga side
would misinterpret one byte at a time. `FSGET`/the file API's payloads
are the one exception: those are explicitly raw, charset-agnostic
bytes (may contain embedded NULs), never decoded as text at all — see
"File API" in `server/README.md`.

## Responses

Every request gets exactly one response, in request order:

```
RC <code> <byte-count>\n
<byte-count bytes of payload>
```

- `<code>` is the same RC policy the ARexx port uses (`arexx_cmd.h`):
  `0` success, `5` warning (well-formed but nothing to act on — window or
  gadget not found), `10` error (bad syntax / unknown command / bad
  locator), `20` failure (the action itself didn't deliver).
- `<byte-count>` is a decimal byte count, possibly `0`. The payload is
  raw bytes — no escaping, no terminator of its own; the next `RC` header
  begins immediately after. Payload text (TREE output, GETTEXT values,
  error messages) is the same stable text format the ARexx RESULT string
  carries; TREE payloads end with a newline per line as `AmiInspect`
  prints them, GETTEXT payloads are the exact value with no added
  newline.
- On errors (RC 5/10/20) the payload, when non-empty, is a one-line
  human-readable reason.

A client MUST parse by byte count, never by scanning for delimiters in
the payload.

## Handshake: `VERSION`

A client's first command after opening the transport SHOULD be
`VERSION`. Response payload (RC 0), three LF-terminated lines:

```
AMIPILOT <major>.<minor> PROTOCOL 1
STABLE VERSION
EXPERIMENTAL TREE CLICK TYPE GETTEXT MANIFEST LAUNCH FSLIST FSSTAT FSMKDIR FSDELETE FSGET MENU MENUPICK SCREENS AUTH QUIT
```

- Line 1: server version (from `version.mk`) and the wire protocol
  version this spec defines. A client that doesn't recognise the
  protocol number MUST disconnect rather than guess.
- Lines 2–3: the verb sets, space-separated after the leading keyword.
  Per the implementation plan, experimental verbs may change between
  minor releases; stable verbs never break within a major. Everything
  except `VERSION` itself is experimental until the 1.0 promotion pass.

`VERSION` is also available over ARexx (same payload as the RESULT
string) so on-Amiga scripts can feature-test too.

## Authentication: `AUTH`

`AUTH <password>` is answerable on every transport, but only ever
**gates** anything on TCP — ARexx and serial.device keep their
existing implicit trust boundaries (local machine, physical cable)
and never check whether it succeeded. On TCP, until `AUTH` succeeds,
every command except `VERSION` and `QUIT` returns `RC 10` without
being dispatched. Right password → `RC 0 0`; wrong password → `RC 10`
with a one-line reason. No rate-limiting or lockout on repeated
guesses — deliberate, see the SECURITY note under "Transport" above
and `server/README.md`'s TCP section.

## Sessions and lifecycle

Serial has no connection concept: the server services whatever arrives,
whenever it arrives, and a client can attach mid-run. TCP does have a
connection concept, but the policy is the same shape: only one
connection is active at a time (a new one replaces whatever was
connected before, closing it first). State on the server (the loaded
manifest) is global, not per-connection either way — one test session
at a time is the model, same as the ARexx port.

`QUIT` replies (`RC 0 0`) before the server exits.

## Versioning

This document defines **protocol 1**. Breaking changes (framing, RC
policy, handshake shape) bump the protocol number and get a new section
here; new verbs and new payload fields on experimental verbs do not.
