# AmiPilot

Object-level GUI automation for classic AmigaOS: an on-Amiga automation
server plus a host-side Python client that together drive real Amiga GUIs
the way Selenium/AutoIt/UIA drive modern ones -- find a window by title,
find a gadget by stable ID, label, or role, act on it with genuinely
synthesised input, read state back, and assert semantically, not by pixels.

See [`docs/implementation-plan.md`](docs/implementation-plan.md) for the
full design, phase sequencing, and minimum requirements.

## Status

Early scaffolding. Phase 0.1 (`intuition-model` + `AmiInspect`) is the
current focus -- see the implementation plan's "Phases" section for what
that means and what comes after.

## Repository layout

```
intuition-model/  the reusable Intuition walker/role/reader library
amiinspect/       standalone on-Amiga Shell command (phase 0.1)
server/           the AmiPilot server commodity (phase 0.2+)
host/             Python client + pytest plugin (phase 0.3+)
manifest/         the manifest-ID locator contract (phase 0.3)
fixtures/         conformance test apps + their manifests
tests/            host-side pytest suites (CI entry point, phase 0.3+)
docs/             implementation plan and design docs
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
