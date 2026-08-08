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
> Run AmiPilotServer SERIAL FSROOT=Work:amipilot-staging
```

`SERIAL` and `TCP` are independent — enable either or both. Options
(ReadArgs template `SERIAL/S,SERDEVICE/K,SERUNIT/K/N,BAUD/K/N,TCP/S,
TCPPORT/K/N,FSROOT/K/M,TCPALLOW/K,TCPPASSWORD/K`):

| Argument | Default | Meaning |
|----------|---------|---------|
| `SERIAL` | off | Enable the serial.device transport (the ARexx port is always on) |
| `SERDEVICE` | `serial.device` | Device driver — name a multi-port card's driver here |
| `SERUNIT` | `0` | Device unit |
| `BAUD` | `19200` | Line rate; both ends must agree. 19200 is a safe floor for a plain 68000; faster CPUs handle more |
| `TCP` | off | Enable the TCP transport (bsdsocket.library) |
| `TCPPORT` | *(required with `TCP`)* | Listen port |
| `FSROOT` | off (file API disabled) | Grants a directory to the file API (see [File API](#file-api) below). Repeatable — `FSROOT=Work:a FSROOT=Work:b` grants both. The directory must already exist; it's locked once at startup and held for the server's whole run. |
| `TCPALLOW` | off (every source accepted) | A source-IP/CIDR allowlist for `TCP`, comma-separated for multiple entries in this one value (`TCPALLOW=192.168.1.0/24,10.0.0.5`) — **not** repeatable like `FSROOT`, see [Securing TCP](#securing-tcp) for why. |
| `TCPPASSWORD` | `amipilot` (public default) | The password the `AUTH` verb checks, `TCP` only. See [Securing TCP](#securing-tcp) — this is not real security on its own. |

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

### Connecting from a real serial port (host side)

The Copperline-bridge path above is convenience, not the only option
— `host/amipilot` can also connect directly over a real (or virtual)
serial port on the host machine, no TCP bridge involved at all. This
is what you need for **real Amiga hardware** over a real cable, or a
Copperline config that itself uses a real serial device instead of
`[serial] mode = "tcp"`. Requires the optional `pyserial` dependency
(`pip install amipilot[serial]`) — everything else about `amipilot`
works with no `pyserial` installed at all.

```python
from amipilot import Amipilot

client = Amipilot.connect_serial("/dev/tty.usbserial-1420", 19200)
```

`device` is OS-specific — `/dev/tty.usbserial-*` on macOS,
`/dev/ttyUSB0`/`/dev/ttyS0` on Linux, `COM3` on Windows. `baud` must
match whatever `AmiPilotServer SERIAL` was actually started with on
the Amiga side — `BAUD` in the table above, default `19200` on both
ends. This is genuinely the same wire, the same verbs, the same RC
semantics as the TCP path; only the transport differs.

Under pytest, the `amipilot` fixture (see
[Building and Testing](Building-and-Testing.md)) takes the same
config via `--amipilot-serial-device`/`--amipilot-serial-baud` (or the
`amipilot_serial_device` ini setting) instead of `--amipilot-config` —
whichever one is set is what the fixture connects with; setting both
is a configuration mistake and fails immediately rather than silently
picking one. Unlike the Copperline path, this mode doesn't boot or
manage any process itself — whatever's on the other end of the cable
must already be running `AmiPilotServer SERIAL` before the test
session starts.

## Talking to it

Send one command per line (LF-terminated; CRLF is fine). Every command
gets exactly one response:

```
RC <code> <byte-count>
```

followed by exactly `<byte-count>` bytes of payload. The codes are the
same as the ARexx port's `RC` values: `0` OK, `5` warning (nothing
matched), `10` error (bad command), `15` timeout (an awaited condition
or payload — `WAITFOR`, `CLICK ... EXPECT=`, `FSPUT` — never arrived
within `TIMEOUT=`), `20` failure (the action didn't deliver). Parse
payloads by the byte count, never by scanning for delimiters — a
`TREE` payload contains newlines.

A quick manual session over the Copperline bridge:

```
$ nc 127.0.0.1 1234
VERSION
RC 0 161
AMIPILOT 0.3 PROTOCOL 1
STABLE VERSION
EXPERIMENTAL TREE CLICK TYPE GETTEXT MANIFEST LAUNCH FSLIST FSSTAT FSMKDIR FSDELETE FSGET MENU MENUPICK SCREENS AUTH QUIT
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

## File API

From 0.4, a connected session can list/stat/create/delete files and
read one back, scoped to directories explicitly granted at startup via
one or more `FSROOT` options (above) — nothing is granted implicitly,
and there is no way to grant a root once the server is running:

