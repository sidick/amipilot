# Building and Testing

This page is for building AmiPilot from source and running its on-target
conformance check — not needed if you're just using the released
`AmiInspect` binary (see [Installation](Installation.md)).

## Cross-building

The Amiga build uses the Bebbo `m68k-amigaos-gcc` cross-toolchain.

```
make docker   # cross-compiles build/AmiInspect and the conformance
              # fixtures inside a pinned Docker image -- no local
              # toolchain needed
```

Or, with a local `m68k-amigaos-gcc` on `PATH`:

```
make amiga      # AmiInspect only
make fixtures   # + the conformance fixtures (gadtools-app, classact-app,
                # second-screen-app, wbapp)
make server     # + AmiPilotServer itself, and the internal
                # AmiClickTest/AmiSetMouse test helpers
```

## Building and testing the host Python package

The wire client, host object API, and pytest plugin live under `host/`
as a real, installable package:

```
pip install -e 'host/[test]'   # editable install, with test extras
make test-host                 # = pip install -e 'host/[test]' + pytest host/tests
```

`test-host` runs entirely against a scripted transport (no emulator
needed) — it's testing the wire framing, the `TREE`/`MENU`/`SCREENS`/
file-API parsers, the object API's exception mapping, and the pytest
plugin's own boot/skip logic. Installing editable first matters: it's
what makes `amipilot.pytest_plugin` register via its real `pytest11`
entry point, the same way a real consumer of the package sees it,
rather than being force-loaded.

## On-target testing under Copperline

AmiPilot's conformance check runs the actual compiled binaries and
fixture applications under the
[Copperline](https://copperline.dev) Amiga emulator — not Amiberry;
Copperline's headless, deterministic `--control` protocol is a much
better fit for scripted, repeatable verification than driving an
interactive emulator's GUI by hand.

```
make test-target
```

This is a whole suite (`tests/copperline/run.sh`), not just the
original `AmiInspect` classification check: `AmiInspect` structural
walking against both fixtures and a real stock Prefs editor
(`WBPattern`), the action engine (`CLICK`/`TYPE`/`DRAG`), the ARexx
port and manifest locators, the wire protocol over both serial and
TCP, `LAUNCH`/`WBLAUNCH`, `SCREENSHOT`, the MUI-ARexx bridge, the file
API, `MENU`/`MENUPICK`, `SCREENS`, `WINDOWMOVE`/`WINDOWSIZE`, the
checked-in golden-tree fixture regression check, and (if the host
package is installed) the phase 0.3 pytest release-gate scenario
itself. It's proven to actually catch a regression, not just written
to look like it does: a real crash bug (a gadget-type bitmask check
that misidentified plain GadTools gadgets as BOOPSI objects) was
deliberately reintroduced during 0.1 development, and this check
caught it before being reverted — and a second, genuine deadlock bug
(found live against AmigaOS 3.2's own `WBPattern`, GitHub issue #36)
is now a permanent regression check (`run_wbpattern_check`) too.

**Needs `tests/copperline/copperline.local.toml`** (gitignored — a
machine-specific Kickstart ROM + Workbench install path; see
`tests/copperline/copperline.example.toml` and that directory's own
README for setup). Skips cleanly, not a false pass, when that file is
absent — which is why public CI's `test-target` run is currently a
no-op: there's no redistributable AmigaOS 3.x Workbench + ReAction image
this project can ship for CI to use yet. This is tracked as real,
outstanding follow-up work, not treated as done.

## Building the docs

The user documentation you're reading is built with
[MkDocs Material](https://squidfunk.github.io/mkdocs-material/) from
`userdocs/`:

```
pip install -r tools/docs-requirements.txt
mkdocs serve   # live preview at http://127.0.0.1:8000
mkdocs build   # static site into site/
```

An AmigaGuide version (for reading on-Amiga in MultiView) is generated
from the same `userdocs/` source:

```
make guide     # -> build/amipilot.guide
```

## Building the release archive

```
make dist   # -> build/dist/amipilot.lha, build/dist/amipilot.readme
```

Builds `AmiInspect`, `AmiPilotServer`, the guide, and packages them with `LICENSE` and
`amipilot.readme` into the same `amipilot.lha` this project's releases
ship — including building a real `lha` archiver from source first
(Homebrew's and most Linux distros' `lha` is Lhasa, extract-only, useless
for packaging), pinned to a known-good commit. Override with your own
archiver: `make dist LHA=/path/to/real/lha`.

## Linting

```
make lint   # semgrep --config auto over the C sources
```
