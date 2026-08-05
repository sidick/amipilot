# AmiPilot — Implementation Plan

**AmiPilot** is an object-level GUI automation system for classic AmigaOS: an
on-Amiga automation server plus a host-side Python client that together drive
real Amiga GUIs the way Selenium/AutoIt/UIA drive modern ones — find a window
by title, find a gadget by stable ID, label, or role, act on it with genuinely
synthesised input, read state back, and assert semantically rather than by
pixels.

Nothing like it exists on the platform today: automation is either
coordinate-level event injection (universal but blind — breaks on any layout
change and verifies nothing) or ARexx (semantic but cooperative-only — drives
only what an application chose to export). AmiPilot is the missing object
layer between the two.

## Goals

- Host-side test scripts (pytest) make semantic assertions against real Amiga
  GUIs: "set the host string, click Connect, wait for the status label to
  read Connected."
- The same test suite runs deterministically under an emulator in CI and,
  unchanged, against real hardware over TCP.
- Applications that opt in to a published manifest contract are *born
  testable*; everything else gets an honest, tiered best effort with
  explicitly documented limits.
- Zero patching: no `SetFunction()` anywhere. A tool aimed at arbitrary
  third-party software takes no stability risk it can avoid.

## Architecture

```
  host: pytest/scripts ── amipilot client (Python package)
                              |  line/JSON protocol
                     serial.device  |  bsdsocket TCP  |  local ARexx
                              |
        +---------------------v----------------------+
        |  AmiPilot server (commodity, priority low) |
        |  +--------------+   +-------------------+  |
        |  | UI model     |   | Action engine     |  |
        |  | (walkers,    |   | (input.device     |  |
        |  |  roles,      |   |  event synthesis, |  |
        |  |  locators)   |   |  focus, menus)    |  |
        |  +------+-------+   +---------+---------+  |
        +---------|---------------------|------------+
             Intuition/GetAttr     input.device
             (LockIBase, copy-out,  handler
              brief holds)
```

Three components:

1. **`intuition-model`** — a standalone C library (no server dependency) that
   walks Intuition structures and classifies gadgets into AT-SPI-style roles
   with class-specific `GetAttr` readers (GadTools kinds, ClassAct/ReAction
   classes). All walking happens under `LockIBase()` with strictly brief
   holds, no allocation under the lock, and copy-out into a private model.
   Built as a reusable library from day one — it is useful to any tool that
   needs to read GUI structure (screen readers, inspectors), not just this
   server.
2. **AmiPilot server** — an AmigaOS commodity hosting the model, the action
   engine (input.device event synthesis via an input handler), the transports
   (serial.device, bsdsocket TCP, ARexx mirror), program launch, and the
   allowlist-scoped file API.
3. **Host client** — a Python package with a plain object API and a pytest
   plugin (fixtures that boot an emulator config, wait for the server, and
   expose the API).

### Design principles

- **Walk safely or not at all:** brief `LockIBase()` holds, copy-out, poll
  the copied model. Never hand out live Intuition pointers.
- **Act with real input, not shortcuts:** actions synthesise genuine
  input.device events at the gadget's *current* location, resolved at action
  time (so layout and font changes are irrelevant) — clicks, drags, string
  entry as activate-then-typed-keystrokes. The application experiences a
  user, exercising the same IDCMP/notification paths a human would.
- **Read via structures and class knowledge:** window lists, titles, gadget
  states, string buffer contents, listbrowser selections via the model's
  class readers. Screenshots delegate to the emulator's own capture when
  running under one — pixels remain available, just no longer the only
  assertion language.
- **Async by design, with action-scoped expectations:** every locate and
  assert supports wait-with-timeout, and actions compose with expectations
  atomically: `click(button, expect="window:Settings")` snapshots the model,
  acts, then watches for the *delta* until timeout, returning what satisfied
  it or a model diff on failure. This kills the classic act-then-wait race.

## Locator tiers

1. **Manifest IDs (opt-in, first-class).** AmiPilot publishes a small,
   versioned manifest contract: an application (or GUI-generating tool)
   stamps every element with a stable ID (`GA_ID`/GadgetID natively;
   `MUIA_UserData` on MUI) and ships a machine-readable manifest mapping
   logical names → window/ID. Tests address `connect_button` forever,
   regardless of relabelling, relayout, or translation. Zero runtime cost;
   the contract itself is a deliverable of this project.
2. **Semantic locators (well-behaved foreign apps).** Window by title
   pattern; gadget by role + label text, by position-in-set, or by proximity
   to a label. Honest and useful; fragile only against relabelling —
   documented as such.
3. **The MUI bridge.** MUI internals are deliberately opaque to external
   walkers, but every MUI app carries an automatic ARexx port — so the MUI
   tier drives through that port (standard commands plus app-defined ones),
   with tier 2 falling back where the port doesn't reach. Different
   mechanism, same client API.