```python
from amipilot import Amipilot, CommandError, NotFound

with Amipilot.connect("127.0.0.1", 1234) as client:
    for entry in client.fs_list("Work:amipilot-staging"):
        print(entry.name, "dir" if entry.is_dir else "file", entry.size)

    data = client.fs_get("Work:amipilot-staging/results.log")

    client.fs_mkdir("Work:amipilot-staging/run3")
    client.fs_delete("Work:amipilot-staging/run3")

    try:
        client.fs_list("SYS:")          # outside every granted root
    except CommandError:
        pass                             # RC 10, expected
```

`fs_stat()` returns the same `FsEntry` shape as one `fs_list()` row —
name, `is_dir`, `size`, `prot` (the classic four-character `rwed`
string, inverted-bit gotcha already handled), `date`, `comment` — for
a single path without listing a whole directory.

**Containment is checked by lock identity, not string matching** —
`Lock()`/`ParentDir()`/`SameLock()`, walking up from the target to see
whether it lands on a granted root. Amiga assigns mean two different
path *strings* can name the same or a nested location, so a prefix
check would be both wrong (missing genuine matches) and unsafe
(missing genuine escapes). A path outside every granted root raises
`CommandError` (`RC 10`) naming what *is* granted; a genuinely missing
path raises `NotFound` (`RC 5`).

`fs_get()` returns the file's exact bytes (`Reply.payload`, not the
NUL-terminated `.text` other verbs use — a file's contents may
legitimately contain embedded NULs, and the wire's length-prefixed
framing carries them intact). The server caps this at its own small
internal buffer (`server/src/fs.c`'s `AMIP_FS_BUF_SIZE`, currently
16KB) and raises `ActionFailed` (`RC 20`) for anything larger — this
is a test-staging channel for small fixtures, config, and log files,
not a general file transfer mechanism.

**`fs_put(path, data, *, timeout=30.0)` writes a file host-to-Amiga**,
creating it if it doesn't exist and overwriting it if it does:

```python
    client.fs_put("Work:amipilot-staging/results.log", b"hello\x00world")
    assert client.fs_get("Work:amipilot-staging/results.log") == b"hello\x00world"
```

This is the wire's first request to carry a raw binary body: on the
wire it's `FSPUT <path> <byte-count> [TIMEOUT=<n>]`, the request-line
declaring how many raw bytes immediately follow it (no delimiter, no
escaping — the same length-prefixed idea the response side already
uses, just inverted). `fs_put()` is **wire-only** — there is no ARexx
equivalent at all, because `RexxMsg`/`ARG0()` only ever carries string
arguments, so there's genuinely no channel to receive a binary payload
over that transport. Same allowlist/containment rules and
`AMIP_FS_BUF_SIZE` cap as `fs_get()`. `timeout` (default 30s, longer
than most other waits here — a real multi-KB payload over a slow
serial link needs it) bounds how long the server waits for the
declared payload to fully arrive after the request line; if it
doesn't, `fs_put()` raises `Timeout` (`RC 15`), distinct from
`ActionFailed` (`RC 20`) on a write that fails after a complete
payload arrived (e.g. disk full).

## Menus

From 0.4, a connected session can read a window's menu strip and
select an item by its keyboard shortcut:

```python
from amipilot import ActionFailed, Amipilot

with Amipilot.connect("127.0.0.1", 1234) as client:
    strip = client.menu("GadTools")
    project = strip.menus[0]
    print(project.title, [i.text for i in project.items])

    about = strip.find("About")             # look up by label instead
    client.menu_pick("GadTools", about.menu_num, about.item_num)

    try:
        client.menu_pick("GadTools", 0, 2)   # a disabled item, say
    except ActionFailed:
        pass                                  # RC 20, expected
```

`menu()` returns a `MenuStrip`: one `Menu` per pulldown title, each
with a list of `MenuItem`s and — one level deep, matching classic
Intuition's own limit — their submenu items. Each `MenuItem` carries
`text`, `checkit`/`checked` (for a checkmark toggle item),
`enabled`, `shortcut` (the single keyboard character, or `None`), and
`menu_num`/`item_num`/`sub_num` — the same 0-based chain positions
Intuition itself reports via `IDCMP_MENUPICK`'s `MENUNUM()`/
`ITEMNUM()`/`SUBNUM()` macros. `MenuStrip.find("some label")` looks up
an item by its text instead of hand-counting positions.

**`menu_pick()` selects by keyboard shortcut only, for now.** It
activates the window, then strikes the item's shortcut character with
the right-Amiga qualifier held — the same input.device path a human
pressing Right-Amiga+key produces. Intuition resolves that
combination against the window's own live menu strip; AmiPilot
doesn't (and can't) synthesize the pick event directly, so `RC 0` is
real evidence the pick reached the app through the genuine
menu-shortcut path. Raises `ActionFailed` (`RC 20`) if the item is
disabled, or if it has no keyboard shortcut at all — pointer-based
navigation (open the menu, move across items, release over the
target) for shortcut-less items is planned but not built yet; see
`server/README.md`.

