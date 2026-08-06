# Wire Protocol (serial)

From 0.3, `AmiPilotServer` can carry its whole command set over
serial.device as well as ARexx — same verbs, same arguments, same
`@name` manifest locators — so a **host machine** (or anything else on
the far end of a serial cable) can drive Amiga GUIs. This page is the
practical guide; the formal contract is
[`server/WIRE.md`](https://github.com/sidick/amipilot/blob/main/server/WIRE.md)
in the repository.

## Starting it

```
> Run AmiPilotServer SERIAL
```

Options (ReadArgs template `SERIAL/S,SERDEVICE/K,SERUNIT/K/N,BAUD/K/N`):

| Argument | Default | Meaning |
|----------|---------|---------|
| `SERIAL` | off | Enable the wire transport (the ARexx port is always on) |
| `SERDEVICE` | `serial.device` | Device driver — name a multi-port card's driver here |
| `SERUNIT` | `0` | Device unit |
| `BAUD` | `19200` | Line rate; both ends must agree. 19200 is a safe floor for a plain 68000; faster CPUs handle more |

The line is 8N1 with xon/xoff **disabled** (responses are binary-safe;
software flow control would corrupt them). If the serial device can't be
opened, the server exits with an error rather than silently running
without its transport.

Under an emulator you usually don't need a real cable: Copperline's
`[serial] mode = "tcp"` (or `--serial tcp`) bridges the guest's serial
port to a host TCP socket (default `127.0.0.1:1234`), which is exactly
what the host test harness uses.

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
