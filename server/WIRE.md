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
- The protocol itself is transport-agnostic: anything that carries a
  byte stream in each direction (Copperline's `[serial] mode = "tcp"`
  bridge, a null-modem cable, later a TCP socket) carries it unchanged.

## Requests

One command per line, terminated by LF (`\n`). A CR before the LF is
stripped (CRLF tolerated); empty lines are ignored. Maximum request line
is 512 bytes including the terminator — longer lines are truncated and
will normally fail parsing (RC 10).

The command grammar is exactly the ARexx port's verb set — same
keywords, same arguments, same `@name` manifest locators, same quoting
rules (see `server/README.md` and `userdocs/`). The wire adds no verbs
of its own except that `VERSION` (below) is its handshake.

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
EXPERIMENTAL TREE CLICK TYPE GETTEXT MANIFEST QUIT
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

## Sessions and lifecycle

Serial has no connection concept: the server services whatever arrives,
whenever it arrives, and a client can attach mid-run. State on the
server (the loaded manifest) is global, not per-connection — one test
session at a time is the model, same as the ARexx port.

`QUIT` replies (`RC 0 0`) before the server exits.

## Versioning

This document defines **protocol 1**. Breaking changes (framing, RC
policy, handshake shape) bump the protocol number and get a new section
here; new verbs and new payload fields on experimental verbs do not.