## Screens

From 0.4, a connected session can see every open screen and target a
specific one when a window-title pattern is ambiguous:

```python
from amipilot import Amipilot

with Amipilot.connect("127.0.0.1", 1234) as client:
    for screen in client.screens():
        print(screen.title, screen.width, screen.height, screen.frontmost)

    # Two windows both matching "GadTools" on different screens --
    # narrow the search to a specific one:
    window = client.tree("GadTools", screen="Second Screen")

    # Waiting for an asynchronously launched program's own screen to
    # appear, instead of a fixed sleep:
    client.launch("SRC:build/fixtures/SecondScreenApp")
    screen = client.wait_for_screen("Second Screen", timeout=20.0)
    window = client.wait_for_window("GadTools", screen=screen.title)
```

`screens()` returns every screen's `title` (its `DefaultTitle` — the
app's own stable name for the screen, set once at open time — **not**
the live title-bar text, which tracks whichever window is currently
active on that screen via that window's own `WA_ScreenTitle` and so
isn't a safe identity to match against), position, size, and whether
it's frontmost.

`tree()`, `click()`, `type()`, `get_text()`, `menu()`, and
`menu_pick()` all accept an optional `screen=` keyword that narrows
the window search to screens whose title contains it — this is purely
for *disambiguating* two same-titled windows on different screens;
window-finding already searched every screen before this existed, it
just couldn't tell two matches apart. `TREE`/`MENU`'s own payload
gains a `screen="..."` field on the window line for the same reason
(`Window.screen`/`MenuStrip.screen` on the host side), so you can
confirm which screen a match actually landed on.

**Acting on a window brings its screen forward automatically.**
`click()`, `type()`, and `menu_pick()` already call `ScreenToFront()`
on the target window's own screen before injecting anything — this
predates `SCREEN=`/`SCREENS`, and there's no separate "bring this
screen to front" verb because none is needed. Read-only calls
(`tree()`, `get_text()`, `menu()`) deliberately leave screen order
alone.

`wait_for_window()`/`wait_for_screen()` poll `tree()`/`screens()`
until the target appears or `timeout` elapses (`TimeoutError` on
expiry) — the same host-side poll loop `launch()`'s own docs already
recommend by hand ("assert on the expected effect instead, e.g.
polling `TREE`"), promoted into reusable methods. There's
deliberately no server-side blocking wait verb: `AmiPilotServer`
services one command at a time, so a verb that blocks server-side for
a timeout would stall the whole server — including `QUIT` — for that
whole duration.

## Securing TCP

**AmiPilot's TCP transport is meant for a trusted LAN or a direct
machine-to-machine link — never expose it on an open/internet-facing
port.** This server can run arbitrary shell commands (`LAUNCH`),
read/write files inside a granted `FSROOT`, and inject GUI input;
that's real exposure on any network it's reachable from. Two
independent, opt-in, combinable options narrow who can connect and
what they can do without one:

- **`TCPALLOW=<ip-or-cidr>[,<ip-or-cidr>...]`** — a source-address
  allowlist, e.g. `TCPALLOW=192.168.1.0/24,10.0.0.5`. A single value,
  comma-separated for more than one entry (not repeatable like
  `FSROOT` — AmigaDOS's `ReadArgs()` only allows one repeatable `/M`
  keyword per template, and `FSROOT` already uses it). With none
  granted, every source is accepted — today's unchanged default. A
  rejected connection is closed immediately, with no reply ever sent.
- **`TCPPASSWORD=<value>`** gates a new `AUTH <password>` verb — TCP
  only, not ARexx or serial.device. If omitted, defaults to
  `"amipilot"`, which `Amipilot.connect()`/`connect_with_retry()`
  already send automatically, so TCP keeps working with zero config
  changes on either side.

```python
from amipilot import Amipilot, CommandError

# Matches a server started with TCPPASSWORD=correct-horse-battery-staple
try:
    client = Amipilot.connect("192.168.1.50", 6800, password="correct-horse-battery-staple")
except CommandError:
    ...  # wrong password
```

**Neither option is real security, and you should say so to anyone
relying on this.** The default password is public — it's in this
open-source repository. There's no TLS, so even a custom password
crosses the wire in cleartext, sniffable by anything on the same
network segment. There's no rate-limiting or lockout on repeated
`AUTH` guesses. `TCPALLOW`/`TCPPASSWORD` raise the bar above "wide
open to anyone," the same way a router's default admin password does
— nothing more. `AmiPilotServer` prints a warning to this effect every
time `TCP` is enabled, precisely so this isn't something you only find
out by reading documentation.
