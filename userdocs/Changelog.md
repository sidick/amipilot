# Changelog

AmiPilot is developed phase by phase; each one is validated on-target
under Copperline against real AmigaOS before being considered done. This
page summarizes what landed in each release, in user-facing terms — see
the repository's
[`docs/implementation-plan.md`](https://github.com/sidick/amipilot/blob/main/docs/implementation-plan.md)
for the full engineering detail and phase sequencing behind each one.

## Unreleased

- **`CLICK` can now dismiss a window-owned Requester** (issue #52
  follow-up): a genuine `AutoRequest()`/`BuildSysRequest()`/
  `EasyRequest()` requester with a real owning window turns out to
  open as a completely ordinary, separate `struct Window` (sharing its
  owner's exact title text) rather than attaching invisibly to
  `FirstRequest` the way the issue's original design sketch assumed —
  so ordinary `CLICK <window-pattern> <gadget-id>` already reaches its
  Yes/No gadgets today, no new locator or wire change needed.
  `BuildSysRequest()`'s own autodoc documents a fixed, app-independent
  `GadgetID` convention (`TRUE`=1 for the positive choice, `FALSE`=0
  for the negative), confirmed live: `CLICK <same-pattern> 1`
  genuinely dismisses the requester, verified by `WAITFOR REQUESTER`
  correctly timing out again afterward. One real limit remains,
  genuinely open: a system-wide requester with no owning window (a
  disk-swap prompt, a Guru) is still detection-only, since it opens
  with no known title to pattern-match against at all. Confirmed
  separately (2026-08-09) that `GETTEXT` reading a requester's own body
  text is a permanent limit, not an open gap: a live dump of every open
  window's `FirstRequest` while a real `AutoRequest()` was up showed it
  NULL on both the owning window and the requester's own window — no
  `struct Requester` exists anywhere reachable here to read `ReqText`
  off of on this target's real OS/ROM.
- **BOOPSI/ReAction role classification for 12 WB3.2-era gadget
  classes** (issue #69): `clicktab.gadget`, `colorwheel.gadget`,
  `datebrowser.gadget`, `fuelgauge.gadget`, `getcolor.gadget`,
  `getfile.gadget`, `getfont.gadget`, `getscreenmode.gadget`,
  `gradientslider.gadget`, `palette.gadget`, `sketchboard.gadget`,
  `speedbar.gadget`, and `texteditor.gadget` — previously all
  `role=custom` — now get a real, AT-SPI-style role
  (`page_tab_list`/`color_wheel`/`calendar`/`progress_bar`/
  `color_chooser`/`file_chooser`/`font_chooser`/`screenmode_chooser`/
  `slider`/`palette`/`canvas`/`toolbar`/`text_editor`), addressable via
  tier-2 `ROLE=` locators. `speedbar.gadget` turned up a real,
  easy-to-guess-wrong exception: its registered class name is literally
  `"speedbar"`, not `"speedbar.gadget"` like every other class here —
  found live, not from documentation, which follows the
  `"name.gadget"` pattern uniformly. A new fixture
  (`fixtures/reaction-classes-app`) exercises one instance of each
  class directly (deliberately not nested inside `window.class`/
  `layout.gadget`, which would make them unreachable the same way
  issue #49 already documents), with a checked-in golden-tree file
  locking in the live-confirmed output. `space.gadget`,
  `virtual.gadget`, `listview.gadget`, `tabs.gadget`, and
  `tapedeck.gadget` were deliberately left unclassified — see
  [Locator Tiers and Limits](Locator-Tiers-and-Limits.md) for why each
  one is an honest gap rather than an oversight.
- **`WHERE`, the cooperative geometry port** (issue #49): the honest
  escape hatch for gadgets nested inside a `window.class` window's
  `layout.gadget` — permanently invisible to structural walking on
  classic AmigaOS 3.x, so no plain manifest entry could ever name
  them. An application implementing this exposes a small, optional
  ARexx port answering `WHERE <name>` with a gadget's own live
  geometry (it already holds the object pointer for its own event
  dispatch); a format-version-2 manifest names such a gadget with
  `WHEREGADGET` instead of `GADGET`. `CLICK`/`TYPE @name` then work
  exactly as they would for any other manifest name — discovery is
  cooperative, but the click itself is still genuine `input.device`
  input, unlike `MUIREXX`, where the target's own port does the
  acting too. Verified end to end against `fixtures/classact-app`'s
  own new `CAAPP.WHERE` port, whose three gadgets are now addressed
  entirely via `WHEREGADGET` (its manifest previously, deliberately,
  named none at all). See [ARexx Reference](ARexx-Reference.md#driving-layoutgadget-only-applications).
- **`MENUPICK`'s pointer-based fallback** (issue #63): a menu item
  with no keyboard shortcut used to be rejected outright — now it's
  picked via a genuine synthesized right-mouse-button-down/move/move/
  release sequence instead, chosen automatically whenever an item has
  no shortcut, the same "real input.device events, not a shortcut"
  principle every other verb already follows. Verified end to end
  against two new shortcut-less items on `fixtures/gadtools-app`'s own
  menu strip — a top-level item and a one-level-deep submenu item.
- **`STRING_KIND` vs `INTEGER_KIND` classification** (issue #64): both
  GadTools kinds create the same underlying `GTYP_STRGADGET`, so
  nothing in the raw gadget structure told them apart — a smaller
  version of the `BUTTON_KIND`/`CHECKBOX_KIND` problem, solved the same
  way. `GT_GetGadgetAttrsA`'s documented per-kind tag table lists
  `GTIN_Number` under `INTEGER_KIND` only; asking a plain string gadget
  for it is a safe, documented no-op, the discriminator rather than a
  guess. Integer gadgets now report `role=integer`. Verified against a
  new Count `INTEGER_KIND` gadget on `fixtures/gadtools-app`'s own
  window.

## v1.0 — 2026-08-09

The first full release: everything the implementation plan's 1.0 gate
asked for — getting files and programs *onto* the machine the way a
real user would, seeing what's actually on screen, and manipulating
whole windows — verified end to end, including on a completely bare
machine profile and against real Picasso96/RTG.

- **`FSPUT`**: push a file from the host onto the Amiga, completing
  the file API's round trip (`FSGET` could already read one back).
  The wire's first request to carry a raw binary body — and
  deliberately wire-only, with no ARexx form at all, since ARexx
  messages can only ever carry string arguments (a real, permanent
  transport asymmetry, stated rather than papered over). See
  [Wire Protocol](Wire-Protocol.md#file-api).
- **`WBLAUNCH`**: launch a program the way Workbench itself does — a
  genuine `WBStartup`/`WBArg` message to a real non-CLI process, the
  same technique real launcher utilities use — with `TOOLTYPE=`
  overrides (merged into a scratch copy of the icon in `T:`, never
  the application's own `.info` file, because a Workbench-started
  program reads its tooltypes back off disk — there is no in-memory
  channel) and `ARG=` project-file arguments. `LAUNCH` (0.4) covers
  the Shell-start case; this covers the other half of how real
  software actually starts. See
  [Wire Protocol](Wire-Protocol.md#wblaunch).
- **`SCREENSHOT`**: capture a screen (or one window's region of it)
  as raw pixels — classic planar screens *and* genuine
  Picasso96/RTG bitmaps in their native pixel formats — with all
  image-format work (PNG, IFF ILBM, P96 pixel-format decoding,
  including the documented 16-bit `PC`-suffix byte-order pitfall)
  done host-side, stdlib-only. The P96 path is verified against real
  Picasso96 under emulation, both CLUT and truecolor, not just
  compiled. Inspector tooling, not a locator mechanism. See
  [Wire Protocol](Wire-Protocol.md#screenshot).
- **`WINDOWMOVE`/`WINDOWSIZE`**: move or resize a whole window via
  the same genuinely synthesized press/move/release drags gadgets
  already get, anchored on the window's own title bar or sizing
  gadget. See [Wire Protocol](Wire-Protocol.md#window-move-and-resize).
- **`WAITFOR REQUESTER`**: wait for a genuine Intuition Requester to
  appear — the detection-only first slice of requester support
  (window-attached requesters only; addressing or clicking a
  requester's own gadgets is stated, open follow-up work, not
  quietly missing). See
  [ARexx Reference](ARexx-Reference.md#waitexpectation-primitives).
- **Fixed**: `CLICK` with a `ROLE=`/`INDEX=` locator could silently
  act on the wrong *system* gadget (close/depth/drag) instead of the
  application gadget the locator actually matched.
- **Verification hardened for 1.0**: the on-target suite now includes
  the implementation plan's own "bare machine" lifecycle check (a
  stock boot with nothing pre-staged), an automated TCP-transport
  check (real `bsdsocket.library` over Copperline's host networking,
  not just serial), and an automated P96 `SCREENSHOT` capture-path
  check — plus a dedicated pre-1.0 code review whose findings were
  all fixed before this release.
- **Protocol verbs promoted to stable**: the implementation plan's
  own 1.0 gate — every verb the `VERSION` handshake reports is now
  `STABLE` (won't break within a major), not `EXPERIMENTAL`. See
  [Wire Protocol](Wire-Protocol.md#talking-to-it).

Known gaps, stated plainly: requester support is detection-only;
menu selection still needs a keyboard shortcut (pointer-based
selection for shortcut-less items isn't built); a `window.class`
window's `layout.gadget`-nested children remain unreachable on
classic OS 3.x (a documented platform limit, not a bug — see
[Locator Tiers and Limits](Locator-Tiers-and-Limits.md)); and TCP
remains LAN-only trust — no TLS, even though every verb is now
stable, see [Securing TCP](Wire-Protocol.md#securing-tcp).

## v0.5 — 2026-08-08

Reliability and reach into the wider ecosystem: waiting on real
conditions instead of guessed sleeps, community-authored coverage for
apps you don't control, structural regression fixtures, verification
against genuine stock AmigaOS and MUI software (not just purpose-built
fixtures), and a real bridge into MUI's own ARexx-driven applications.

- **Wait/expectation primitives** (`WAITFOR`, and `CLICK`'s trailing
  `EXPECT=`): closes the classic click-then-check race by polling
  server-side, in the same request, instead of a host-side loop.
  `WAITFOR WINDOW=<pattern>` / `NOWINDOW=<pattern>` wait for a window
  to appear or close; `CLICK`'s own `EXPECT=WINDOW=<pattern>` /
  `EXPECT=NOWINDOW` compose the click atomically with the wait,
  `EXPECT=NOWINDOW` checked by identity against the exact window the
  click itself resolved (not a fresh pattern search). `WAITFOR` also
  takes a `TEXT=<value>` condition — wait for a gadget's text to
  exactly equal a value — reusing the same locator parsing and
  gadget-text reading `CLICK`/`TYPE`/`GETTEXT` already share. A
  condition that never becomes true is a new, distinct `RC=15`. See
  [ARexx Reference](ARexx-Reference.md#waitexpectation-primitives).
- **Quirk profiles**: the manifest format ([Manifest
  contract](https://github.com/sidick/amipilot/blob/main/manifest/SPEC.md))
  isn't only for an application's own developer — the same file
  format, loaded the same way, works for a user or the community
  describing a third-party application's structure, with a documented
  convention for recording known behavioral oddities as comment
  lines. No new machinery, one grammar for both cases.
- **The honest-limits toolkit-to-tier table**
  ([Locator Tiers and Limits](Locator-Tiers-and-Limits.md)): which
  locator tier actually reaches which kind of UI, and why — plain
  GadTools/top-level ReAction gadgets, `window.class`+`layout.gadget`
  nested children, MUI applications, and custom-rendered UIs each
  land somewhere different, stated plainly rather than left to
  discover the hard way.
- **Golden-tree fixtures**: a saved structural dump doubles as a
  regression fixture — `amipilot dump <window> --golden PATH
  [--update-golden]`, or `Amipilot.assert_tree_matches()` inside a
  test, compares a live window/gadget tree against a checked-in
  snapshot and fails loudly on drift.
- **The stock-app conformance set**: automation verified against
  genuine, unmodified stock AmigaOS software, not only hand-written
  fixtures — driving AmigaOS 3.2's own Time Preferences editor
  end-to-end via tier-2 locators discovered purely from `AmiInspect`/
  `amipilot dump` output. This also caught two real bugs: a host
  client socket-timeout bug (`connect_with_retry()` could silently
  break a legitimately slow-but-successful `WAITFOR`/`EXPECT=` wait)
  and a genuine machine-wide hang walking a different stock
  application's window (a custom gadget claiming a BOOPSI object
  header it didn't actually have) — both fixed, the latter now
  regression-tested directly against the real application.
- **The MUI-ARexx bridge tier** (`MUIREXX <app-base> [TIMEOUT=<n>]
  <command...>`): drives a MUI (Magic User Interface) application
  through the ARexx port every MUI app carries automatically — a
  different mechanism from `CLICK`/`TYPE`/`GETTEXT` entirely, since
  MUI internals are opaque to structural walking. Verified live
  against a real MUI application that MUI's own built-in ARexx
  support is a small, universal command set (window lifecycle plus
  fixed metadata), not a generic per-widget accessor — `MUIREXX`
  passes an application's own commands through honestly rather than
  promising generic MUI widget control it can't deliver. See
  [ARexx Reference](ARexx-Reference.md#driving-mui-applications).

## v0.4 — 2026-08-07

Reach: everything 0.3's wire needed to actually be useful for driving
a real application end to end — a second transport, launching the
subject under test, moving files, menus, and two new ways to locate
and act on a gadget.

- **TCP transport** (`AmiPilotServer TCP TCPPORT=n`): the same wire
  over `bsdsocket.library`, for real hardware with TCP/IP or an
  emulator with no serial bridge — listen-mode only for now (the host
  connects in). Opt-in hardening: `TCPALLOW` (a source-IP/CIDR
  allowlist) and `TCPPASSWORD` (gates a new `AUTH` verb, defaulting to
  a public starting password so it works out of the box). Neither
  makes this internet-safe — no TLS, no rate-limiting; LAN/trusted-
  network use only, see [Wire Protocol](Wire-Protocol.md#securing-tcp).
- **Program launch** (`LAUNCH [STACK=n] <command-line>`): starts the
  test subject itself over the wire — `SystemTagList()`-based,
  asynchronous, with an overridable stack size — so a test session
  doesn't need the target pre-staged via `S:User-Startup`.
- **The file API** (`FSLIST`/`FSSTAT`/`FSMKDIR`/`FSDELETE`/`FSGET`):
  allowlist-scoped to directories granted at startup (`FSROOT`),
  disabled entirely otherwise. A test-staging channel for small
  fixtures/config/log files, not a file manager — `FSGET` is capped at
  the server's own internal buffer. `FSPUT` (host-to-Amiga writes)
  needs a wire protocol addition and isn't built yet.
- **Menus** (`MENU`/`MENUPICK`): walks a window's live menu strip —
  every pulldown, its items, checkit/checked/enabled state, and any
  keyboard shortcut — and selects an item via that shortcut, the same
  input.device path a human pressing Right-Amiga+key would use.
  Pointer-based selection for items with no shortcut isn't built yet.
- **Multi-screen support** (`SCREENS`, `SCREEN=<substring>`): lists
  every open screen and narrows any window-targeting verb's search to
  a specific one, keyed off each screen's own `DefaultTitle`.
- **Tier-2 semantic locators** (`ROLE=<role>`/`LABEL=<substring>`/
  `INDEX=<n>`, in place of a bare `GA_ID` on `CLICK`/`TYPE`/`GETTEXT`):
  find a gadget by role and label text, or by position among several
  matches, instead of only by numeric ID or a manifest `@name` — see
  [ARexx Reference](ARexx-Reference.md#tier-2-semantic-locators).
  Proximity-to-a-label matching (the third tier-2 style from the
  design docs) isn't built yet.
- **`DRAG`**: a genuine press/move/release drag, either by a pixel
  offset from a gadget's current center (the natural shape for
  adjusting a slider/scroller) or onto a second gadget's center
  (drag-and-drop/reorder, both resolved live, zero coordinates in the
  script).
- **Host-side real serial port support**
  (`Amipilot.connect_serial()`/`WireClient.connect_serial()`, the
  pytest plugin's `--amipilot-serial-device`): connect directly over a
  real or virtual serial port — real Amiga hardware over a real cable,
  or a Copperline config using a real serial device — instead of only
  Copperline's TCP bridge. Optional `pyserial` dependency
  (`pip install amipilot[serial]`).

**Known gaps, tracked as real follow-up work, not silently accepted:**

- No wait/expectation primitives yet (`click` that waits for an
  expected change, timeouts) — a script still adds its own polling.
  Carried over from 0.1–0.3.
- The wire connects host-to-Amiga only; the Amiga dialing out to a
  configured host (useful behind NAT) is a considered future addition
  ([#12](https://github.com/sidick/amipilot/issues/12)), not yet
  built.
- The MUI locator tier (driving MUI apps through their own automatic
  ARexx port) isn't started.
- No public CI on-target run yet, same reason as 0.1–0.3:
  `make test-target` needs a machine-specific Workbench install CI
  doesn't have.

## v0.3 — 2026-08-06

The wire and the host client: the same command set the ARexx port
speaks, now reachable from a host machine — no ARexx interpreter or
even a Workbench session on the Amiga side needed to drive it.

- **The wire protocol** (`server/WIRE.md`): a length-prefixed line
  protocol over serial.device, with no JSON anywhere — requests are the
  exact same command grammar the ARexx port already parses, responses
  are `RC <code> <byte-count>` followed by exactly that many payload
  bytes, binary-safe with zero escaping. A `VERSION` handshake reports
  the server version, the protocol number, and which verbs are stable
  vs. experimental.
- **`AmiPilotServer SERIAL`**: the commodity now optionally carries its
  whole verb set over serial.device (`SERDEVICE`/`SERUNIT`/`BAUD` to
  configure), alongside its existing ARexx port — the same dispatch
  serves both, so results are identical either way. See the new
  [Wire Protocol](Wire-Protocol.md) page.
- **The host Python client** (`host/`, `pip install -e host/`): a
  transport-level `WireClient`, and `Amipilot` — the Pythonic object API
  (`tree()`/`click()`/`type()`/`get_text()`/`manifest()`, plus `@name`
  locator forms) that raises typed exceptions instead of requiring
  manual RC checks.
- **`amipilot dump <window>`**: the host half of "the inspector" —
  connects and prints a window's gadget tree, either in the same format
  `AmiInspect` prints or as ready-to-paste `# name = <id>` suggestions
  for a quirk profile.
- **A pytest plugin**: the `amipilot` fixture boots a configured
  Copperline (or real-hardware-adjacent) session and hands a test a
  connected client — session-scoped, and it skips cleanly rather than
  failing when no emulator config is set up. This delivers the phase's
  actual release gate: a host pytest test types into a field, reads it
  back, clicks a button, and asserts the window closed — driven
  entirely from the host, with Copperline booted by the test itself.

**Known gaps, tracked as real follow-up work, not silently accepted:**

- TCP transport (for real hardware or an emulator with no serial
  bridge) is phase 0.4 scope, along with program launch, the file API,
  menus, and drag.
- No wait/expectation primitives yet (`click` that waits for an
  expected change, timeouts) — a script still adds its own polling.
- The wire connects host-to-Amiga only; the Amiga dialing out to a
  configured host (useful behind NAT) is a considered future addition,
  not yet built.
- No public CI on-target run yet, same reason as 0.1/0.2:
  `make test-target` needs a machine-specific Workbench install CI
  doesn't have.

## v0.2 — 2026-08-05

The act side of object-level GUI automation: a server commodity, driven
by ARexx, with no host machine involved.

- **`AmiPilotServer`**: a commodity hosting the action engine and the
  `intuition-model` walker behind a genuine public ARexx port. See the
  [ARexx Reference](ARexx-Reference.md).
- **`TREE`/`CLICK`/`TYPE`/`GETTEXT`/`QUIT`** — locate a window by title,
  click a gadget by `GA_ID` through a real `input.device` event (the
  documented `IECLASS_NEWPOINTERPOS`/`IESUBCLASS_PIXEL` mechanism, not a
  coordinate hack), type text into it via genuine `IECLASS_RAWKEY`
  events paced to approximate human typing, and read state back —
  proven end to end by driving `AmiPilotServer`'s own test fixture from
  a real ARexx script: type into a field, read the value back, click a
  button, confirm the window closed.
- `AmiInspect`'s gadget-tree output gains a `value=` field for string
  and integer gadgets — their live editable contents, not just their
  label. See the updated [AmiInspect Reference](AmiInspect-Reference.md).
- BOOPSI/ReAction gadget geometry (`GA_Left`/`GA_Top`/`GA_Width`/
  `GA_Height`) is now read correctly, including the classic
  `GFLG_RELWIDTH`/`RELHEIGHT`/`RELRIGHT`/`RELBOTTOM` convention (a
  gadget's size/position stored as an offset from its window's own
  dimensions) — needed for `CLICK`/`TYPE` to land on a BOOPSI gadget at
  all, not just for `AmiInspect` to report sane numbers.
- Both `AmiInspect` and `AmiPilotServer` now embed a standard `$VER:`
  cookie — see [Installation](Installation.md#checking-which-version-you-have).
- `amipilot.lha` now ships both binaries — see
  [Installation](Installation.md).

**Known gaps, tracked as real follow-up work, not silently accepted:**

- `STRING_KIND` and `INTEGER_KIND` GadTools gadgets still aren't
  distinguished from each other (both report as `string`) — carried
  over from 0.1.
- No wire protocol yet — ARexx only reaches scripts running on the same
  Amiga. Serial.device and a host Python client are phase 0.3 scope.
- No wait/expectation primitives yet (`click` that waits for an expected
  change, timeouts) — a script has to add its own `Wait`/polling for now.
- No public CI on-target run yet, same reason as 0.1: `make test-target`
  needs a machine-specific Workbench install CI doesn't have.

## v0.1 — 2026-08-05

First release: the read side of object-level GUI automation.

- `intuition-model`: a reusable Intuition/BOOPSI walker library, reading
  windows and gadgets under strict `LockIBase()` discipline (brief holds,
  copy-out, no live pointers handed out, no patching or `SetFunction()`
  anywhere).
- `AmiInspect`: a standalone Shell command that prints any window's
  gadget tree by role, label, class, ID, position, and state. See the
  [AmiInspect Reference](AmiInspect-Reference.md).
- Plain GadTools role classification, including officially-sanctioned
  `GT_GetGadgetAttrsA` kind-probing to distinguish a checkbox from a
  button (both produce the same underlying gadget type).
- BOOPSI/ReAction class reading via `OCLASS()` — a documented NDK
  mechanism, not a hack — correctly identifying real class names and
  mapping known ones to roles.
- Verified against two purpose-built conformance fixtures and a real,
  unmodified stock AmigaOS Prefs editor (`ScreenMode`) — not just
  software built for this project.
- An automated on-target regression check (`make test-target`, headless
  under Copperline) — see [Building and Testing](Building-and-Testing.md).
- Documented, permanent limits rather than silent gaps: `PLACETEXT_IN`
  button labels and `layout.gadget`-nested gadgets are both genuinely
  unreadable at this tier — see
  [Locator Tiers and Limits](Locator-Tiers-and-Limits.md).
- `amipilot.lha` — a pre-built binary release archive (`AmiInspect` +
  license + this documentation as an AmigaGuide) — see
  [Installation](Installation.md).

**Known gaps, tracked as real follow-up work, not silently accepted:**

- `AmiInspect` doesn't yet embed a `$VER:` cookie.
- `STRING_KIND` and `INTEGER_KIND` GadTools gadgets aren't distinguished
  from each other (both report as `string`).
- No public CI on-target run yet — `make test-target` needs a
  machine-specific Workbench install (see
  [Building and Testing](Building-and-Testing.md)) that CI doesn't have.
