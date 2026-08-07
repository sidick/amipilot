# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

AmiPilot is an object-level GUI automation system for classic AmigaOS: an
on-Amiga automation server plus a host-side Python client that drive real
Amiga GUIs semantically (find a window/gadget by ID, label, or role; act
with genuinely synthesised input; assert on state) rather than by pixel
coordinates or screen scraping. The full design, locator-tier model, wire
protocol, and phase sequencing live in `docs/implementation-plan.md` —
read that before making architectural decisions; this file only covers
what's needed to build and navigate the code day to day.

**Current state:** v0.4 released (phase 0.4, reach, complete). Phase
0.5's scope is feature-complete on main, not yet tagged: `WAITFOR` (including its
`TEXT=` condition) and `CLICK`'s `EXPECT=` (wait/expectation
primitives, docs/implementation-plan.md's "Async by design" section)
-- see `server/README.md`'s own section. Quirk profiles are real too:
same manifest format/parser (`manifest/SPEC.md`), no new machinery,
just a documented convention for community-authored, third-party-app
manifests carrying known-oddity notes as comment lines -- see
`manifest/SPEC.md`'s own "Quirk profiles" section. The honest-limits
toolkit-to-tier table lives in
`userdocs/Locator-Tiers-and-Limits.md`. Golden-tree fixtures are real
too: `amipilot.golden`'s `assert_golden()`/`GoldenMismatch`, wired
into `amipilot dump --golden`/`Amipilot.assert_tree_matches()` and
into `tests/copperline/run.sh`'s `run_golden_check` against real,
checked-in golden files for both fixtures
(`fixtures/gadtools-app/GTApp.golden`,
`fixtures/classact-app/CAApp.golden`) -- see
`tests/copperline/README.md`'s "Golden-tree fixtures and Locale"
section for the real reproducibility caveat found while building
this (window/screen titles, and a real app's catalog-driven labels,
aren't Locale-invariant). The stock-app conformance set is real too:
`tests/copperline/stock-app-test.py` (wired into `run.sh`'s
`run_stock_app_check`) drives AmigaOS 3.2's own `SYS:Prefs/Time` --
launched over the wire, not a hand-written fixture -- end to end via
tier-2 `ROLE=`/`INDEX=` locators discovered purely from AmiInspect/
`amipilot dump` output; see its own header for what was actually
tried and confirmed live (its year field and "Save" button turned out
to be genuinely inert under this profile's `rtc: none` config -- an
honest finding, recorded as a quirk profile at
`tests/copperline/Time.manifest`, not silently worked around) and
`host/amipilot/client.py`'s `connect_with_retry()` docstring for a
real bug this work found and fixed along the way: the method left a
returned client's socket read timeout clamped to a leftover value
from its own retry loop (as little as 0.1s), silently breaking any
`WAITFOR`/`CLICK(expect=...)` whose `TIMEOUT=` exceeded it. A second
real bug from the same stock-app-conformance work, found and fixed
(issue #36, closed): `AmiInspect` genuinely hung (a true deadlock, not
a spin loop -- confirmed via GDB attached live to Copperline's `--gdb`
remote server, `TaskWait`/`tc_SigWait` archaeology, and an
instrumented walker build that pinned the exact gadget) walking a
different stock app's window (`SYS:Prefs/WBPattern`, one of its
custom-drawn Preview/Sketch boxes) -- its `GadgetType` bits claimed
`GTYP_CUSTOMGADGET` without a real BOOPSI `_Object` header behind
them, and `WalkGadgetList()` used to trust `OCLASS()`'s result
unconditionally. Fixed with a `TypeOfMem()` sanity check (see
`intuition-model/src/walk.c`'s own comment there) before dispatching
anything through it; `tests/copperline/run.sh`'s `run_wbpattern_check`
regression-tests this against the real app. The MUI-ARexx bridge tier
is real too, completing phase 0.5's scope: `MUIREXX <app-base>
[TIMEOUT=<n>] <command...>` (`server/src/muirexx.c`,
`Amipilot.mui_command()`) sends an ARexx command verbatim to a MUI
application's own port -- confirmed live against AmigaOS 3.2's own
MUI-Demo that MUI's built-in ARexx support is a small, universal
seven-command set (`quit`/`hide`/`show`/`activate`/`deactivate`/
`info`/`help`), not a generic widget-value accessor, so this bridge
is an honest passthrough rather than a CLICK/TYPE-shaped verb built on
a false promise -- see `server/README.md`'s own section and
`tests/copperline/run.sh`'s `run_mui_check` (skips cleanly when MUI
isn't installed on the configured Workbench volume, since it's a
third-party archive, not a standard Workbench component).
`intuition-model/` (the walker library) and `amiinspect/` (the Shell
command) are real, building, and verified on-target. `server/` is
real: the action engine (`server/src/action.c`, click/type/geometry/
menu-pick) and `AmiPilotServer` (`server/src/amipilotserver/`), a
commodity hosting both behind a genuine public ARexx port, the serial
wire transport (`server/src/serial.c`), and (0.4) TCP
(`server/src/tcp.c`, listen-mode only) — framing contract in
`server/WIRE.md`. TCP now has opt-in hardening: `TCPALLOW` (a source-
IP/CIDR allowlist, comma-separated single value — NOT `/M`, ReadArgs
only allows one repeatable keyword per template and `FSROOT` already
claims it) and `TCPPASSWORD` (gates the `AUTH` verb, defaults to the
public `"amipilot"` the host client sends automatically). Neither is
mandatory, and **neither makes this internet-safe** — no TLS, a
public default password, no rate-limiting; LAN/trusted-network use
only, see `server/README.md`'s TCP section. Phase 0.4 additions
beyond TCP: `LAUNCH` (start a test subject over the wire), the
allowlist-scoped file API (`FSLIST`/`FSSTAT`/`FSMKDIR`/`FSDELETE`/
`FSGET`, `server/src/fs.c` — `FSPUT` deliberately deferred, needs a
wire protocol addition), menu walking + shortcut-based selection
(`MENU`/`MENUPICK`, `intuition-model`'s `AmipWalkMenuStrip()` —
pointer-based selection for shortcut-less items not yet built),
multi-screen support (`SCREENS`/`SCREEN=`, keyed off
`Screen->DefaultTitle`, not the live `Title` field), tier-2 semantic
locators (`ROLE=`/`LABEL=`/`INDEX=` on CLICK/TYPE/GETTEXT's classic
form, resolved via a fresh `AmipWalkWindow()` walk — proximity-to-
label matching deliberately not built, an honest limit not a gap),
and `DRAG` (`server/src/action.c`'s `AmipDragAt()`/
`AmipDragGadgetBy()`/`AmipDragGadgetToGadget()` — an offset form for
sliders/scrollers and a gadget-to-gadget form for drag-and-drop,
built on a single press/absolute-jump/release, not synthesized
continuous motion). See
`server/README.md` for the full verb set and what's verified for each.
`manifest/` carries the manifest contract (`manifest/SPEC.md`, parsed
by `server/src/manifest.c` — no screen-awareness yet, `SCREEN=` only
applies to the classic locator form). `host/` is a real, installable
package: the wire client (`host/amipilot/wire.py`), text-format
parsers (`host/amipilot/model.py` for TREE, `menu.py` for MENU,
`screen.py` for SCREENS, `fs.py` for the file API), the object API
(`host/amipilot/client.py` — `Amipilot`, what test code actually
imports, including `wait_for_window()`/`wait_for_screen()` polling
helpers), `amipilot dump` (`host/amipilot/dump.py`, a console script
via `host/pyproject.toml`), and the pytest plugin
(`host/amipilot/pytest_plugin.py`, auto-registered via the `pytest11`
entry point) — its session-scoped `amipilot` fixture boots a
Copperline config and hands a test a connected client, which is phase
0.3's actual release gate ("a host pytest clicks a button and asserts
a label changed, deterministically, under the emulator") verified
live via `tests/copperline/pytest-example/`. All of it has real test
coverage (`make test-host` runs pytest — a superset that also collects
the stdlib-unittest files here — with `host/` editable-installed first
so the plugin's entry point is real, not force-loaded).
User-facing documentation lives in `userdocs/` (built
as a MkDocs site,
`mkdocs.yml`) and mirrored to AmigaGuide via `make guide`
(`tools/docs2guide.py`) — see `userdocs/Building-and-Testing.md`.

## Build commands

Requires Bebbo's m68k-amigaos GCC (`m68k-amigaos-gcc`) on `PATH`, or use
the container image:

```sh
make amiga          # build intuition-model + AmiInspect
make fixtures        # build the on-Amiga fixture apps (fixtures/*)
make docker          # run 'make amiga fixtures' inside ghcr.io/sidick/amiga-dev
make clean
```

CI verb contract (`sidick/amiga-workflows/build-test.yml`, invoked from
`.github/workflows/ci.yml`) — these Makefile targets exist because CI
calls them by name, not because they're the primary local entry points:

```sh
make build           # = amiga + fixtures
make test-host        # host-side unit tests (host/tests, stdlib unittest, no deps)
make test-target      # boots both fixtures under Copperline, asserts AmiInspect's output
make lint             # semgrep --config auto over intuition-model/ amiinspect/ server/ fixtures/
```

`make test-target` (`tests/copperline/run.sh`) is a real check when
`tests/copperline/copperline.local.toml` exists locally (see "On-target
testing" below); it skips cleanly, not a false pass, when that
machine-specific file is absent (e.g. in CI, which has no such
Workbench/ROM asset yet). It caught the `GTYP_CUSTOMGADGET` masking
crash below when deliberately reintroduced — proven, not just written.

Toolchain flags worth knowing before touching the Makefile: `-m68000
-msoft-float` (the real target floor, not a default — see "Minimum
requirements" below) and `-noixemul` (links against libnix, not
ixemul.library — see the `libnix` skill for its conventions if editing
startup/library-open code).

## Minimum requirements

AmigaOS 2.04 (V37) floor, plain 68000, no FPU, ~1MB RAM. Recommended/
CI-tested config is OS 3.1, 68020, 2MB chip + 8MB fast. Full rationale in
`docs/implementation-plan.md` under "Minimum requirements" — check it
before using any API newer than V37, or anything requiring an FPU.

## On-target testing

`make test-target` (`tests/copperline/run.sh`) is now a real automated
harness — a dozen-plus checks covering every verb, run live against a
real Copperline boot; see the "Build commands" section above. Two ways
to actually run code against real AmigaOS behavior interactively:

**Preferred: Copperline** (`brew install copperline`), a deterministic,
scriptable emulator — see `tests/copperline/README.md` for full setup.
Its `--control` JSON-RPC server (driven via `copperline-ctl`) gives
frame-accurate `run_until {seconds|stable_frames}` waits and an
`input.mouse_to {x, y}` that servos the pointer to an exact pixel by
watching sprite 0, instead of guessing relative mouse deltas and
screenshotting to check. For a scripted boot-run-verify cycle with *no*
GUI input at all, add commands to `S:User-Startup` on the mounted
Workbench volume (**back it up first, restore it after** — this mutates
a real, possibly-shared Workbench install) and read results straight off
the host filesystem via the `SRC:` hostfs mount — no screenshot parsing,
no window-focus fights. This is how the checkbox-classification fix and
the `GT_Underscore` fix below were verified.

**Fallback: Amiberry MCP tools** (`mcp__amiberry__*`) for interactive,
by-hand debugging (visually checking layout, poking around) against the
`amipilot.uae` config (Kickstart 3.2, A1200/AGA, Workbench 3.2.3). Slower
and fussier for anything scripted — expect to fight relative mouse deltas
and window z-order — so reach for Copperline first unless you specifically
need to watch it interactively.
1. `mcp__amiberry__launch_and_wait_for_ipc` with `config: "amipilot.uae"`.
2. Immediately call `mcp__amiberry__set_active_instance` with `instance:
   0` — without this, IPC calls silently fail with "Connection refused"
   even though the socket is live (a stray-connector-process quirk, not a
   real connection problem).
3. The config mounts this repo's working directory as `SRC:` and a
   Workbench hard drive as `DH0:` — freshly built binaries under `build/`
   are immediately visible as `SRC:build/...` with no copying step.
4. Open a Shell (Right-Amiga+E → type `NewShell` → Return), `run
   SRC:build/fixtures/GTApp` to launch a target, `SRC:build/AmiInspect
   WINDOW=<substring>` to inspect it. Redirect output to a file on `SRC:`
   and read it back rather than trusting a screenshot.
5. `mcp__amiberry__kill_amiberry` when done.

Both paths: always rebuild (`make amiga fixtures`) before booting — the
hostfs/SRC: mount only exposes what's already on disk, and `make clean`
silently leaves you testing against a binary that no longer exists.

Two known gaps already found this way, documented as TODO comments at
their exact site in `intuition-model/src/walk.c`, not silently patched
around: GadTools populates `gadget->GadgetText` for external-label
placements (`PLACETEXT_LEFT/RIGHT/ABOVE/BELOW`) on kinds like
`CHECKBOX_KIND`/`STRING_KIND`, but **`BUTTON_KIND` is a documented
exception** — confirmed against a second button added to
`fixtures/gadtools-app` (2026-08-07) that `PLACETEXT_RIGHT` bakes a
button's label into its rendered imagery exactly like `PLACETEXT_IN`
does, leaving `gadget->GadgetText` NULL either way; a button's label
is invisible to this tier under every `PLACETEXT_*` value tried, not
just `PLACETEXT_IN` as originally thought (address a button by
`GA_ID` or a `ROLE=`/`INDEX=` locator, not `LABEL=`); and
`BUTTON_KIND`/`CHECKBOX_KIND` both produce a plain `GTYP_BOOLGADGET`,
requiring a `GT_GetGadgetAttrsA` kind-probe (see `ClassifyBoolGadget`)
to tell them apart rather than a single flag check.

A third, more serious one found the same way (2026-08-07): a real,
OS-shipped stock application's `GadgetType` bits can claim
`GTYP_CUSTOMGADGET` (correctly matching the masked check the
`GTYP_CUSTOMGADGET`/`GTYP_BOOLGADGET` bitmask fix above already
requires) on a gadget that does **not** actually carry a genuine
BOOPSI `_Object` header — confirmed against AmigaOS 3.2's own
`SYS:Prefs/WBPattern` (its custom-drawn Preview/Sketch boxes).
`OCLASS()` there returns an implausible pointer, and dispatching
`GetAttr()`/`DoMethod()` through it unconditionally (as the walker
used to) wedges the entire machine — root-caused via GDB attached
live to Copperline's `--gdb` remote server (see the closed
investigation on GitHub issue #36 for the full methodology, including
two real amiga-gcc/vlink toolchain gotchas hit getting debug symbols
working at all). Fixed in `WalkGadgetList()` with a `TypeOfMem()`
sanity check on `OCLASS()`'s result before trusting it for anything —
the documented, honest way to confirm a pointer refers to allocated
system memory at all, not a guess at "plausible" address ranges.
Degrades to `role=custom` with no class/label, the same graceful path
an unrecognised class already took. `tests/copperline/run.sh`'s
`run_wbpattern_check` regression-tests this directly against the real
stock app, not just a hand-written fixture.

## Architecture

**`intuition-model/`** — the reusable Intuition walker library, with no
dependency on the server. Every walk starts with a brief `LockIBase()`
hold to confirm its target is still genuinely linked into Intuition's
own live screen/window lists, copying out a private model
(`AmipWindowModel` → linked list of `AmipGadgetModel`); nothing hands
out live Intuition pointers, and nothing patches anything (no
`SetFunction` anywhere in this codebase — see "Design principles" in
the implementation plan). **The lock does NOT stay held for the whole
walk** — `LockIBase()`'s own autodoc is explicit that no Intuition,
graphics, layers, or dos call is permitted while holding it, and the
per-gadget work (`GetAttr()`/`OCLASS()` dispatch, `CopyString()`'s own
`AllocVec()`) needs exactly those, so it happens after release. This
narrows, but doesn't eliminate, the gap between the liveness check and
the walk finishing — a window/gadget closing in that instant is a
known, accepted risk (same shape as `AmipIsWindowOpen()`'s own
documented limit for the action engine, below), not a guarantee this
library claims to provide. `AmipRole` is an AT-SPI-style
classification independent of which toolkit produced the gadget
(GadTools, BOOPSI/ReAction, MUI). Role classification covers plain
GadTools kinds (`ClassifyGadget`/`ClassifyBoolGadget`) and BOOPSI/
ReAction classes via `OCLASS()` (`ClassifyByClassID`) — a documented NDK
mechanism for reading a live BOOPSI object's class name from outside,
not a hack. **Confirmed limit:** a `window.class` window attaches only
its single top-level `layout.gadget` to `window->FirstGadget` — the
layout's own button/string/checkbox children aren't individually
walkable there, and there's no public API to enumerate them on classic
OS 3.x (see the implementation plan's "Honest limits" section and the
comment at `ClassifyByClassID` in `walk.c`). Don't try to "fix" this
with a reverse-engineered private struct — it's a stated, permanent
constraint, not an oversight.

Library-base convention: functions that need `gadtools.library` (the
`CHECKBOX_KIND` discriminator) consume the global `GadToolsBase` via
`proto/gadtools.h`'s `extern` declaration but never open or close it —
the calling program (currently `amiinspect/src/main.c`) owns that
lifecycle, same pattern as `IntuitionBase`. If `GadToolsBase` is `NULL`
(library not opened, or genuinely unavailable), classification degrades
gracefully rather than failing.

**Always initialize library-base globals** (`struct Library *FooBase =
NULL;`, never `struct Library *FooBase;`). An uninitialized base is a
COMMON symbol, which doesn't stop the linker from pulling libnix's own
same-named archive member — and that member carries a pre-`main()`
auto-open constructor that derives the library name from the base name
and aborts the whole program if the open fails (for `WindowBase` it
tries the nonexistent `"window.library"`, printing "window.library
failed to load" and exiting with rc 20 before any of your code runs).
This cost a full debugging session on 2026-08-05; see the header comment
in `fixtures/classact-app/src/main.c`. Two related ReAction findings
from the same session: BOOPSI gadget objects need `-lamiga`
(`DoMethod`/`NewObject` varargs marshaling), and `string.gadget`/
`checkbox.gadget` don't register public class names — use
`STRING_GetClass()`/`CHECKBOX_GetClass()`, not
`NewObject(NULL, "string.gadget", ...)` (which works for
`button.gadget` but returns NULL for these).

**`amiinspect/`** — standalone Shell command (`ReadArgs` template
`WINDOW/K`), the platform's first UIA-Inspect-equivalent. Finds a window
by title substring (or defaults to the active window), walks it via
`intuition-model`, prints the gadget tree. No host or server session
required — this is deliberately usable standing at the machine itself.
This is also *the* development/verification tool for everything else in
the walker: when in doubt about what a gadget structure actually looks
like, run `AmiInspect` against it rather than guessing from headers.

**`fixtures/`** — hand-written test apps used to exercise the walker
against real gadgets instead of only a window's built-in system gadgets.
`gadtools-app/` is a plain GadTools app (button + string + checkbox, real
`GA_ID`s) built directly against system libraries — it is the *target*
being inspected, not a consumer of `intuition-model`, so it doesn't link
against the library. When adding a fixture, remember `GT_Underscore` in
every `CreateGadget` tag list, or the `_` shortcut markers in labels
render as literal underscores instead of underlined shortcuts.

**`server/`, `host/`, `manifest/`, `tests/`** — see "Current state"
above for what's real in each. Phase 0.4 scope is now complete: TCP
transport + hardening (`TCPALLOW`/`TCPPASSWORD`), string entry, menus,
launch, the file API, tier-2 semantic locators (`ROLE=`/`LABEL=`/
`INDEX=`), and drag (`DRAG`, both the offset and gadget-to-gadget
forms) have all landed on main. Read the corresponding `README.md`,
`server/WIRE.md`, and the implementation plan's "Phases" section
before starting work in any of these.

## House conventions (this repo and its siblings)

- Version lives in `version.mk` (`VERSION`/`REVISION`, Amiga major.minor).
- License is BSD 2-Clause; new source files don't need a header (LICENSE
  file covers the repo), but workflow/CI files carry an SPDX header — see
  `.github/workflows/ci.yml` for the pattern.
- CI is a thin wrapper around a shared reusable workflow
  (`sidick/amiga-workflows/build-test.yml@v1`) that calls the Makefile's
  five-verb contract. Don't rename `build`/`test-host`/`test-target`/
  `lint`/`dist` — CI depends on those exact names.
- Real functions, not guessed heuristics: when the correct behavior isn't
  obvious from the header alone (as with the checkbox/button
  discrimination), find the documented contract (autodocs, RKRM) rather
  than assuming a plausible-looking flag check is right — then verify
  against a real fixture in the emulator, don't trust compilation alone.
