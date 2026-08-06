# Wire Protocol

From 0.3, `AmiPilotServer` can carry its whole command set over a byte
stream as well as ARexx — same verbs, same arguments, same `@name`
manifest locators — so a **host machine** can drive Amiga GUIs. Two
transports carry it: serial.device (0.3) and, from 0.4, TCP via
bsdsocket.library. This page is the practical guide; the formal
contract is
[`server/WIRE.md`](https://github.com/sidick/amipilot/blob/main/server/WIRE.md)
in the repository.

## Starting it

```
> Run AmiPilotServer SERIAL
> Run AmiPilotServer TCP TCPPORT=6800
> Run AmiPilotServer SERIAL TCP TCPPORT=6800
```

`SERIAL` and `TCP` are independent — enable either or both. Options
(ReadArgs template `SERIAL/S,SERDEVICE/K,SERUNIT/K/N,BAUD/K/N,TCP/S,
TCPPORT/K/N`):

| Argument | Default | Meaning |
|----------|---------|---------|
| `SERIAL` | off | Enable the serial.device transport (the ARexx port is always on) |
| `SERDEVICE` | `serial.device` | Device driver — name a multi-port card's driver here |
| `SERUNIT` | `0` | Device unit |
| `BAUD` | `19200` | Line rate; both ends must agree. 19200 is a safe floor for a plain 68000; faster CPUs handle more |
| `TCP` | off | Enable the TCP transport (bsdsocket.library) |
| `TCPPORT` | *(required with `TCP`)* | Listen port |

The serial line is 8N1 with xon/xoff **disabled** (responses are
binary-safe; software flow control would corrupt them). TCP is
listen-mode only today — the server binds and listens, the host
connects in, and only one connection is treated as active at a time (a
new one replaces the old). If a requested transport can't be opened,
the server exits with an error rather than silently running without
it.

Under an emulator you usually don't need a real cable or a network
bridge for the *serial* transport: Copperline's `[serial] mode = "tcp"`
(or `--serial tcp`) bridges the guest's serial port to a host TCP
socket (default `127.0.0.1:1234`), which is exactly what the host test
harness uses. Reaching the *TCP* transport instead needs the guest's
own network reachable from the host — Copperline's `[hostsocket]`
board bridged to a real or virtual adapter, or a real Amiga with
TCP/IP on the LAN.

## Talking to it

Send one command per line (LF-terminated; CRLF is fine). Every command
gets exactly one response:

```
RC <code> <byte-count>
```

followed by exactly `<byte-count>` bytes of payload. The codes are the
same as the ARexx port's `RC` values: `0` OK, `5` warning (nothing
matched), `10` error (bad command), `20` failure (the action didn't
deliver). Parse payloads by the byte count, never by scanning for
delimiters — a `TREE` payload contains newlines.

A quick manual session over the Copperline bridge:

```
$ nc 127.0.0.1 1234
VERSION
RC 0 90
AMIPILOT 0.3 PROTOCOL 1
STABLE VERSION
EXPERIMENTAL TREE CLICK TYPE GETTEXT MANIFEST QUIT
GETTEXT GadTools 2
RC 0 10
aminet.net
```

`VERSION` is the handshake: a client should send it first and check the
`PROTOCOL` number before anything else. It works over ARexx too, for
on-Amiga feature tests.

## The host client

The repository's `host/` Python package speaks this protocol
(`amipilot.wire.WireClient`) — connect, handshake, command:

```python
from amipilot.wire import WireClient

client = WireClient.connect("127.0.0.1", 1234)
info = client.handshake()          # checks PROTOCOL 1
client.command("TYPE GadTools 2 aminet.net")
reply = client.command("GETTEXT GadTools 2")
assert reply.text == "aminet.net"
```

The commands themselves are documented in the
[ARexx Reference](ARexx-Reference.md) — the wire adds no verbs of its
own beyond `VERSION`.

## The object API and `amipilot dump`

Most scripts won't touch `WireClient` directly — `amipilot.client`
wraps it in a Pythonic API that raises exceptions on non-OK replies
instead of requiring you to check RC codes by hand:

```python
from amipilot import Amipilot, NotFound

with Amipilot.connect("127.0.0.1", 1234) as client:
    client.type("GadTools", 2, "aminet.net")
    window = client.tree("GadTools")           # -> Window, gadgets parsed
    try:
        client.click("GadTools", 99)
    except NotFound:
        ...
```

`amipilot dump "<window>"` (installed via `pip install -e host/`) is the
host-side half of [the inspector](https://github.com/sidick/amipilot/blob/main/docs/implementation-plan.md#the-inspector):
connects, fetches a window's tree, and prints it — either the same text
`AmiInspect` prints (`--format text`, the default) or quirk-profile-ready
`# name = <id>` suggestions (`--format python`) to copy into a test.

## LAUNCH

From 0.4, a connected session can start its own test subject instead of
requiring it pre-staged (e.g. via `S:User-Startup`):

```python
from amipilot import Amipilot, NotFound

with Amipilot.connect("127.0.0.1", 1234) as client:
    try:
        client.tree("GadTools")
        raise AssertionError("should not be running yet")
    except NotFound:
        pass

    client.launch("SRC:build/fixtures/GTApp", stack=8192)

    # LAUNCH is asynchronous -- poll for the effect, don't assume timing.
    import time
    for _ in range(20):
        try:
            window = client.tree("GadTools")
            break
        except NotFound:
            time.sleep(0.5)
```

`stack` sets the new process's stack size in bytes (AmigaDOS's own
default is 4000 if omitted — most Intuition/ReAction GUI apps need
more). On the wire this is `LAUNCH [STACK=<n>] <command-line...>`; the
command line is Shell syntax, sent verbatim, and must not itself
contain a line terminator.

**Read this before relying on `RC 0`:** the launch is asynchronous
(`SystemTagList()`, `SYS_Asynch`) so AmiPilotServer keeps servicing the
connection while the launched process runs — necessary for anything
that doesn't exit on its own, like a GUI app. That means `RC 0` only
confirms the new AmigaDOS process could be *created* (no memory
exhaustion, a free process slot) — **not** that the command was found
or that it ran successfully; the shell resolves the command name after
`LAUNCH`'s own reply has already gone out, and there's no output
capture yet to see that failure. Always assert on the expected effect
(a window appearing, as above) rather than trusting the RC alone. A
`proc-wait` verb for real exit-code retrieval is planned but not built
yet — see `server/README.md`.
