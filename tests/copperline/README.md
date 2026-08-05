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
2. `make amiga fixtures` (or `make docker`) first — the mounts only
   expose whatever's already on disk.

## Running a smoke test

There's no automated harness yet (that's phase 0.3/0.4 — see
`docs/implementation-plan.md`). Today's approach: temporarily add commands
to `S:User-Startup` on the mounted Workbench volume, boot headlessly, and
read the results back from a file written under `SRC:build/`. This
directly mutates a real, possibly-shared Workbench install (e.g. one you
also use interactively via Amiberry) — always back up `S:User-Startup`
first and restore it afterward. A future phase should replace this with a
dedicated bootable fixture image this repo owns outright, so tests stop
touching external state.

```sh
# 1. Back up, then append test commands to S:User-Startup on the
#    Workbench mount, e.g.:
#      Run >NIL: SRC:build/fixtures/GTApp
#      Wait 3
#      SRC:build/AmiInspect WINDOW=GadTools >SRC:build/out.txt

# 2. Boot headless with the control server:
copperline --config tests/copperline/copperline.local.toml \
  --model A1200 --chipset AGA --chip 2M --fast 8M --noaudio \
  --control :0 --control-info /tmp/ccp.json

# 3. Drive it (in another shell):
copperline-ctl --info /tmp/ccp.json run_until '{"seconds": 20}'

# 4. Read the result straight off the host filesystem -- no screenshot
#    parsing needed:
cat build/out.txt

# 5. Clean up: kill copperline, restore the original S:User-Startup.
```

`run_until {"stable_frames": N}` (wait for N identical rendered frames —
much better than a guessed `seconds` delay) is unreliable immediately
after power-on, since the first several frames are identically black
before Kickstart starts drawing; give it a `seconds` head start first, or
use it later in a sequence once something is already on screen.
