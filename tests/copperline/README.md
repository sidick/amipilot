# tests/copperline

On-target testing via [Copperline](https://copperline.dev) (`brew install
copperline`), a deterministic, scriptable Amiga emulator. This is the
preferred way to verify a change against real AmigaOS behavior — see
`CLAUDE.md`'s "On-target testing" section for the full workflow and why
it replaced driving Amiberry's GUI by hand.

## Setup

1. Copy `copperline.example.toml` to `copperline.local.toml` (gitignored
   — it holds machine-specific absolute paths) and fill in:
   - `rom`: path to a Kickstart 3.2 ROM image (`A1200.47.115.rom` or
     equivalent).
   - The `Workbench` `[[filesys]]` mount: path to a directory holding an
     installed AmigaOS 3.2 Workbench (an existing Amiberry hard-drive
     directory works fine — Copperline reads/writes it live, no image
     file in between).
   - The `SRC` `[[filesys]]` mount: path to this repo's checkout, so
     `build/` binaries are reachable as `SRC:build/...` with no copy step.
2. On that Workbench install, add the **dev-test hook** to `S:User-Startup`
   once (this is a one-time, permanent addition — not something to redo
   per test run):
   ```
   If EXISTS SRC:tests/copperline/smoke.script
     Execute SRC:tests/copperline/smoke.script
   EndIf
   ```
   From then on, staging `tests/copperline/smoke.script` (gitignored) on
   the host and booting is enough to run arbitrary AmigaDOS commands at
   startup — no further edits to the shared `User-Startup` needed, so
   tests stop touching state your interactive Amiberry sessions also use.

## Automated regression check

`make test-target` (from the repo root) runs `run.sh`: builds nothing
itself (run `make amiga fixtures` first, or let `make test-target`'s own
prerequisites do it), boots both `fixtures/gadtools-app` and
`fixtures/classact-app` headlessly in turn, and asserts specific
`AmiInspect` classification lines confirmed correct during phase 0.1
development. Fails loudly (nonzero exit, log + output left under `build/`)
on a crash, a hang, or a classification regression — this is what caught
the `GTYP_CUSTOMGADGET` bitmask bug when deliberately reintroduced to
prove the check works.

Skips cleanly (exit 0, not a false pass) when `copperline.local.toml` is
absent, since CI has no such machine-specific Workbench/ROM asset yet —
see the Makefile's `test-target` target.

```sh
make amiga fixtures
make test-target
```

### Golden-tree fixtures and Locale

`run.sh` also asserts each fixture's live window/gadget tree still
matches a checked-in golden file (`fixtures/gadtools-app/GTApp.golden`,
`fixtures/classact-app/CAApp.golden` — see `host/amipilot/golden.py`).
These two golden files are locale-invariant for this repository's own
fixtures specifically, because `fixtures/gadtools-app/src/main.c` and
`fixtures/classact-app/src/main.c` hardcode their gadget labels and
window titles as plain C string literals — no `locale.library` catalog
lookup, so no text in their golden files can change with the running
system's Locale preference.

**That is a property of these two fixtures, not of golden trees in
general.** A golden file taken against a real, localized application
(one that resolves its window title or gadget labels through a
catalog) will only reproduce on a machine set to the same Locale that
generated it — the window/screen title lines in the rendered text are
literal strings the running OS/app supplied, and comparison is an
exact match with no locale-awareness built in. If you generate golden
files for your own application under test, regenerate them against a
single, documented reference Locale (or keep one canonical machine/CI
environment that owns golden-file generation) rather than letting
each contributor's own Workbench install produce a slightly different
"golden" and fight over spurious diffs.

### Stock-app conformance (phase 0.5)

`run.sh` also drives a genuine OS-shipped stock application, not a
hand-written fixture: `run_stock_app_check` starts `AmiPilotServer`
only (no fixture pre-launched), then `tests/copperline/
stock-app-test.py` `LAUNCH`es `SYS:Prefs/Time` over the wire and
drives it end to end using only tier-2 `ROLE=`/`INDEX=` locators
discovered from `AmiInspect`/`amipilot dump` output — this is the
implementation plan's own stated success criterion for this work
(`docs/implementation-plan.md`'s "Success criteria": "A stock Prefs
editor is driven end-to-end... via tier-2 locators only... with the
script written using only dump/AmiInspect output, no source or docs
consulted"). Read the script's own header before touching it — it's
an honest account of what was tried and what actually worked (the
year field and "Save" button both turned out to be inert under this
machine profile's `rtc: none` config), not a clean success story
written after the fact. `tests/copperline/Time.manifest` is the quirk
profile capturing those findings (see `manifest/SPEC.md`'s "Quirk
profiles" section).

Along the way this also found and fixed a real bug in `host/amipilot/
client.py`'s `connect_with_retry()`: it left a returned client's
socket read timeout clamped to whatever small value its own retry
loop last shrank it to (as little as 0.1s), so any `WAITFOR`/
`CLICK(expect=...)` call with a `TIMEOUT=` longer than that would
raise a raw socket `TimeoutError` instead of the intended, clean RC-15
`Timeout` exception — even when the server answered correctly. See
that method's own docstring for the fix and how to size
`connect_timeout` for your own long-running waits.

`SYS:Prefs/WBPattern` (a different stock Prefs editor) surfaced a
second, still-open finding the same way: `AmiInspect` genuinely hangs
walking its window (confirmed for 90+ simulated seconds with zero
forward progress, not just slow) — filed as issue #36, not
root-caused yet. Stock-app conformance testing is explicitly meant to
surface exactly this kind of gap against real, OS-shipped software,
not just prove the happy path.

### Bare-machine lifecycle (phase 1.0, docs/implementation-plan.md's own success criteria)

`run_bare_lifecycle_check` is the literal scenario the implementation
plan's own success criteria describe: "Workbench-launch a tool with an
overridden tooltype and a project argument, assert the override took
effect in the GUI, drive it, and quit it via its own affordances...
entirely self-contained against a bare machine over TCP -- fixtures
staged via fs-put (icon included), artifacts harvested via fs-get,
tmpdir cleaned -- no shared drive, mount, or emulator involved."

Unlike `run_wblaunch_check` above (which stages `WBApp`'s own icon and
launches it both via the SRC: hostfs mount), this check's own
`smoke.script` does only two things: run `MakeIcon` once (still
fundamentally needs a real Amiga environment, since it reads the
system's own live default WBTOOL icon -- not different in kind from
`fixtures/wbgui-app` itself being a pre-built cross-compiled artifact)
to produce a reusable `fixtures/wbgui-app/WBGuiApp.info` on the host
disk, then start `AmiPilotServer TCP FSROOT=T:` under Copperline
0.15's `--hostsocket-net host` (see the "P96/Picasso96 RTG" section
below for the CPU-profile fix this project needed to make TCP/RTG
checks work at all) -- no fixture launch via SRC: at all. Everything
else (staging the binary/icon/project-arg file, `WBLAUNCH`ing,
driving, harvesting, cleanup) happens purely over the wire from
`tests/copperline/bare-lifecycle-test.py`.

`fixtures/wbgui-app` is a deliberately SEPARATE fixture from
`wbapp`/`WBApp` above, not a modification of it -- giving `WBApp` a
real GUI window would break every existing `run_wblaunch_check`
assertion that depends on it running to completion and exiting
promptly. It's also the only fixture in this project that's both
Workbench-startable AND has real GUI state to assert on. A genuine,
newly-found limitation surfaced building this: clicking a GadTools-
context window's own system close gadget via the tier-2
`ROLE=custom INDEX=` locator reports success but has no effect (the
same window's drag bar responds fine to `WINDOWMOVE`) -- filed as
issue #60, not root-caused yet. `fixtures/wbgui-app` sidesteps it with
a real `_Quit` button instead (a numeric `GA_ID` click, the mechanism
proven reliable everywhere else in this project) rather than relying
on the close gadget.

### P96/Picasso96 RTG (phase 1.0, GitHub issue #55)

`run_screenshot_p96_check` exercises `SCREENSHOT`'s P96-active capture
path (`server/src/screenshot.c`, issue #44) against a REAL Picasso96/
RTG board under Copperline itself, using its 0.15-and-later `[rtg]`
support — the missing piece `server/README.md`'s own SCREENSHOT
section used to call "honestly unverified," since Copperline had no
RTG emulation at all before 0.15.

**Skip-safe by design, opt-in.** Most contributors' own
`copperline.local.toml` won't have `[rtg]` configured — it's not in
`copperline.example.toml` by default. `fixtures/p96-app` detects this
itself (no P96 mode available at all) and writes a real `SKIP ...`
status rather than crashing or hanging; `run.sh` reports this as a
genuine skip, not a failure, the same "skip cleanly, don't falsely
pass" precedent the whole `make test-target` gate already sets for
`copperline.local.toml` itself being absent. If you want this check to
actually exercise the real capture path rather than skip:

```toml
[rtg]
card = "picasso2"
vram = "2M"
```

Two real, non-obvious things had to be true before this worked, found
live getting it running:

1. **CPU/address-space, not a Copperline bug -- and fixed for every
   check, not just this one.** The `--model A1200` default profile
   uses a 68EC020 (24-bit/16MB address bus); `--fast 8M` at `$200000`
   consumes nearly all the scarce Zorro II autoconfig space, leaving
   none for the RTG board's VRAM, which then autoconfigures at address
   `$00000000` and gets shut down by the OS's own Expansion Board
   Diagnostic screen -- which also blocks headless boot outright,
   since it needs a manual "Continue" click. Every check in `run.sh`
   now uses `--cpu 68020 --chip 2M --accelerator 8M` (fast RAM at the
   accelerator slot, `0x08000000`, well outside the 24-bit space)
   instead of `--model A1200`'s own 68EC020 + `--fast 8M` (Zorro II).
   Not a workaround scoped to this one check: `CLAUDE.md`'s own
   "Recommended/CI-tested config" already says plain "68020", not
   "68EC020" -- every check here was quietly emulating the wrong CPU
   variant before this -- and a real A1200 has no built-in Zorro II
   slots at all, so `--accelerator` (an accelerator-slot card) is the
   historically accurate choice for A1200 fast RAM anyway, not just a
   fix for RTG's own address-space needs.
2. **The right monitor driver has to be bound, not just the board
   present.** Even with the board autoconfiguring cleanly,
   `Picasso96API.library` can open fine and correctly report the board
   (`p96GetBoardDataTags` naming it `"PicassoII"`) while still having
   **zero display modes registered** if the Workbench install's own
   Picasso96 setup doesn't have a monitor driver matching the specific
   hardware Copperline's `picasso2` emulates (CL-GD5426) -- e.g. a
   Workbench disk set up for Amiberry's own `uaegfx` virtual card
   won't necessarily have this. Install the matching Picasso96 monitor
   driver on the Workbench disk if `p96BestModeIDTags()` keeps
   returning `INVALID_ID` even after fixing (1).

Once both are true, verified pixel-for-pixel identical results to the
manual Amiberry verification (`server/README.md`'s SCREENSHOT
section): a known `x%4` pen-ramp pattern painted on a genuine P96 CLUT
screen decodes back exactly.

## Ad hoc smoke testing (debugging, new fixtures)

For anything `run.sh` doesn't already assert on, write
`tests/copperline/smoke.script` directly and drive Copperline by hand via
its `--control` JSON-RPC server (`copperline-ctl`):

```sh
# 1. Write tests/copperline/smoke.script, e.g.:
#      Run >NIL: SRC:build/fixtures/GTApp
#      Wait 5
#      SRC:build/AmiInspect WINDOW=GadTools >SRC:build/out.txt
#      Echo "DONE" >SRC:build/marker.txt

# 2. Boot headless with the control server:
copperline --config tests/copperline/copperline.local.toml \
  --model A1200 --chipset AGA --chip 2M --fast 8M --noaudio \
  --control :0 --control-info /tmp/ccp.json

# 3. Drive it (in another shell):
copperline-ctl --info /tmp/ccp.json run_until '{"seconds": 20}'

# 4. Read the result straight off the host filesystem -- no screenshot
#    parsing needed:
cat build/out.txt

# 5. Kill the copperline process; delete smoke.script when done (it's
#    gitignored scratch, not committed).
```

`run_until {"stable_frames": N}` (wait for N identical rendered frames —
much better than a guessed `seconds` delay) is unreliable immediately
after power-on, since the first several frames are identically black
before Kickstart starts drawing; give it a `seconds` head start first, or
use it later in a sequence once something is already on screen.

If a run produces no output at all, check `build/` actually has fresh
binaries first (`SRC:build/AmiInspect: Unknown command` means `make
clean` ran and nothing rebuilt since — not a Copperline or hostfs
problem) before assuming something is broken on the Amiga side.
