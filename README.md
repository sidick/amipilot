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

**v1.1 — closing out 1.0's own remaining gaps, plus interactive
discovery.**
What shipped in v0.1 (`intuition-model`, the standalone `AmiInspect`
Shell command) is still there, but this is no longer a read-only
inspector: `AmiPilotServer`, an on-Amiga commodity, actually drives a
GUI with genuine synthesized `input.device` events -- click, type,
drag, move/resize windows, work menus -- reachable both from ARexx and
over the wire (serial or TCP) via the host Python client, so a test
suite on a different machine can launch, click through, assert on, and
screenshot real AmigaOS software end to end. Verified against both
Copperline and Amiberry, including a growing set of real, OS-shipped
stock applications (not just purpose-built fixtures), MUI's own
ARexx-driven apps, and real Picasso96/RTG hardware emulation. 1.1
closes every gap v1.0 itself named as open -- requesters, pointer-only
menu items, `layout.gadget`-nested children -- and adds `PICK`, a
genuinely interactive point-at-a-gadget-get-its-locator discovery
mode. See the [Changelog](userdocs/Changelog.md) for what's in each
release and its known gaps, `CLAUDE.md`'s "Current state" section for
what's real on `main` right now, and the implementation plan's
"Phases" section for how each phase built up to 1.0.

## Repository layout

```
intuition-model/  the reusable Intuition walker/role/reader library
amiinspect/       standalone on-Amiga Shell command (phase 0.1)
server/           AmiPilotServer: the commodity (action engine, wire
                  transports, launch, file API -- phase 0.2+)
host/             Python client + pytest plugin + amipilot dump CLI
                  (phase 0.3+, installable via `pip install -e host/`)
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
make amiga fixtures server  # build locally
make docker                 # cross-compile inside ghcr.io/sidick/amiga-dev
make test-host               # host-side pytest (host/ editable-installed first)
make test-target             # on-target Copperline conformance check
```

See [Building and Testing](userdocs/Building-and-Testing.md) for the
full breakdown of every build/test target, including the host Python
package and `make test-target`'s own on-target check suite.

Minimum target: AmigaOS 2.04 (V37), plain 68000, no FPU. See the
implementation plan's "Minimum requirements" section for the full
rationale and the recommended/CI-tested configuration.

## License

BSD 2-Clause -- see [`LICENSE`](LICENSE).
