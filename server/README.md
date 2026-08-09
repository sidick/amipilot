# server

The AmiPilot server commodity: hosts the `intuition-model` walker, the
action engine (input.device event synthesis), and the transports (ARexx
first, then serial.device, then TCP).

Lands in phase 0.2 onward -- see
[`docs/implementation-plan.md`](../docs/implementation-plan.md).

## Current state (phase 0.4, shipped in v0.4)

- **The wire (phase 0.3, shipped in v0.3):** the same verb grammar over
  serial.device, framed per [`WIRE.md`](WIRE.md) (this directory) --
  LF-terminated request lines in, `RC <code> <byte-count>` + payload
  out, `VERSION` as the handshake. `src/serial.c` is the transport
  (standing async 1-byte read + SDCMD_QUERY chunk drain, xon/xoff
  disabled); one parser (`arexx_cmd.c`) and one dispatch
  (`HandleCommand` in `amipilotserver/main.c`) serve every transport,
  so ARexx RESULT strings and wire payloads are the same bytes. Enable
  with `AmiPilotServer SERIAL` (`SERDEVICE`/`SERUNIT`/`BAUD` to taste).
  Verified end-to-end by `make test-target`'s wire check: the real host
  client (`host/amipilot/wire.py`) connects through Copperline's
  `--serial tcp` bridge, handshakes, TYPEs into the fixture's string
  gadget, reads the value back, clicks Connect, and confirms the
  window died -- the phase 0.3 loop minus pytest, host-driven.
  `SERDEVICE`/`SERUNIT`/`BAUD` were already configurable server-side
  from day one; the host had no matching way to reach a REAL serial
  port until `WireClient.connect_serial()`/`Amipilot.connect_serial()`
  and the pytest plugin's `--amipilot-serial-device`/
  `--amipilot-serial-baud` (optional `pyserial` dependency) --
  see [Wire Protocol](../userdocs/Wire-Protocol.md#connecting-from-a-real-serial-port-host-side).
- **TCP (phase 0.4, shipped in v0.4):** the same wire over
  bsdsocket.library, listen-mode -- `src/tcp.c`, enabled with
  `AmiPilotServer TCP TCPPORT=n` (`SERIAL` and `TCP` are independent;
  either or both may be given at once). Readiness is driven by
  `WaitSelect()`, not the more obvious `SBTC_SIGEVENTMASK` +
  `SO_EVENTMASK` + `GetSocketEvents()` async-notification mechanism the
  headers also document -- that path reported success at every step
  (`SocketBaseTags()` returned 0, a plausible signal bit came back) but
  never actually delivered a signal when tested live against a real
  Copperline hostsocket bridge; `WaitSelect()` (the mechanism
  bsdsocktest's own conformance suite exercises directly) does. See
  `tcp.c`'s own header comment for the full story, including a second
  confirmed-live gotcha: a command sent before the connection is truly
  ready gets silently dropped, not buffered, so the client needs to
  resend on a *held* connection rather than reconnect (`host/amipilot/
  client.py`'s `Amipilot.connect_with_retry()` does this on the host
  side).

  **Verified end-to-end on two independent bsdsocket.library
  implementations**, not just one: a full `VERSION`/`TREE`/`TYPE`/
  `GETTEXT`/`CLICK`/`TREE`/`QUIT` round trip against Amiberry (whose
  bsdsocket.library forwards straight to the host OS's real socket
  API -- `amipilot.uae`'s `bsdsocket_emu=true`, no virtual networking
  involved at all, just `Run AmiPilotServer TCP TCPPORT=n` and connect
  to that port on the host directly), and the same round trip earlier
  against a real Copperline hostsocket bridge (bridged to a host
  `feth` pair, before Copperline 0.15's own `net="host"` backend
  existed). The two implementations are architecturally unrelated
  (Copperline's is a from-scratch smoltcp-based TCP/IP stack;
  Amiberry's is a thin syscall-forwarding shim), so agreement between
  them is real evidence the `WaitSelect()` design is correct, not an
  artifact of one emulator's specific behaviour. **Now wired into
  `make test-target`** as a real, automated check
  (`run_tcp_host_check`, `tests/copperline/tcp-host-test.py`) using
  Copperline 0.15's `[hostsocket] net = "host"` backend
  (`--hostsocket-net host`) -- confirmed to delegate straight to a
  real host OS socket for `listen()`/`accept()`, so a host Python
  client reaches `AmiPilotServer TCP`'s listener directly with **no
  `feth` pair, no `/dev/bpf`, no root, and no static interface/
  address/gateway config at all**, unlike the `bridge` backend's own
  real setup cost (still documented in `tests/copperline/README.md`
  for anyone who needs it, e.g. genuinely testing cross-machine
  reachability rather than same-host loopback). Amiberry still isn't
  scripted headlessly the way Copperline is (see "On-target testing"
  in the top-level `CLAUDE.md`), so that half stays manual. Only
  listen-mode exists today (the server binds, the host connects in);
  a guest-initiated dial-out mode -- which would sidestep needing any
  bridge/NAT-forwarding setup at all, since the guest only needs
  outbound connectivity -- is proposed future work, see
  [amipilot#12](https://github.com/sidick/amipilot/issues/12).

- **Securing TCP (phase 0.4, shipped in v0.4):** `AmipTcpOpen()` binds
  `INADDR_ANY` and accepts any source by default -- fine for a trusted
  LAN, real exposure on anything else given this server can run
  arbitrary shell commands (`LAUNCH`), read/write files inside a
  granted `FSROOT`, and inject GUI input. Two independent, opt-in,
  combinable knobs raise the bar:

  - **`TCPALLOW=<ip-or-cidr>[,<ip-or-cidr>...]`** -- a single `/K`
    value, comma-separated for multiple entries, e.g.
    `TCPALLOW=192.168.1.0/24,10.0.0.5`. **Not `/M`**, unlike `FSROOT`
    -- `ReadArgs()`'s template grammar allows at most one `/M` keyword
    per template, and `FSROOT` already claims that slot; a second one
    makes `ReadArgs()` fail with `ERROR_BAD_TEMPLATE` on *every*
    invocation of `AmiPilotServer`, not just when `TCPALLOW` is used
    -- caught live via Amiberry verification, not assumed. Addresses
    are parsed by a small hand-rolled dotted-quad parser
    (`ParseDottedQuad()`, `tcp.c`), deliberately **not** `inet_aton()`:
    that call is a Roadshow-era bsdsocket extension at a high LVO
    offset, absent from every real bsdsocket.library implementation,
    and calling it under Amiberry's `bsdsocket_emu` produced a genuine
    CPU trap (illegal instruction) rather than a clean failure --
    reproduced in isolation with a 6-line standalone test program, so
    this wasn't specific to this server's own code. `inet_addr()` (the
    classic, portable alternative) was considered and rejected too:
    its failure return (`INADDR_NONE`, `0xFFFFFFFF`) is indistinguishable
    from the valid address `255.255.255.255`. Parsing four decimal
    octets by hand needed no library call at all, sidestepping both
    problems. With no `TCPALLOW` granted, every source is accepted,
    unchanged from this transport's original behavior. A rejected
    peer is closed immediately -- before ever becoming the active
    client, no reply ever sent -- so a disallowed connection can't be
    used to fingerprint this service.
  - **`TCPPASSWORD=<value>`** gates the new `AUTH <password>` verb
    (`server/WIRE.md`) -- TCP only, not ARexx or serial.device, which
    keep their existing implicit trust boundaries (local machine,
    physical cable). Defaults to `"amipilot"` when omitted (see
    `AMIP_TCP_DEFAULT_PASSWORD`) -- the bundled host client
    (`Amipilot.connect()`/`connect_with_retry()`) sends this
    automatically, so TCP keeps working out of the box with zero
    config changes on either side. Neither `TCPALLOW` nor
    `TCPPASSWORD` is mandatory; `TCP TCPPORT=n` alone behaves exactly
    as before this work.

  **This is explicitly NOT real security, and neither knob makes TCP
  safe to expose on an open/internet-facing port -- LAN or a direct
  machine-to-machine link only.** The default password is public (it's
  in this repo); there is no TLS, so even a custom password crosses
  the wire in cleartext; and there is no rate-limiting or lockout on
  repeated `AUTH` guesses. `AmiPilotServer` prints a warning to this
  effect every time `TCP` is enabled, not just here. `TCPALLOW`/
  `TCPPASSWORD` raise the bar above "wide open to anyone," nothing
  more -- treat them as you would a router's default admin password.

  **Verification, manual (same honest-limits precedent TCP's own
  original verification already established just above -- no
  automated `make test-target` check exists for the real TCP
  transport at all, only for serial.device carried over Copperline's
  `--serial tcp` bridge):** confirmed live via Amiberry
  (`bsdsocket_emu=true`), driving the real wire protocol from a host
  Python socket against `TCP TCPPORT=n TCPALLOW=... TCPPASSWORD=...`.
  This verification pass is *why* the implementation looks the way it
  does above, not an afterthought run once the code compiled -- it
  caught three real bugs before merge, none of which showed up any
  other way:

  1. **`ReadArgs()` failed on every single invocation, TCP or not.**
     The first `TCPALLOW/K/M` draft gave the template two `/M`
     keywords (`FSROOT` already had the one AmigaDOS allows per
     template) -- confirmed live as `ERROR_BAD_TEMPLATE` on startup,
     not caught by the cross-compiler (a syntax-valid string literal,
     wrong at the AmigaDOS level). Fixed by making `TCPALLOW` a plain
     `/K` value with comma-separated entries instead (see above).
  2. **`inet_aton()` trapped the CPU outright.** It's a Roadshow-era
     bsdsocket extension at a high LVO offset, absent from other real
     implementations; calling it under Amiberry's `bsdsocket_emu`
     produced a genuine illegal-instruction exception, not a clean
     "unsupported" failure -- reproduced in an isolated 6-line test
     program to confirm it wasn't specific to this server's own code.
     Fixed by parsing dotted-quad addresses by hand instead
     (`ParseDottedQuad()`, `tcp.c`) -- no library call, so no
     availability risk.
  3. **Rejection/auth-failure reasons never reached the wire.** The
     new early-return paths in `HandleCommand()` set the payload text
     but not its length, so the TCP dispatch loop sent `RC 10` with a
     silently empty payload -- confirmed by driving the actual bytes
     over a socket and seeing `len=0` where a message was expected,
     something a design read-through wouldn't have caught. Fixed by
     setting `*resultLenOut` on those paths, same as the fall-through
     tail already does for every other verb.

  With those three fixed, confirmed working end to end: (a) an
  unauthenticated connection has every non-`VERSION`/`AUTH`/`QUIT`
  command rejected, *with* the correct reason text now arriving; (b)
  `AUTH amipilot` (the default) succeeds and unlocks normal operation;
  (c) a wrong password is rejected (with the correct reason text) and
  doesn't unlock anything; (d) `TCPALLOW` set to a range excluding the
  test machine causes the connection to be reset with no reply, while
  a range including it connects and authenticates normally. A fourth,
  smaller gap surfaced during (d) and was also fixed: the rejection
  log line had no `fflush()`, so it sat in stdio's buffer instead of
  reaching a redirected log file until the process eventually exited
  -- confirmed by the client-side reset/no-reply happening correctly
  while the log stayed empty until `AmiPilotServer` was killed.

- **Program launch (phase 0.4, shipped in v0.4):** `LAUNCH [STACK=n]
  <command-line...>` starts an AmigaDOS process from a connected
  session -- so a test can connect first, confirm nothing is running
  yet, and start its own subject over the wire instead of pre-staging
  it via `S:User-Startup`. Implemented with `SystemTagList()`
  (`SYS_Asynch` so the launch doesn't block AmiPilotServer's own
  dispatch loop -- a persistent GUI app like a fixture never returns),
  with explicit `NIL:` input/output handles rather than defaulted ones:
  `SystemTagList()`'s own docs say an async launch closes the caller's
  `Input()`/`Output()` on completion "even if these were your Input()
  and Output()!", so leaving them unset would eventually close
  AmiPilotServer's own stdio. `STACK` sets `NP_StackSize` (bytes;
  AmigaDOS's own `CreateNewProc()` default is 4000 if omitted) -- most
  Intuition/ReAction GUI apps need more than that default. **Honest
  limit, not silently glossed over:** an async launch's own RC only
  reflects whether the shell process itself could be created (out of
  memory, no process slot) -- RC 0 does NOT mean the command was found
  or ran successfully, since the shell resolves the command name after
  `SystemTagList()` has already returned. There's no output capture
  yet either. Assert on the expected effect instead (e.g. polling
  `TREE` for the launched app's window), same as
  `tests/copperline/launch-test.py` does. Real command-found
  verification and exit-code retrieval need a `proc-wait` verb -- part
  of this phase's plan but not built here, not invented ahead of it.
  Verified end-to-end by `make test-target`'s LAUNCH check: connect to
  a bare server (no fixture pre-staged), confirm its window doesn't
  exist, `LAUNCH ... STACK=8192`, poll until the window appears, then
  a full `TYPE`/`GETTEXT`/`CLICK` round trip proves the launched
  process is genuinely functional with the non-default stack, not just
  that it didn't immediately crash.

- **`WBLAUNCH <icon-path> [TOOLTYPE=<key>=<value> ...] [ARG=<path> ...]`
  (phase 1.0, real Workbench-style launch):** `LAUNCH` above starts a
  Shell-style process (`SystemTagList()`); this is the other half
  `docs/implementation-plan.md`'s "Program launch" section calls for --
  a genuinely hand-built `WBStartup`/`WBArg` message sent to a real
  non-CLI `CreateNewProc()` process, the same mechanism real launcher
  utilities (AmigaOS 45's own `WBLoad`; the classic `WBRun`) use, not a
  V44+ shortcut: `workbench.library`'s `OpenWorkbenchObjectA()` looked
  tempting but its own autodoc BUGS section says launching (not just
  opening drawers) was unsafe and could trash memory up to and
  including V45.38 -- disqualifying against this project's V37 floor.
  `<icon-path>` is a tool or project icon's path WITHOUT the ".info"
  suffix (icon.library's own convention); a `WBPROJECT` icon resolves
  its own `do_DefaultTool` and becomes a second `WBArg` (the RKRM's own
  "Two Arguments" case), a `WBTOOL` icon launches directly. Repeatable
  `ARG=<path>` entries become further `WBArg` project-file arguments
  (Lock()-based, same containment-free convenience LAUNCH's own command
  line already has -- WBLAUNCH has no FSROOT-style allowlist of its
  own). **One real deviation from this feature's own original plan
  sketch, found doing the research:** the plan assumed tooltype
  overrides could be merged "in memory... no disk writes." That's not
  achievable against a real, unmodified target program: a Workbench-
  started app discovers its own tooltypes by calling `GetDiskObject()`
  on ITS OWN icon, found via the very `WBArg[0]` this verb hands it --
  there's no in-memory channel to hand tooltypes to an off-the-shelf
  binary at all. Repeatable `TOOLTYPE=<key>=<value>` entries are
  instead merged (existing keys not named are preserved, named ones
  overridden, new ones appended) into a scratch copy of the icon
  written to `T:` -- **never the app's own real `.info`** -- with the
  primary `WBArg` repointed at that scratch location instead; cleaned
  up automatically once the launched process exits and replies its
  `WBStartup` message (`server/src/wblaunch.c`'s `AmipWbPoll()`, folded
  into the main dispatch loop's own `Wait()`/`AmipTcpWait()`, the same
  shape `WAITFOR`'s own polling and every other async-completion signal
  here already uses). Same honest async caveat as `LAUNCH`: `RC 0`
  means the process was created and the startup message queued, not
  that the launched program actually finished starting or found its
  tooltypes/arguments meaningful -- assert on the expected effect.
  Unlike `FSPUT`, this carries no binary wire payload, so (like
  `LAUNCH`) it's fully answerable over ARexx too -- no wire-only
  asymmetry here. Verified end-to-end by `make test-target`'s WBLAUNCH
  check (`tests/copperline/wblaunch-test.py`) against a real,
  Workbench-startable fixture (`fixtures/wbapp`, whose own icon is
  stamped once by a small helper, `MakeIcon`, off the system's own
  default `WBTOOL` icon -- see `fixtures/wbapp/src/makeicon.c`'s own
  header): a bare launch reports both baked-in tooltypes unchanged; a
  `TOOLTYPE=` override changes one while leaving the other alone (the
  real merge, not a full replace); an `ARG=` path becomes a second
  `WBArg` the target actually sees; and a bad icon path is rejected
  (`RC 10`), not silently accepted.

- **`SCREENSHOT [SCREEN=<substring>] [WINDOW=<pattern>]` (phase 1.0,
  GitHub issues [#41](https://github.com/sidick/amipilot/issues/41)/
  [#44](https://github.com/sidick/amipilot/issues/44)):** raw,
  uncompressed bitmap capture (planar OR Picasso96/RTG) -- inspector
  tooling for human viewing/debugging/documentation, explicitly
  **not** a locator mechanism (`CLICK`/`TYPE`/`GETTEXT` stay
  structural/semantic, this doesn't change that). Same "wire stays
  simple, host does the rendering" split this project already uses
  for `TREE`/`amipilot dump`: the wire carries raw pixel bytes plus a
  small header -- see `server/include/screenshot.h`'s own header
  comment for the exact byte layout -- and ALL format/colour-space
  work (IFF ILBM, PNG, and P96 pixel-format decoding) happens
  host-side (`host/amipilot/screenshot.py`), stdlib-only (`zlib`, no
  Pillow). With both `SCREEN=`/`WINDOW=` omitted, captures the
  frontmost/default public screen; `WINDOW=` captures that window's
  OWNING SCREEN in full plus its rectangle in the response header,
  since classic Intuition has no separate per-window pixel buffer to
  grab (overlapping windows all share one screen bitmap) --
  "capturing a window" is always "capture the screen, then crop," the
  same thing any windowing system's real screenshot tool does; the
  host client crops to just that window BY DEFAULT.

  **Picasso96/RTG support (issue #44) is real, optional, and never
  required.** A Picasso96/CGX screen's `BitMap` looks the same shape
  as a classic one but its `Planes[]`/`BytesPerRow`/`Depth` fields
  aren't real chip-mem bitplanes -- P96 uses its own opaque, driver-
  private representation there, and walking `Planes[]` on one risks
  reading garbage pointers (the same risk class as the `WBPattern`/
  `GTYP_CUSTOMGADGET` hang found and fixed in issue #36). An earlier
  version tried to guard against this via `BitMap->Flags &
  BMF_STANDARD`; live testing against a real, completely ordinary
  Copperline Workbench screen proved that check WRONG, not just
  imperfect -- it rejected the ordinary planar case it was meant to
  allow (see issue #44's own comment thread for the finding). The
  REAL, verified mechanism (from Picasso96API.library's own published
  SDK, https://wiki.icomp.de/w/images/6/62/P96Develop.lha, NOT
  redistributed in this repo -- see `server/include/p96_compat.h`'s
  own header comment for exactly what was independently reproduced
  from its factual interface data, and why): `p96GetBitMapAttr(bm,
  P96BMA_ISP96)`, documented safe to call on ANY `struct BitMap *`
  without locking it first. `Picasso96API.library` is opened
  OPTIONALLY at startup (same graceful-degradation pattern as
  `GadToolsBase`/`KeymapBase`/`GfxBase`) -- absent, or the target
  screen isn't genuinely P96-backed, and the classic planar path
  (issue #41) runs completely unchanged, exactly as before #44. When a
  genuine P96 bitmap IS the target, pixel memory is read via
  `p96LockBitMap()`'s own `RenderInfo` buffer (the SDK's own required
  protocol -- "Picasso96 could move the bitmap's image data away
  while you are reading... you're likely to cause illegal memory
  accesses" otherwise), held only for the raw memcpy, then
  `p96UnlockBitMap()` immediately (the SDK's own docs: "never hold the
  lock for longer than about one second"). Whatever native pixel
  format P96 reports (CLUT/8-bit palette, or one of several truecolor/
  hicolor RGB byte orders) is sent RAW over the wire, unconverted --
  `host/amipilot/screenshot.py` decodes it host-side, including the
  documented `PC`-suffix byte-swap pitfall (16-bit `PC` formats are
  little-endian, non-`PC` ones big-endian; never assumed uniform).
  YUV formats are NOT supported -- the SDK's own docs mark them
  "hardware-window-only... bitmap operations may be implemented
  incompletely," so excluding them honors the SDK's own stated limit,
  not a scope cut. `to_ilbm()` (fundamentally planar) has no way to
  represent a P96 truecolor/hicolor capture and raises a clear error
  for one -- use `to_png()` (real true-colour PNG, no palette) or
  `to_rgb888()` instead; a P96 CLUT capture re-plane into a standard
  8-bitplane ILBM works fine, same as any 256-colour image.

  Palette precision (planar or P96 CLUT) is 4-bit-per-gun
  (`GetRGB4()`, V33+ -- not `ColorMap->ColorTable` directly, an opaque
  `APTR` in the real struct, and not `GetRGB32()`, V39+, above this
  project's V37 floor), expanded to 8-bit-per-channel for the wire/
  PNG/ILBM. Capped at `AMIP_SCREENSHOT_MAX_BYTES` (512KB, generous for
  this project's floor without permanently reserving that much memory
  -- the capture buffer is allocated on first use and only ever grown)
  -- a capture whose real size would exceed this is rejected outright,
  not silently truncated (a large P96 truecolor desktop can easily
  exceed it).

  **Serial transfer time, no compression on the wire (8N1, byte rate ≈
  baud/10):**

  | Capture (typical example)                    | Size     | 9600 baud | 19200 baud (default) | 38400 baud | 57600 baud |
  |------------------------------------------------|----------|-----------|-----------------------|------------|------------|
  | 320x256, 16 colours (4 planes)                  | ~41 KB   | ~43 s     | ~21 s                 | ~11 s      | ~7 s       |
  | 320x256, 32 colours (5 planes)                  | ~50 KB   | ~53 s     | ~27 s                 | ~13 s      | ~9 s       |
  | 640x512 interlaced, 256 colours (8 planes, AGA) | ~320 KB  | ~5.7 min  | ~2.9 min              | ~1.4 min   | ~57 s      |
  | `AMIP_SCREENSHOT_MAX_BYTES` cap                 | 512 KB   | ~9.1 min  | ~4.6 min              | ~2.3 min   | ~1.5 min   |

  Serial is genuinely slow for anything beyond a small/low-colour
  screen -- **prefer TCP** (`server/README.md`'s own TCP section) for
  `SCREENSHOT`, which isn't baud-limited at all; these numbers exist so
  a serial-only setup (real hardware without a network card, or a
  Copperline config without `--serial tcp`) knows what to expect rather
  than guessing why a capture appears to hang.

  Verified end-to-end by `make test-target`'s SCREENSHOT check
  (`tests/copperline/screenshot-test.py`, against a real running
  `fixtures/gadtools-app` window, not a synthetic payload): a bare
  (default-screen) capture has sane non-zero dimensions and a
  palette/plane-size shape matching its own declared depth; a
  `WINDOW=` capture reports a non-empty crop rectangle; both
  `to_ilbm()`/`to_png()` produce real, correctly-signed files on the
  host filesystem; and a bad `SCREEN=` substring is rejected. This
  also verifies the P96 code path's own graceful-degrade behavior
  live: Copperline (this project's on-target test environment) has no
  Picasso96/RTG emulation at all, so `Picasso96API.library` never
  opens there and every one of these checks exercises the classic
  planar path exactly as before issue #44 -- a real, live confirmation
  that adding optional P96 support didn't disturb the common case.
  **The P96-ACTIVE capture path is now verified too, and automated
  (GitHub issue #55).** First verified manually against real Picasso96
  3.6 + `uaegfx` under Amiberry: both P96 pixel-format shapes were
  exercised against a genuine P96 screen with a known pattern painted
  onto it -- a CLUT (8-bit palette) screen decoded back an exact,
  pixel-for-pixel match of the painted pen pattern; a truecolor
  (16-bit) screen using the byte-swapped `R5G6B5PC` layout decoded
  back colour values matching the expected 5/6/5-bit quantization
  exactly (not corrupted, not byte-order-garbled) -- real confirmation
  that the SDK's own `p96LockBitMap()`/`RGBFormat` contract and this
  module's own `PC`-suffix byte-swap handling both work as documented
  against genuine RTG hardware emulation, not just against the SDK's
  paper interface. Copperline 0.15 then shipped its own `[rtg]`
  support, closing the gap that made this Amiberry-only: reproduced
  the same CLUT verification under Copperline itself and wired it into
  `make test-target` for real (`run_screenshot_p96_check`,
  `fixtures/p96-app`, `tests/copperline/screenshot-p96-test.py`) --
  **skip-safe by design**, since most machines won't have `[rtg]`
  configured (opt-in) or the matching Picasso96 monitor driver
  installed; the fixture detects and reports exactly that as a genuine
  skip, not a failure. See `tests/copperline/README.md`'s own
  "P96/Picasso96 RTG" section for the two real, non-obvious things
  that had to be fixed to get this working at all (a CPU/address-space
  conflict between RTG and this project's own default A1200 profile,
  and a missing monitor driver for the specific hardware Copperline
  emulates). The parsing/encoding logic itself (exact byte layout
  including the P96 pixel-format decode table, IFF chunk shape, PNG
  chunk CRCs, IDAT round-trip) has its own dedicated host-side unit
  tests (`host/tests/test_screenshot.py`) against synthetic captures
  of both shapes too, independent of any emulator.

- **`WINDOWMOVE [SCREEN=<substring>] <window-pattern> <dx> <dy>` /
  `WINDOWSIZE [SCREEN=<substring>] <window-pattern> <width> <height>`
  (phase 1.0):** whole-window drag and resize, built on the exact same
  `AmipDragAt()` press/move/release primitive `DRAG`'s gadget forms
  already use -- not a new input-injection mechanism, just a different
  anchor point. `WINDOWMOVE` drags the window's own title bar
  (`WFLG_DRAGBAR`) by a relative pixel offset `<dx> <dy>`, anchored at
  the horizontal center of the title bar vertically centered in the
  window's `BorderTop` strip -- a documented, honest heuristic (not an
  attempt to locate the close/depth/zoom system gadgets' exact pixel
  extents and dodge them precisely) that's clear of them for any
  window wider than roughly 120px, effectively all real windows.
  `WINDOWSIZE` drags the window's own sizing gadget
  (`WFLG_SIZEGADGET`) from its current bottom-right corner (inset by
  half the border thickness on each axis) to wherever that corner
  needs to land to reach an ABSOLUTE target `<width> <height>`.
  Neither verb pre-checks the target against the window's own
  `MinWidth`/`MinHeight`/`MaxWidth`/`MaxHeight` (real, live fields on
  `struct Window`) -- Intuition's own sizing logic clamps the drag
  exactly as it would a genuine user drag, so the honest way to
  confirm the actual outcome is a follow-up `TREE` call, the same
  "verify the real outcome, don't assume the request was granted
  exactly" precedent `DRAG`'s own gadget forms already set. Both bring
  the window/screen forward first, same as every other action verb.
  `RC 20` ("window has no drag bar" / "window has no sizing gadget")
  if the target window never had that flag set at all -- a real,
  honest failure, not a silent no-op. **Classic locator form only, no
  `@name` manifest support** -- same scope as `TREE`/`MENU`, since
  this acts on a whole window, not a gadget within one, and there's no
  verified separate "resolve a window-only logical name" path in the
  manifest resolver as currently wired up. **No separate "get window
  position/size" verb was added** -- `TREE`'s own response already
  carries a window's current `[left,top WxH]` (`Window.left`/
  `Window.top`/`Window.width`/`Window.height` host-side), so querying
  before or after a move/resize is just another `TREE` call. Verified
  end-to-end by `make test-target`'s check
  (`tests/copperline/windowmoveresize-test.py`) against
  `fixtures/second-screen-app`'s window (the only fixture with a real
  sizing gadget -- `gadtools-app`/`classact-app` deliberately don't
  get one, since it changes border thickness and both have their own
  checked-in golden-tree fixture that a border-thickness change risks
  silently invalidating): a `WINDOWMOVE` lands at exactly the expected
  new `left`/`top`; a `WINDOWSIZE` lands at exactly the expected new
  `width`/`height`; a `WINDOWSIZE` against a window with no sizing
  gadget (`gadtools-app`) is honestly rejected; a `WINDOWMOVE` against
  a nonexistent window pattern is rejected as not-found.

- **File API (phase 0.4, shipped in v0.4):** `FSLIST`/`FSSTAT`/`FSMKDIR`/
  `FSDELETE`/`FSGET`, each taking a single `<path>` argument (quoted
  the same way a window pattern is if it contains a space) --
  `src/fs.c`. Disabled entirely until the server is started with at
  least one `AmiPilotServer ... FSROOT=<path> [FSROOT=<path> ...]`
  grant (`FSROOT/K/M`, so multiple roots may be given); every verb
  refuses a path outside every granted root with `RC 10` naming the
  granted list, and there is no way to grant a root after startup.
  Containment is checked by **lock identity**
  (`Lock()`/`ParentDir()`/`SameLock()`, all V36), never by string
  prefix matching -- Amiga assigns mean two different path strings can
  name the same or a nested location, so string comparison would miss
  genuine matches and genuine escapes alike. **Known limitation
  (TOCTOU, accepted):** `FSDELETE`, `FSGET`, and `FSMKDIR` verify
  containment via a lock, then release it and re-resolve the same path
  *string* for the actual `DeleteFile()`/`Open()`/`CreateDir()` --
  AmigaDOS (this project's V37 floor) has no relative-to-lock form of
  any of those calls. An assign or path component repointed in the gap
  between the check and the real operation could steer the operation
  outside the granted root; this narrows nothing further than the
  check itself and is a real, standing gap, not silently assumed closed
  (see the doc comment on `ResolveExisting()` in `src/fs.c`, matching
  `AmipIsWindowOpen()`'s own "shrinks the gap, doesn't close it
  entirely" precedent). `FSGET` returns the whole
  file as raw bytes (may contain embedded NULs; the wire's
  length-prefixed framing carries them intact) and is capped at the
  server's own internal buffer (`AMIP_FS_BUF_SIZE`, 16KB) -- this is a
  test-staging channel for small fixtures/config/log files, not a file
  manager, and `RC 20` on anything larger.

  **`FSPUT <path> <byte-count> [TIMEOUT=<n>]` (phase 1.0, host-to-Amiga
  writes) is real and wire-only.** It is the wire protocol's first
  request that carries a raw binary body -- see `WIRE.md`'s "Request
  payloads" section for the `<byte-count>`-then-raw-bytes framing this
  introduces, symmetric to how a *response* already carries one.
  **Not answerable over ARexx**: `RexxMsg`/`ARG0()` only ever carries
  string arguments, so there is genuinely no channel to receive a raw
  payload on that transport -- `FSPUT` issued via the ARexx port
  returns `RC 10` outright, an explicit, permanent asymmetry (stronger
  than `AUTH`'s own weaker one, which parses everywhere but only
  *gates* on TCP). `path`'s parent directory is containment-checked the
  same lock-identity way `FSMKDIR` checks its target (the file itself
  may not exist yet); creates or overwrites identically
  (`Open(path, MODE_NEWFILE)`), same `AMIP_FS_BUF_SIZE` (16KB) cap as
  `FSGET`, checked against the declared byte-count at parse time,
  before any attempt to read the payload. Once a valid (in-cap)
  byte-count has been parsed, the server unconditionally drains exactly
  that many raw bytes off the wire before replying -- even when the
  request will ultimately be rejected for another reason (`FSROOT`
  disabled, path outside the allowlist) -- because the client has
  already sent those bytes and not consuming them would desync the
  connection for the next request. `TIMEOUT=<n>` (seconds, default 30
  -- longer than most other waits here, since a real multi-KB payload
  over a slow serial link genuinely needs it) bounds how long the
  server waits for the declared payload to fully arrive; `RC 15`
  (`AMIP_AREXX_RC_TIMEOUT`) if it doesn't, distinct from `RC 20` on a
  write that fails after a complete payload arrived (disk full).
  Implemented per-transport (`AmipSerialReadExact()`/
  `AmipTcpReadExact()`, `src/serial.c`/`src/tcp.c`) since
  `AmipArexxParse()` itself has no read primitive of its own -- serial
  polls the existing async-read-plus-drain mechanism, TCP briefly makes
  the client socket non-blocking (`IoctlSocket(FIONBIO)`) and polls
  `recv()`; both at the same ~100ms tick `WAITFOR`'s own polling uses.

  Verified end-to-end by `make test-target`'s file API check
  (`tests/copperline/fs-test.py`): seeds a granted `RAM:` directory
  with a file via `S:User-Startup` before the server starts (`FSROOT`
  is a startup-time `Lock()`, so the root must already exist), then
  over the wire lists it, stats and reads the seeded file back byte-
  for-byte, creates and deletes a subdirectory, writes a new file via
  `FSPUT` and reads it back byte-for-byte, and confirms `FSLIST
  SYS:` -- outside the granted root -- is rejected rather than served.

- **Menus (phase 0.4, shipped in v0.4):** `MENU <window-pattern>` walks a
  window's live `struct Menu`/`struct MenuItem` chain
  (`intuition-model`'s `AmipWalkMenuStrip()`, `intuition-model/src/
  walk.c`) and returns every pulldown menu, its items, and (one level
  deep -- classic Intuition menus don't nest a second pull-right
  level) their submenu items, each with its checkit/checked/enabled
  state and keyboard shortcut if it has one. `MENUPICK
  <window-pattern> <menu-num> <item-num> [<sub-num>]` addresses an
  item by the same 0-based chain positions `MENU`'s own output
  reports (and what Intuition itself decodes an `IDCMP_MENUPICK`
  `Code` into via `MENUNUM()`/`ITEMNUM()`/`SUBNUM()`).

  **Selection is keyboard-shortcut only for now.** `AmipMenuPickByShortcut()`
  (`server/src/action.c`) activates the window, then strikes the
  item's `Command` byte (inverted through the live keymap via
  `MapANSI()`, same technique `AmipTypeString()` uses per character)
  with the right-Amiga qualifier held -- the same input.device path a
  human pressing Right-Amiga+key produces. Intuition resolves that
  combination against the window's own live menu strip on its own;
  this deliberately does not synthesize `IDCMP_MENUPICK` directly, so
  a successful `RC 0` is genuine evidence the pick reached the app
  through the real menu-shortcut path, not a shortcut around it.
  **Honest limit:** an item with no keyboard shortcut (`COMMSEQ`
  unset) can't be picked yet -- `RC 20` names this explicitly
  ("pointer-based menu selection isn't built yet") rather than
  silently failing or guessing a fallback. Pointer-based navigation
  (open the menu, move across items/submenus, release over the target
  -- `LayoutMenusA()` already precomputes every item's screen-absolute
  geometry before the menu is ever opened, so this is buildable) is
  real follow-up work, not invented here ahead of it. A disabled item
  (`ITEMENABLED` unset) is rejected client-side, before any keystroke
  is sent at all -- `RC 20`, distinct from the no-shortcut case.

  Verified end-to-end by `make test-target`'s MENU/MENUPICK check
  (`tests/copperline/menu-test.py`) against a menu strip added to
  `fixtures/gadtools-app` for this purpose: walks the menu and asserts
  every field the walker read live (including a `CHECKIT|MENUTOGGLE`
  item's starting `checked` state and a separator bar's blank,
  disabled entry), `MENUPICK`s a top-level item and a submenu item by
  their shortcuts and confirms each pick genuinely reached the
  fixture's own `IDCMP_MENUPICK` handler (which writes a distinct
  marker into its Host string gadget, read back via the
  already-verified `GETTEXT` path -- proof of real delivery through
  Intuition, not just that a keystroke was injected), and confirms the
  fixture's permanently-disabled item is rejected without ever sending
  a keystroke.

- **Tier-2 semantic locators (phase 0.4, shipped in v0.4):** `CLICK`/`TYPE`/
  `GETTEXT`'s classic form accepts a `ROLE=<role>`/`LABEL=<substring>`/
  `INDEX=<n>` locator in place of the bare numeric `<gadget-id>` --
  `docs/implementation-plan.md`'s "Locator tiers" section, tier 2
  ("gadget by role + label text, or by position-in-set"). `ROLE=`
  matches `intuition-model`'s `AmipRoleName()` vocabulary
  (`"button"`, `"string"`, `"slider"`, etc., case-insensitively --
  `AmipRoleFromName()` is the reverse lookup); `LABEL=` is a
  case-**sensitive** substring match (`strstr`), the same convention
  window/screen patterns already use, not a new inconsistent
  behavior; `INDEX=` (0-based, default the first match) disambiguates
  when more than one gadget matches. At least one of `ROLE=`/`LABEL=`
  must be given -- a bare digit is always the original numeric form,
  unchanged. Resolved server-side (`ResolveTargetGadget()`,
  `amipilotserver/main.c`) against a fresh walk of the live window
  (the same `AmipWalkWindow()` call `TREE`/`GETTEXT` already make, so
  no new classification logic), never a cached or stale model. No
  match is `RC 5`, same class an unmatched numeric ID already uses.
  **Honest limit:** proximity-to-a-label matching (the plan's third
  tier-2 locator style) isn't built -- `ROLE=`/`LABEL=`/`INDEX=` is
  the complete locator vocabulary today, not a partial step toward a
  fuzzier heuristic.

  `fixtures/gadtools-app` carries a second `BUTTON_KIND` gadget
  (Connect, Cancel) specifically so `INDEX=` has a real same-role
  pair to disambiguate, not just a single unambiguous instance
  `ROLE=`/`LABEL=` alone could already find.

- **Drag (phase 0.4, shipped in v0.4):** `DRAG <window-pattern> <locator>
  <dx> <dy>` -- a genuine press/move/release drag of the target
  gadget's current center by a pixel offset, the natural shape for
  adjusting a slider/scroller (GadTools `SLIDER_KIND`/`PROP_KIND`,
  which is inherently a delta operation). `DRAG <window-pattern>
  <locator> TO <dest-gadget-id>` (or `TO @<dest-name>`) is the
  gadget-to-gadget form instead: drags the source onto a second
  gadget's center, both resolved live at action time, zero
  coordinates in the caller's script -- for drag-and-drop/reorder
  cases (e.g. dragging one listview item onto another). The
  destination is always resolved against the SAME window as the
  source; a `TO @<dest-name>` whose own manifest entry names a
  different window is rejected explicitly (`RC 10`), not silently
  dragged into the wrong window. The source locator is exactly
  CLICK/TYPE/GETTEXT's (numeric `GA_ID`, tier-2 `ROLE=`/`LABEL=`/
  `INDEX=`, or `@name`) -- DRAG gets tier-2 source locators for free
  from the same parser path. No `ROLE=`/`LABEL=` form for the
  destination, keeping this verb's scope contained.

  `AmipDragAt()` (`server/src/action.c`) is the raw primitive: a real
  `IECLASS_RAWMOUSE` button-down, an absolute `IEPointerPixel` jump to
  the destination, a real button-up -- built on the same
  `AmipMoveMouseTo()`/button-injection code `AmipClickAt()` already
  uses (`AmipGadgetCenter()`, refactored out of `AmipClickGadget()`,
  is the shared geometry-resolution step both now call). **Honest
  limit:** the move between press and release is a single absolute
  jump, not synthesized continuous motion -- sufficient for
  Intuition's own built-in prop-gadget/window-drag tracking (which
  watches ordinary mouse-move events regardless of how many arrive),
  but an application with its OWN per-frame-delta-sensitive drag
  handling could in principle behave differently than under a real
  human drag across every intervening pixel. Narrows to "the
  endpoints are always genuine input.device events reaching the real
  event path," not full motion-stream fidelity.

  Verified live against a real `AmiPilotServer` (Amiberry, not just
  build/lint): a `SLIDER_KIND` gadget added to `fixtures/gadtools-app`
  for this purpose (range 0..100, starts at 0) whose `IDCMP_GADGETUP`
  handler writes the live level GadTools itself reports (the event's
  own `Code` field, its documented contract for `SLIDER_KIND`/
  `PROP_KIND`) into the Host string gadget -- the same observable-
  marker technique the Cancel button/menu items already use, proving
  a `DRAG` genuinely reached GadTools' real slider-tracking code, not
  just that input.device events were injected.

- **Wait/expectation primitives (phase 0.5, shipped in v0.5):**
  `WAITFOR [SCREEN=<s>] WINDOW=<pattern> [TIMEOUT=<n>]` polls until a
  window matching `<pattern>` appears; `WAITFOR [SCREEN=<s>]
  NOWINDOW=<pattern> [TIMEOUT=<n>]` polls until none does. `TIMEOUT`
  defaults to 10 seconds; a condition that never becomes true is
  `RC 15` (`AMIP_AREXX_RC_TIMEOUT`), a new RC distinct from `RC 5`
  (nothing matched at all) and `RC 20` (an action's own injection
  failed) -- "the condition never became true in time" is a genuinely
  different outcome a test author may want to handle differently from
  either.

  `CLICK` additionally accepts a trailing `EXPECT=WINDOW=<pattern>` or
  bare `EXPECT=NOWINDOW` (no argument) plus `[TIMEOUT=<n>]`, composing
  the click atomically with a server-side wait -- one wire round trip,
  not an action followed by separate host-side polling. The two
  `NOWINDOW` forms are deliberately asymmetric: `CLICK`'s bare
  `EXPECT=NOWINDOW` means "the window this click itself just resolved
  and acted on has closed," checked by pointer identity via the
  existing `AmipIsWindowOpen()` (the exact `struct Window *` the click
  already has in hand, free) -- not a pattern re-search, and therefore
  precise even if a different, similarly-titled window happens to
  exist afterward. `WAITFOR`'s own `NOWINDOW=<pattern>` has no prior
  action to anchor an identity to, so it's necessarily a fresh
  `AmipFindWindow()` pattern search each poll -- an honest, slightly
  weaker guarantee, not a bug. The click itself always happens
  regardless of `EXPECT=`; a timeout waiting for the expected effect
  is `RC 15`, reported distinctly from the click's own injection
  failing outright (`RC 20`, unchanged).

  Polling is entirely server-side (`~100ms` between checks, blocking
  that one request/response for up to `TIMEOUT` seconds -- safe here
  since `HandleCommand()`'s dispatch is strictly single-threaded and
  TCP's own "one active client at a time" model means there's no
  second connection to starve), which is the actual point: this closes
  the classic click-then-check race a host-side poll loop can still
  lose on a fast transition, not just a convenience wrapper around one.

  `WAITFOR` also takes a third condition form for gadget state:
  `WAITFOR [SCREEN=<s>] <window-pattern> (<gadget-id> | ROLE=<r>
  [LABEL=<l>] [INDEX=<n>]) TEXT=<value> [TIMEOUT=<n>]` (or `WAITFOR
  @<name> TEXT=<value>`) polls until a gadget's text (`GETTEXT`'s own
  value-or-label convention) EXACTLY equals `<value>`. This falls
  through into the exact same window-pattern-or-`@name` + gadget-
  locator parsing `CLICK`/`TYPE`/`GETTEXT`/`DRAG` already share, rather
  than a second copy of it, and reuses `GETTEXT`'s own text-reading
  logic (`FindGadgetText()`) for the comparison -- no new reading
  path, just a poll loop around the existing one. `TEXT=` matching is
  exact, not substring: a partial match could be satisfied by an
  intermediate, not-yet-final state (e.g. matching both "loading..."
  and "loaded"), a subtler footgun for a wait condition specifically.

  **Scope for this pass:** `WINDOW=`/`NOWINDOW=`/`TEXT=`/`REQUESTER`
  conditions, and only `CLICK` composes with `EXPECT=` (`WINDOW=`/
  `NOWINDOW=` only, not `TEXT=`/`REQUESTER`) -- `TYPE`/`DRAG`/
  `MENUPICK` callers, and anyone wanting a `TEXT=`/`REQUESTER` wait,
  use a separate `WAITFOR` call instead.
  `CLICK`'s own `EXPECT=` doesn't get a `TEXT=` form: the gadget whose
  text changes as a result of a click is often a DIFFERENT gadget than
  the one clicked (e.g. a status label), which would need a second,
  independent locator embedded inside `EXPECT=` -- real added
  complexity, deferred rather than silently built partway.

  `fixtures/gadtools-app` carries a button (`Open Req`) whose
  `IDCMP_GADGETUP` handler doesn't open a second window until a full
  two seconds after receiving the event -- a genuine, measurable race
  (not a hypothetical) that an immediate post-click check reliably
  misses, that `EXPECT=`/`WAITFOR` reliably don't, and that a
  deliberately-too-short `TIMEOUT=` reliably reports as `RC 15`
  (confirmed live, not just theoretically distinguishable).

  `WAITFOR` also takes a fourth condition form, `REQUESTER`: `WAITFOR
  [SCREEN=<s>] REQUESTER [TIMEOUT=<n>]` polls until a genuine Intuition
  Requester appears anywhere (optionally narrowed to a screen) -- GitHub
  issue #52's detection-only "cheap first step", no way yet to address
  or click a Requester's own gadgets. No pattern argument, since a
  Requester generally has no title of its own.

  Window-attached Requesters only, by design -- a system-wide Requester
  with no owning window (a disk-swap prompt, a Guru) is a genuinely
  harder detection problem `BuildSysRequest()`'s own autodoc leaves
  open. Getting even this "cheap" slice right, confirmed live against
  `fixtures/gadtools-app`'s own `Ask` button (a real `AutoRequest()`
  call) and `tests/copperline/run.sh`'s `run_requester_check`, found
  TWO genuine surprises the original design (and `BuildSysRequest()`'s
  own 1990s autodoc) got wrong for this project's target OS/ROM:
  neither a non-NULL `window->FirstRequest` (the classic
  `Request()`-based struct `Requester` chain) NOR the `GTYP_REQGADGET`
  bit the autodoc documents on the Positive/Negative gadgets is
  actually set by `AutoRequest()`/`BuildSysRequest()`/`EasyRequest()`
  when given a real owning window -- `BuildSysRequest()`'s own autodoc
  is explicit elsewhere that "a new window is opened in the same
  screen as the one containing your window", and that new window turns
  out to be given its owner's EXACT title text, which no ordinary
  well-behaved app window shares with another on its own. Detection
  uses that title collision as the real signal (`FirstRequest` is
  still checked first, both because it's free and for a possible
  future genuine `Request()`-based interaction path). Full story in
  `WaitForRequesterPresent()`'s own comment,
  `server/src/amipilotserver/main.c`.

- **MUI-ARexx bridge tier (phase 0.5, shipped in v0.5):** `MUIREXX <app-base> [TIMEOUT=<n>]
  <command...>` sends `<command>` verbatim to the ARexx port of the MUI
  application whose `MUIA_Application_Base` is `<app-base>` (e.g.
  `MUIREXX MUIDEMO info title`), exactly as an ARexx script's own
  `ADDRESS` would, and reports back what that application's own command
  handler replied with. Genuinely different from `CLICK`/`TYPE`/
  `GETTEXT`: no structural walk, no `input.device` synthesis -- MUI
  internals are deliberately opaque to external walkers (`intuition-model`
  sees a MUI window's top-level object and nothing below it, the same
  limit `window.class`/`layout.gadget` already has), so this tier drives
  through the port every MUI app carries automatically instead.

  Port resolution tries `<app-base>` verbatim first, then `<app-base>.1`
  -- both are real, observed naming conventions (MUI's own dev docs
  document the `.N` slot convention explicitly, the same one this
  server's own port uses; a shipped MUI example macro addresses its
  target by the bare base name with no suffix at all). `server/src/
  muirexx.c` sends a genuine `RexxMsg` (`CreateRexxMsg`/`FillRexxMsg`/
  `PutMsg`) and polls for the reply the same `~100ms`-tick,
  `Delay()`-based way `WAITFOR`'s own polling does -- not a signal wait,
  so a target that never replies times out on schedule (`RC 15`)
  instead of hanging the whole server.

  **Confirmed live against a real MUI application** (AmigaOS 3.2's own
  `MUI:Demos/MUI-Demo`) rather than assumed from docs: MUI's BUILT-IN
  ARexx support is a small, universal, seven-command set (`quit`/`hide`/
  `show`/`activate`/`deactivate`/`info <item>`/`help [file]`) -- there is
  no generic "read/write this widget's value" command the way `GA_ID` or
  tier-2 `ROLE=`/`LABEL=`/`INDEX=` are for classic gadgets. `MUI-Demo`
  itself registers zero application-specific commands (confirmed via its
  own `help` output). So `MUIREXX` is an honest passthrough, not a
  CLICK/TYPE-shaped verb built on a false promise of generic MUI widget
  access: anything beyond the universal seven is entirely up to the
  target application having registered its own commands
  (`MUIA_Application_Commands`), and this bridge relays whatever you send
  it unchanged rather than inventing capability an app doesn't have.
  `quit` -- the one command every MUI app answers -- is genuinely useful
  on its own for the same "exit via the app's own affordances" teardown
  discipline the rest of this project follows; confirmed live that it
  really closes the target's window, not just that the wire round trip
  succeeds.

  The target's own reply code (an arbitrary, app-defined value -- this
  bridge doesn't reinterpret it) is relayed as `RC 10`
  (`AMIP_AREXX_RC_ERROR`) with the numeric code prefixed onto the result
  text when nonzero, distinct from `RC 5` (no ARexx port found for that
  base -- tried both naming conventions), `RC 15` (sent, but no reply
  within `TIMEOUT`), and `RC 20` (this server's own `RexxMsg` allocation
  failed -- out of memory, not the target application's fault).

- **Screens (phase 0.4, shipped in v0.4):** `SCREENS` (no arguments) lists
  every open screen -- title, position, size, and whether it's
  frontmost. Every window-targeting verb (`TREE`/`CLICK`/`TYPE`/
  `GETTEXT`/`MENU`/`MENUPICK`'s classic form, not `@name`) additionally
  accepts an optional leading `SCREEN=<substring>` token before the
  window pattern -- same `KEYWORD=value` idiom `LAUNCH`'s own
  `STACK=<n>` already established -- narrowing the window search
  (`AmipFindWindow()`, `server/src/action.c`) to screens matching that
  substring; omitted, the search covers every screen exactly as before
  this existed. This is for disambiguating two same-titled windows on
  different screens, not for making cross-screen targeting possible in
  the first place -- `AmipFindWindow()` already searched every screen
  before this feature existed, it just couldn't tell two matches on
  different screens apart. `TREE`/`MENU`'s window header line also
  gains a `screen="<title>"` field for the same reason, so a script
  can confirm which screen a match actually landed on.

  **Identity is `Screen->DefaultTitle`, deliberately not the live
  `Title` field** -- verified against `intuition.doc`, not assumed:
  `SetWindowTitles()`'s own doc says the screen's title-bar text
  "appears ... whenever this window is the active one", i.e. `Title`
  tracks whichever window is currently active on that screen (via that
  window's own `WA_ScreenTitle`), not a fixed name for the screen
  itself, and so isn't a safe thing to match or report as identity.
  `DefaultTitle` is the app's own name for the screen, set once at
  open time -- the classic `NewScreen.DefaultTitle` field and the V36+
  `SA_Title` tag are the *same* field (the autodoc says outright "[For
  V36: superseded by SA_Title]"), so there's no behavioral difference
  between a screen opened the old way and a tag-opened one here.

  **Honest limit, not silently glossed over:** `SCREEN=` and the new
  `screen="..."` field only apply to the classic `<window-pattern>`
  form -- the manifest contract and `@name` locators have no
  screen-awareness yet, since manifests don't record which screen a
  window belongs to. There's also no standalone "bring this screen to
  front" verb: it's unnecessary, since `CLICK`/`TYPE`/`MENUPICK`
  already call `ScreenToFront()`/`WindowToFront()`/`ActivateWindow()`
  on the target window's own screen as a side effect of acting (this
  predates the `SCREENS`/`SCREEN=` work -- see `AmipClickGadget()` and
  `AmipMenuPickByShortcut()`), and read-only verbs (`TREE`/`GETTEXT`/
  `MENU`) deliberately don't move anything forward.

  Also new: `Amipilot.wait_for_window()`/`wait_for_screen()`
  (`host/amipilot/client.py`) -- host-side polling helpers for "did my
  window/screen show up yet" (an asynchronously `launch()`ed program
  opening its own screen is the normal case, not an edge case), same
  idea `LAUNCH`'s own docstring already recommended by hand
  (`tests/copperline/launch-test.py`'s poll loop), now reusable rather
  than re-written per caller. A server-side blocking wait verb was
  considered and rejected: `AmiPilotServer`'s dispatch is
  single-threaded and synchronous (see `main.c`'s own comment on why
  `g_resultBuf`/`g_treeBuf` are safe as file-statics), so a verb that
  blocks server-side for a timeout would stall the *entire* server --
  ARexx port and wire both -- for that whole duration.

  Verified end-to-end by `make test-target`'s multi-screen check
  (`tests/copperline/screens-test.py`), against a new fixture
  (`fixtures/second-screen-app`) that opens its own custom screen: with
  `gadtools-app`'s window pushed to a now-background Workbench screen
  by the second screen opening on top of it, `SCREENS` correctly
  reports both screens and which is frontmost (reached via
  `wait_for_screen()`, exercising that helper live rather than only
  against a fake clock), a loose pattern matching both fixtures'
  windows is disambiguated by `SCREEN=`, and -- the check that actually
  retires the risk this feature exists to address -- `TYPE`/`GETTEXT`/
  `CLICK` against the window on the background screen still succeed,
  proving `ScreenToFront()` genuinely brings a non-frontmost screen
  forward correctly, not just in the single-screen case every prior
  on-target check has ever exercised.

- **Cooperative geometry port (issue #49, not yet released):** `WHERE @<name> [TIMEOUT=<n>]`
  is the diagnostic form of the escape hatch for gadgets structural
  walking can never reach -- `layout.gadget` children on classic OS
  3.x (the project's own documented "Confirmed limit"; see
  `docs/implementation-plan.md`'s "future tier between 1 and 2" design
  note, now implemented). A manifest gadget declared with
  `WHEREGADGET` (format version 2, `manifest/SPEC.md`) instead of
  `GADGET` has no `GA_ID` at all; resolving its name queries the
  manifest's declared `WHEREPORT` -- a small, optional ARexx port the
  *application itself* exposes, answering `WHERE <name>` with its own
  live `GetAttr(GA_Left/GA_Top/GA_Width/GA_Height)` geometry (window-
  relative, including the border/title-bar area -- the exact
  convention `AmipGadgetCenter()` already uses for ordinary gadgets,
  `server/src/action.c`). `WHERE`'s own RESULT is the raw
  `"<x> <y> <w> <h>"` text; it exists mainly as a test/debugging probe
  -- `CLICK @name`/`TYPE @name` route through the identical query
  automatically whenever the manifest resolves the name to a
  `WHEREGADGET`, then act with a genuine `input.device` click via the
  new `AmipClickWindowRelative()` (`server/src/action.c`), the same
  real-input path every other locator tier uses. **Discovery is
  cooperative; actuation stays real input** -- unlike `MUIREXX`, where
  the target's own port does the acting too. No coordinates ever
  appear in a script: they're resolved live, at action time, so
  relayout and font changes can't break anything, the same immunity a
  plain manifest already gives `GA_ID`-addressed gadgets.

  `GETTEXT`/`DRAG` have no `WHERE`-based path in this cut -- a
  `WHEREGADGET` name given to either is `RC 10` with an explicit
  "geometry only" message, a stated limit rather than a silent
  fallback to nothing. `server/include/where.h`/`server/src/where.c`
  hold the query primitive (`AmipWhereQuery()`); it deliberately does
  **not** share `muirexx.c`'s `AmipMuiRexxSend()`, even though the
  underlying `RexxMsg` send/poll mechanics are the same shape --
  `WHEREPORT` name resolution is exact-match only, with no `<name>.1`
  fallback the way `MUIREXX`'s MUI-specific port-slot convention gets
  (`manifest/SPEC.md`'s "Clash guard" section explains why: a
  `WHEREPORT` that doesn't exist under the exact declared spelling
  must fail loudly, not silently probe a related name that happens to
  exist for an unrelated reason).

  RC mapping: port not found → `RC 5`; the target's own reply RC
  nonzero (unknown name) → `RC 10`; no reply within `TIMEOUT` → `RC
  15`; a reply that doesn't parse as exactly four decimal integers →
  `RC 10` ("malformed WHERE reply", the *application's* WHERE
  implementation being at fault, not this bridge); this server's own
  `RexxMsg` allocation failing → `RC 20`.

  **Confirmed live** against `fixtures/classact-app`'s own new
  `CAAPP.WHERE` port (`tests/copperline/where-test.py`,
  `run_where_check` in `tests/copperline/run.sh`): all three of that
  fixture's gadgets (button/string/checkbox, all `layout.gadget`
  children, all invisible to the walker -- `CAApp.golden` is
  unchanged, proving it) are addressed purely via `WHEREGADGET`;
  `WHERE @connect_button` returns geometry that lands inside the
  window's own reported bounds; an unknown name and a `GETTEXT` on a
  `WHEREGADGET` both reject cleanly; `TYPE @host_field` genuinely
  lands text in the layout child's own string gadget (confirmed via
  the fixture's own log line, since `GETTEXT` can't read it back --
  see `where-test.py`'s header for why); and `CLICK @connect_button`
  reaches and presses the real button, confirmed the same way every
  other teardown check in this suite is -- `EXPECT=NOWINDOW` catching
  the window actually closing.

  **A real bug found building this** (2026-08-09): a `RexxMsg`
  constructed by hand via `CreateRexxMsg()`/`FillRexxMsg()`/`PutMsg()`
  -- the same recipe `MUIREXX`'s own `AmipMuiRexxSend()` already uses
  -- arrives at the receiver with its node type left at `NT_MESSAGE`,
  not `NT_REPLYMSG`. `rexxsyslib.library`'s own `IsRexxMsg()` reports
  such a message as not a `RexxMsg` at all, even though `rm_Action`
  correctly carries `RXCOMM` and `ARG0()` reads back the real command
  text -- confirmed by direct inspection (dumping `ln_Type`/
  `rm_Action` from the fixture's own receiving side) that a message
  built exactly this way is structurally sound in every way
  `IsRexxMsg()` doesn't check. A real ARexx interpreter's own
  outgoing command messages (a genuine `rx` script's `ADDRESS`)
  apparently arrive already marked `NT_REPLYMSG` through some internal
  `rexxsyslib` mechanism this project's own hand-built sends don't
  reproduce -- `IsRexxMsg()` seems intended for a *sender* validating
  its own reply, not a receiver validating an incoming command, and
  `server/src/arexx.c`'s own receiver-side use of it only ever works
  because its senders are real ARexx scripts, never this project's own
  bridges. Fixed by having `CAAPP.WHERE` -- a port dedicated solely to
  this one protocol, per the "Clash guard" convention above -- trust
  every message that arrives on it rather than gating on `IsRexxMsg()`
  first; see the doc comment on `HandleWhereMessage()` in
  `fixtures/classact-app/src/main.c` for the full account.
  `MUIREXX`'s own `AmipMuiRexxSend()` has this same latent gap, masked
  there only because MUI-Demo's built-in ARexx handling never calls
  `IsRexxMsg()` on what it receives either -- not fixed here, since
  MUIREXX genuinely works against every real target tried and isn't
  this issue's scope, but worth knowing if a future MUIREXX target
  ever does check.

## Phase 0.2 (shipped)

- `src/action.c` + `include/action_engine.h` -- the action engine's
  first real verbs: absolute pointer positioning (documented
  `IECLASS_NEWPOINTERPOS`/`IESUBCLASS_PIXEL` mechanism) and genuine
  button click synthesis through `input.device`. Verified end-to-end
  under Copperline by `make test-target`'s action-engine click check:
  the synthetic click really presses `fixtures/gadtools-app`'s Connect
  button. Read the comments in `action.c` before changing event fields
  -- `IEQUALIFIER_RELATIVEMOUSE` and the gadget-coordinate convention
  were both hard-won.
- `src/clicktest/` (`AmiClickTest`) and `src/setmouse/` (`AmiSetMouse`)
  -- dev-only Shell diagnostics, not deliverables: end-to-end click
  proof and isolated pointer-positioning verification respectively.
  Built by `make server`, deliberately not part of `make build`.
- **`src/amipilotserver/` (`AmiPilotServer`) -- the actual phase 0.2
  deliverable.** A commodity hosting the action engine and
  `intuition-model` behind a genuine public ARexx port
  (`"AMIPILOT.<n>"`), plus `src/arexx.c`/`arexx_cmd.c` (the RexxMsg glue
  and portable command parser, split the same way `../amiauth`'s
  `arexx.c`/`arexx_cmd.c` are). Verb set: `TREE`/`CLICK`/`TYPE`/
  `GETTEXT`/`MANIFEST`/`QUIT` -- a small, real subset of the plan's
  full v1 verb list (wire protocol, launch, fs, menu/drag verbs are
  0.3/0.4 scope). `src/manifest.c` (portable, like `arexx_cmd.c`)
  parses the manifest contract (`../manifest/SPEC.md`) behind the
  `MANIFEST` verb and the `@<logical-name>` locator form.
  Verified against a real resident `RexxMast` (`rx`, not a hand-rolled
  RexxMsg -- `rexxsyslib.library`'s `IsRexxMsg()` only validates
  messages whose `rm_TaskBlock` came from a live ARexx task) by
  `make test-target`'s ARexx-port check: `tests/copperline/arexx-test.rexx`
  types into `fixtures/gadtools-app`'s Host string gadget, reads the
  value back over the port, then clicks Connect and confirms the window
  is gone -- state changed, driven and observed entirely through ARexx,
  no host involved. This is the phase 0.2 release gate.

  Kept under `server` rather than `amiga`/`build` for now because phase
  0.2 isn't tagged yet, not because it's a throwaway tool like
  `AmiClickTest`/`AmiSetMouse` above.

  **Stack note, hard-won (2026-08-05):** the commodity's TREE/GETTEXT
  result buffers (`treeBuf`/`resultBuf`, ~4.5KB combined) are `static`,
  not stack-allocated. A Shell-launched process's default AmigaDOS stack
  is small; as locals they silently overflowed it, corrupting state such
  that the *first* ARexx command handled fine but the process took an
  illegal-instruction exception before the second could be processed --
  `rx` then reported a confusing "Host environment not found" for every
  later command, since the crashed task was never servicing the port
  again. Nothing about that symptom pointed at a stack overflow; don't
  reintroduce large stack-allocated locals in the per-message dispatch
  loop without moving them back to `static` (or `AllocVec`).
