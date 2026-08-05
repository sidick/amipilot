# AmiPilot

Object-level GUI automation for classic AmigaOS: an on-Amiga automation
server plus a host-side Python client that together drive real Amiga GUIs
the way Selenium/AutoIt/UIA drive modern ones -- find a window by title,
find a gadget by stable ID, label, or role, act on it with genuinely
synthesised input, read state back, and assert semantically, not by pixels.

**Full user documentation: [sidick.github.io/amipilot](https://sidick.github.io/amipilot/)**
(source in [`userdocs/`](userdocs/), built as a MkDocs site -- see
[Building and Testing](userdocs/Building-and-Testing.md) to build it
locally, or read the plain Markdown directly).

See [`docs/implementation-plan.md`](docs/implementation-plan.md) for the
full design, phase sequencing, and minimum requirements, and
[`CLAUDE.md`](CLAUDE.md) for contributor build/architecture notes.

## Status

**v0.1 released** -- `intuition-model` (the Intuition/BOOPSI walker
library) and `AmiInspect` (the standalone Shell command that prints any
window's gadget tree), verified against real AmigaOS 3.2.3. See the
[Changelog](userdocs/Changelog.md) for what's in it and its known gaps,
and the implementation plan's "Phases" section for what comes next.

## Repository layout

```
intuition-model/  the reusable Intuition walker/role/reader library
amiinspect/       standalone on-Amiga Shell command (phase 0.1)
server/           the AmiPilot server commodity (phase 0.2+)
host/             Python client + pytest plugin (phase 0.3+)
manifest/         the manifest-ID locator contract (phase 0.3)
fixtures/         conformance test apps + their manifests
tests/            host-side pytest suites (CI entry point, phase 0.3+);
                  tests/copperline/ has the on-target regression check
docs/             implementation plan and design docs (contributor-facing)
userdocs/         user documentation (built as the MkDocs site + AmigaGuide)
tools/            docs toolchain (docs2guide.py, pinned requirements)
```

## Building

Requires Bebbo's m68k-amigaos GCC. Either install it locally (set
`PATH` so `m68k-amigaos-gcc` resolves) or use the shared container image:

```sh
make amiga    # build locally
make docker   # build inside ghcr.io/sidick/amiga-dev
```

Minimum target: AmigaOS 2.04 (V37), plain 68000, no FPU. See the
implementation plan's "Minimum requirements" section for the full
rationale and the recommended/CI-tested configuration.

## License

BSD 2-Clause -- see [`LICENSE`](LICENSE).
