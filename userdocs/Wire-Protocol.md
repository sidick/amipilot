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

## WBLAUNCH

From 1.0, a connected session can also start a target the way a user
actually would — double-clicking its icon — instead of `launch()`'s
Shell-style start:

```python
from amipilot import Amipilot, CommandError

with Amipilot.connect("127.0.0.1", 1234) as client:
    client.wb_launch("SRC:build/fixtures/WBApp")

    client.wb_launch(
        "SRC:build/fixtures/WBApp",
        tooltypes={"PORT": "7777"},
        args=["Work:data/one.txt"],
    )

    try:
        client.wb_launch("SRC:build/fixtures/NoSuchApp")
    except CommandError:
        pass  # RC 10, bad icon path
```

This is a genuinely different mechanism from `launch()`, not a
cosmetic variant: it hand-builds a real `WBStartup`/`WBArg` message and
sends it to a real non-CLI `CreateNewProc()` process, the exact
handshake a Workbench-aware program's own startup code (libnix's
`_WBenchMsg`, or any compiler's equivalent) waits for — so tooltype
parsing and `WBStartup` argument handling are code paths the launched
program actually exercises, which `launch()`'s Shell start never
touches at all.

`icon_path` is the tool or project icon's path **without** the
".info" suffix (icon.library's own convention) — `wb_launch()` reads
the real icon, and for a project icon resolves its own default tool
automatically (the project itself becomes a second argument, matching
what double-clicking a project icon actually does).

`tooltypes`, if given, overrides (or adds, if not already present)
just the named tooltypes for this one launch — everything else on the
real icon is left alone. This needs a real, if surprising, mechanism:
there is no in-memory channel to hand a tooltype override to an
off-the-shelf binary, since the launched program discovers its own
tooltypes by reading its own icon file back off disk. `wb_launch()`
instead writes a **scratch** copy of the icon (merged tooltypes) to
`T:` — never touching the app's real `.info` — and points the launch
at that instead; it's cleaned up automatically once the launched
process exits.

`args`, if given, are additional fully-qualified paths passed as
further project-file arguments (the "multiple files selected, one is a
tool" case double-clicking with extended-select icons produces).

Same async honesty as `launch()`: this raises `CommandError` as soon
as the icon/path itself is rejected (bad icon, unsupported icon type,
an unlockable `ARG=`/`TOOLTYPE=`-scratch path), `ActionFailed` if
`CreateNewProc()` itself fails (out of memory, no process slot) — but
neither confirms the launched program actually finished starting;
assert on its expected effect, same as `launch()`.

Unlike `FSPUT`, `WBLAUNCH` carries no binary wire payload, so — unlike
that verb — it's fully answerable over ARexx too; there's no wire-only
asymmetry here.

## SCREENSHOT

From 1.0, a connected session can capture a screen's raw pixels for
human viewing/debugging/documentation — [GitHub issue
#41](https://github.com/sidick/amipilot/issues/41). This is
inspector tooling, **not** a locator mechanism: `click()`/`type()`/
`get_text()` stay structural/semantic, this doesn't change that.

```python
from amipilot import Amipilot, NotFound

with Amipilot.connect("127.0.0.1", 1234) as client:
    shot = client.screenshot()                     # frontmost screen
    ilbm_path, png_path = shot.save("/tmp/capture")  # writes both

    window_shot = client.screenshot(window="GadTools")
    print(window_shot.crop)  # (x, y, w, h) within its owning screen

    try:
        client.screenshot(screen="NoSuchScreen")
    except NotFound:
        pass
```

Same "wire stays simple, host does the rendering" split this project
already uses for `TREE`/`amipilot dump` (no JSON on the wire, ever):
the Amiga side sends raw, **uncompressed** bitplane bytes plus a small
header — width, height, depth, the `CAMG` view-mode bits (`HAM`/
`EHB`/`LACE`/`HIRES`), a palette, and (for a `WINDOW=` capture) a
crop rectangle — see `server/include/screenshot.h`'s own header
comment for the exact byte layout. **All image-format work happens
host-side**, `amipilot.screenshot`, stdlib only (`zlib`, no Pillow):

- `shot.to_ilbm()` — a real IFF `FORM ILBM` (`BMHD`/`CMAP`/`CAMG`/
  `BODY`), a near-direct copy of the already-planar capture since
  that's ILBM's own native shape. Viewable with Multiview or any
  Amiga paint program, and preserves the exact view mode the capture
  came from.
- `shot.to_png()` — de-planed to chunky, palette-indexed pixels
  first (`shot.to_chunky()`), then a straightforward indexed-color
  PNG. Easier for modern tooling (browsers, CI artifact viewers,
  image diffing) to work with than ILBM, at the cost of real
  per-pixel unpacking work host-side — cheap there, which is the
  whole point of doing it host-side rather than on the 68000.
- `shot.save(path)` writes both `<path>.iff` and `<path>.png` in one
  call.

**Planar screens only by intent — with a real, currently unguarded gap
for RTG.** A Picasso96/CGX screen's `BitMap` still has `Planes[]`/
`BytesPerRow`/`Depth` fields, but they're not real chip-mem bitplanes
on an RTG screen; walking them risks reading garbage pointers, the
same risk class as the `WBPattern`/`GTYP_CUSTOMGADGET` hang found and
fixed in issue #36. An earlier attempt to guard against this
(`BitMap->Flags & BMF_STANDARD`) turned out to be simply wrong, not
just imperfect — live testing against a real, completely ordinary
Copperline Workbench screen showed that flag is never set on
Intuition's own screen bitmaps regardless of whether they're planar,
so it rejected the normal case it was meant to allow. It was removed
rather than replaced with another guess. **There is currently no
detection of a genuine RTG/P96 screen at all** — targeting one is a
real, unverified risk. Real detection is tracked separately as
[issue #44](https://github.com/sidick/amipilot/issues/44).

With both `screen`/`window` omitted, `screenshot()` captures the
frontmost/default public screen; `screen` alone selects one by
`DefaultTitle` substring; `window` (optionally narrowed by `screen`,
resolved exactly like `click()`'s own window pattern) captures that
window's OWNING SCREEN in full, with the window's rectangle on
`shot.crop` — there is no separate per-window pixel buffer to grab on
classic Intuition (overlapping windows share one screen bitmap), so
that's what a "window screenshot" actually is on any windowing system.

Palette precision is 4-bit-per-gun (12-bit RGB, expanded to 8-bit-per-
channel for the wire/PNG/ILBM) — the real, V33-era-safe way to read a
`ColorMap` (`GetRGB4()`, not `ColorMap->ColorTable` directly, which is
an opaque `APTR` in the real struct, not the plain array it might look
like). A real AGA 8-bit-per-gun capture would need `GetRGB32()` (V39+,
above this project's V37 floor) — not attempted here.

**Serial transfer time, no compression on the wire (8N1, byte rate ≈
baud/10):**

| Capture (typical example)                       | Size    | 9600 baud | 19200 baud (default) | 38400 baud | 57600 baud |
|---------------------------------------------------|---------|-----------|-----------------------|------------|------------|
| 320x256, 16 colours (4 planes)                     | ~41 KB  | ~43 s     | ~21 s                 | ~11 s      | ~7 s       |
| 320x256, 32 colours (5 planes)                     | ~50 KB  | ~53 s     | ~27 s                 | ~13 s      | ~9 s       |
| 640x512 interlaced, 256 colours (8 planes, AGA)    | ~320 KB | ~5.7 min  | ~2.9 min              | ~1.4 min   | ~57 s      |
| Server's own size cap (`AMIP_SCREENSHOT_MAX_BYTES`) | 512 KB  | ~9.1 min  | ~4.6 min              | ~2.3 min   | ~1.5 min   |

Serial is genuinely slow for anything beyond a small/low-colour
screen — **prefer TCP** for `SCREENSHOT`, which isn't baud-limited at
all. These numbers exist so a serial-only setup (real hardware without
a network card, or a Copperline config without `--serial tcp`) knows
what to expect rather than guessing why a capture appears to hang.

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
