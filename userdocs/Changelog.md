# Changelog

AmiPilot is developed phase by phase; each one is validated on-target
under Copperline against real AmigaOS before being considered done. This
page summarizes what landed in each release, in user-facing terms — see
the repository's
[`docs/implementation-plan.md`](https://github.com/sidick/amipilot/blob/main/docs/implementation-plan.md)
for the full engineering detail and phase sequencing behind each one.

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
