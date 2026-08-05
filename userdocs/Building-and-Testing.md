# Building and Testing

This page is for building AmiPilot from source and running its on-target
conformance check — not needed if you're just using a released
`AmiInspect` binary (once one exists — see
[Installation](Installation.md)).

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
make fixtures   # + the conformance fixtures (gadtools-app, classact-app)
```

## On-target testing under Copperline

AmiPilot's conformance check runs the actual compiled `AmiInspect` and its
two fixture applications under the
[Copperline](https://copperline.dev) Amiga emulator — not Amiberry;
Copperline's headless, deterministic `--control` protocol is a much
better fit for scripted, repeatable verification than driving an
interactive emulator's GUI by hand.

```
make test-target
```

This boots each fixture headlessly, runs `AmiInspect` against it, and
asserts the exact classification output confirmed correct during
development — it's proven to actually catch a regression, not just
written to look like it does: a real crash bug (a gadget-type bitmask
check that misidentified plain GadTools gadgets as BOOPSI objects) was
deliberately reintroduced during 0.1 development, and this check caught
it before being reverted.

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

## Linting

```
make lint   # semgrep --config auto over the C sources
```