4. **Coordinates (the floor).** Raw positional actions remain available,
   marked in the API as the fragile tier — for genuinely custom-rendered
   corners nothing structural can see, and nothing else.

Per-application **quirk profiles** (a small config layer) capture
app-specific mappings and known oddities, so community knowledge about
driving a particular program accumulates as shareable data.

**Honest limits, stated up front:** applications drawing custom UIs into
bitmaps are invisible to tiers 1–3; games are out of scope; MUI coverage
depends on what each app's port exposes. The docs will carry a table saying
which tier each common toolkit lands in and why.

## Protocol and client

- One verb set, three surfaces: an ARexx port (the first transport shipped,
  for on-Amiga scripting), then line-oriented request/response with JSON
  payloads over serial.device and TCP.
- **Version handshake from 0.1.** Verbs land marked *stable* or
  *experimental* (experimental verbs may change between minors; stable ones
  never break within a major), and the host client pins the server versions
  it speaks.
- Verbs (v1): `windows/list`, `tree` (full window/gadget model as JSON —
  roles, labels, classes, IDs, states), `find`, `click`, `doubleclick`,
  `drag`, `type`, `menu-pick` (menu strips are walkable data; selection via
  the menu's keyboard shortcut where present, pointer navigation otherwise),
  `get/state`, `get/text`, `wait-for`, action-with-expect, `launch-shell`,
  `launch-wb`, `proc-wait` / `proc-break` (Ctrl-C/D/E/F), `fs-list` /
  `fs-stat` / `fs-put` / `fs-get` / `fs-mkdir` / `fs-delete`
  (allowlist-scoped; disabled until roots are granted),
  `screenshot-request` (emulator-delegated), `quit`.

### The inspector

Discovering locators is the whole workflow for scripting an app you didn't
write, so the `tree` model surfaces three ways:

- **`amipilot dump` on the host** — pretty-printed or JSON tree of any
  window, piped straight into a quirk profile or test file: *dump, copy,
  script*.
- **`AmiInspect`, a standalone on-Amiga Shell command** (ReadArgs, no host
  or server session required): prints the frontmost or named window's gadget
  tree — roles, labels, classes, IDs, positions, states. The platform's
  first equivalent of UIA's Inspect or a browser's element picker.
- **Golden trees:** a saved dump doubles as a structural fixture — "this
  app's UI still has this shape" as a one-line assertion, catching upstream
  UI changes before quirk-profile scripts fail confusingly.

A later pick mode (hover a gadget, see its identity) is v2 polish; the dump
path is v1 because tier 2 is only as good as its discoverability.

### Program launch (tests own their subject's lifecycle)

- **Shell launch:** `SystemTags()`-based, with command line, stack, current
  directory, local environment, and priority configurable; stdout/stderr
  captured through a pipe and returned as assertable text; return code
  surfaced; sync or async with a process handle.
- **Workbench launch, done properly:** read the tool's icon via
  icon.library, merge per-launch tooltype overrides over the icon's array in
  memory (no disk writes), take stack from the icon or an override, and
  construct a genuine `WBStartup` message — argument list with locks and
  names, including project-file arguments ("launch the editor with this
  document"). The launched program experiences a real Workbench start, which
  matters precisely because tooltype parsing and WBStartup handling are code
  paths its tests should exercise.
- **Teardown without a kill verb, by design:** AmigaOS has no safe forcible
  termination, so none is offered. Tests end their subject the way a user
  would — close gadget, quit menu item, the app's own ARexx QUIT — composed
  with expect-window-closed; shell processes additionally accept the
  conventional break signals. An application that cannot be quit cleanly by
  its own affordances has failed a test worth having.

### File system access (fixtures in, artifacts out)

On real hardware over TCP — exactly the audience without shared drives or
host-mounted filesystems — the control channel is the only road in, so the
server carries a small file API:

- **Explore:** `fs-list` (ExAll-based, returning full Amiga metadata —
  protection bits, comments, datestamps, sizes) and `fs-stat`.
- **Transfer:** `fs-put` / `fs-get`, chunked with per-file checksums so
  serial-link transfers are trustworthy; `.info` files travel as ordinary
  bytes, so staging a tool *with its icon* for a Workbench-launch test just
  works. Protection bits and comments settable on put.
- **Manage:** `fs-mkdir`, `fs-delete` — scoped by a **user-provided
  allowlist of directories, never assumed**: the file API is disabled until
  the configuration grants explicit roots, and every fs verb refuses paths
  outside them with a clear error naming the granted list. Docs *suggest*
  `T:` and `RAM:` as sensible grants; nothing is granted implicitly.
- The pytest integration builds a guest-tmpdir fixture on this: created
  under a granted root per test, populated from host paths, harvested and
  removed on teardown.

This is a test-staging channel, not a file manager — serial throughput makes
bulk transfer slow (TCP recommended for anything sizeable). The point is
that a test against a bare machine can be entirely self-contained: connect,
stage, launch, drive, harvest, clean up, disconnect.

## Minimum requirements

**Minimum: AmigaOS 2.04 (V37), plain 68000, no FPU, 1 MB RAM.**
**Recommended / CI-tested: AmigaOS 3.1 (V40), 68020, 2 MB chip + 8 MB fast.**

- **OS:** everything core is V37 API — commodities, GadTools,
  BOOPSI/`GetAttr`, input.device event synthesis, `SystemTags()`, ARexx —
  so 2.04 is the honest floor. Two version lines inside that:
  `ExAll()` is buggy before V39, so the fs verbs use `Examine`/`ExNext` on
  pre-V39 systems; and OS 3.2 (V47) niceties (richer ReAction attribute
  introspection) are runtime-guarded with `lib_Version` checks, enhancing
  output where present and never required. Richer trees on newer OS is
  fine; failing on older is not.
- **CPU:** all binaries compiled for plain 68000, no FPU, no float in hot
  paths. The server's work — copying structures under brief locks, polling
  its own model, formatting text/JSON — is trivial even at 7 MHz.
- **RAM:** server + model copies budgeted well under 100 KB resident,
  allocated from any memory (nothing needs chip RAM). AmiPilot itself is
  small; the real floor is whatever the application under test needs.
- **Transports by tier:** ARexx — any 2.04+ machine. Serial — any machine
  (it's just serial.device). TCP — requires a bsdsocket.library stack
  (Roadshow, AmiTCP, Miami), which in practice means '020-class machines
  with a few MB of fast RAM; that is the stack's floor, not AmiPilot's.
- **CI reference config:** a Copperline A1200 profile (OS 3.1, 68020,
  2 MB chip + 8 MB fast) as the primary run, plus a periodic A500-class
  run (OS 2.04*, 68000, minimal RAM) to keep the stated minimum honest.
  *ROM-image licensing permitting; Copperline ships a redistributable
  bundled AROS Kickstart replacement as its no-ROM-given default (weaker —
  can't validate real-Kickstart-specific behaviour — but needs no licensed
  asset, so it's what public CI runs), with a real ROM used locally and on
  release-gate runs and 2.04 support verified on real hardware per release.

## Toolchain and CI

- **Server / library / AmiInspect:** C, cross-compiled for m68k AmigaOS
  (OS 2.04/V37 minimum, per the requirements section; V39/V47 features
  guarded at runtime).
- **Host client:** Python 3, packaged normally (`pip install amipilot`),
  pytest plugin as an entry point.
- **CI:** [Copperline](https://copperline.dev), a deterministic,
  from-scratch Amiga emulator with a headless JSON-RPC control protocol
  (`--control`, driven by the bundled `copperline-ctl`) purpose-built for
  exactly this kind of scripted boot-run-verify cycle: frame-accurate
  `run_until {seconds|stable_frames}` waits instead of guessed sleeps, and
  hostfs (`[[filesys]]`) mounts that expose a build directory to the guest
  live, with no disk-image step. Boots a fixed OS config with the server
  started; the deterministic serial path is the CI transport. Real-hardware
  runs use the same suite over TCP. (An interactive emulator such as
  Amiberry remains useful for by-hand debugging, but is not the CI engine —
  see `CLAUDE.md`'s "On-target testing" for why.)

## Testing strategy

- **Conformance apps in-repo:** a hand-written ClassAct/ReAction test app
  and a GadTools test app, each with a manifest, serve as the primary
  fixtures — driven in CI through every tier they support.
- **Foreign-app tier** tested against a fixed set of stock programs
  (Workbench windows, Prefs editors, a GadTools app, a ClassAct app) with
  golden interaction scripts.
- **Determinism:** under Copperline's deterministic core, identical
  scripts must produce identical event streams and model states
  run-over-run; flake budget is zero by construction. Zero-sleep policy in
  all shipped examples — wait-for semantics are the only synchronisation
  primitive, mirroring the control protocol's own `run_until
  {stable_frames}` (wait for N identical rendered frames, not a guessed
  delay).
- **Real-hardware pass per release** over TCP; divergences treated as
  findings (app timing, driver behaviour, or emulator inaccuracy).

## Phases (a release train — ship at 0.1, grow in public)

Every phase ends in a tagged public release, because even the first slice is
independently useful: 0.1's `AmiInspect` and 0.2's ARexx-scriptable
automation solve real problems for Amiga users on their own, with no host
machine involved. The price of early users is paid up front via the
protocol version handshake and the stable/experimental verb marking.

The sequencing principle: **Amiga-side tooling first**, because it is
independently useful on its own machine with no host, transport, or emulator
involved — then grow outward through the transports. Each early release is a
complete tool an Amiga user can run today; the host-side test story arrives
once the on-Amiga core has proven itself.

**0.1 — the model and the inspector (2–3 weekends):** `intuition-model`
library (walkers, roles, class readers, LockIBase discipline) +
**`AmiInspect`**, the standalone Shell command that prints any window's
gadget tree — roles, labels, classes, IDs, positions, states. The inspector
is built first because it is independently useful (the platform's first
element picker), and because it is the development tool for everything
after it: every later feature is debugged by looking at its output.
*Release gate:* `AmiInspect` correctly dumps the ClassAct and GadTools test
apps plus a stock Prefs editor — tagged and shipped as **0.1**, a useful
tool on its own.

**0.2 — the server, driven locally (2 weekends):** the server commodity
hosting the model and the action engine (input.device event synthesis:
click, read state/text), with **ARexx as the first transport** — the full
verb set reachable from on-Amiga scripts. Wait semantics and action-scoped
expectations land here, since they are properties of the engine, not the
transport.
*Release gate:* an ARexx script clicks a button on the test app and asserts
a label changed — on-Amiga automation with no host involved.

**0.3 — the wire and the host client (2 weekends):** the line/JSON protocol
with its version handshake over **serial.device**, the host Python client
and `amipilot dump`, the pytest plugin with emulator fixtures, and the
manifest contract (schema published and versioned) with manifest locators.
*Release gate:* a host pytest clicks a button and asserts a label changed,
deterministically, under the emulator in CI.

**0.4 — reach (2 weekends):** **TCP transport**, string entry, menus, drag,
tier-2 semantic locators, program launch (shell with capture; Workbench
with tooltype merge and project arguments), the break-signal/teardown
discipline, and the allowlist-scoped file API with the guest-tmpdir
fixture — the self-contained bare-machine story.

**0.5 (1–2 weekends):** MUI-ARexx bridge tier, quirk profiles, golden-tree
fixtures, the stock-app conformance set, the honest-limits documentation
table.

**1.0:** protocol verbs promoted to stable, real-hardware validation pass,
full docs site.

## Repository layout (target)

```
amipilot/
  server/            C source for the commodity (transports, actions, launch, fs)
  intuition-model/   the reusable walker/role/reader library
  amiinspect/        standalone Shell command
  host/              Python client package + pytest plugin + `amipilot` CLI
  manifest/          the manifest contract: schema, docs, examples
  fixtures/          conformance test apps (ClassAct, GadTools) + manifests
  tests/             host-side pytest suites (CI entry point)
  docs/              this plan, protocol spec, locator-tier table, quirk-profile format
```

## Risks

- **LockIBase discipline is the safety-critical core:** held too long it
  janks the whole machine; ordered wrongly against other locks it
  deadlocks. Mitigations: copy-out with brief holds, no allocation under
  the lock, and the deterministic emulator harness hammering it in CI.
- **App-model diversity** is unbounded in the tail; the tier system and
  quirk profiles bound the promise instead of the ecosystem.
- **MUI opacity** caps that tier at what ports expose — stated, not fought.
- **Flakiness reputation risk:** UI automation's classic failure. Wait-for
  semantics as the only synchronisation primitive, zero-sleep policy in
  shipped examples, and emulator determinism in CI are the defences.
- **Scope gravity toward accessibility:** reading GUIs aloud is a different
  product; the standalone `intuition-model` library is the firewall —
  AmiPilot stays a test tool.

## Success criteria

- The 0.1 release-gate scenario runs green 100 consecutive times under the
  emulator with zero flakes.
- A manifest-carrying test app's interaction test survives a deliberate
  relayout and relabel untouched.
- A stock Prefs editor is driven end-to-end (open, change a setting, save)
  via tier-2 locators only — with the script written *using only
  `dump`/`AmiInspect` output*, no source or docs consulted.
- A `click` with `expect="window:..."` passes deterministically where the
  naive click-then-wait ordering is demonstrated (in a fixture) to race.
- A full lifecycle test passes: Workbench-launch a tool with an overridden
  tooltype and a project argument, assert the override took effect in the
  GUI, drive it, and quit it via its own affordances — no disk-modified
  icons, no kill, window-closed confirmed.
- The same lifecycle test runs entirely self-contained against a bare
  machine over TCP — fixtures staged via `fs-put` (icon included),
  artifacts harvested via `fs-get`, tmpdir cleaned — no shared drive,
  mount, or emulator involved.
- The honest-limits table exists and a MUI app example demonstrates the
  bridge tier's reach and its edge.
