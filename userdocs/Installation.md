# Installation

AmiPilot has **no installer**: copy the files into place and they work.

## Requirements

`AmiInspect` and `AmiPilotServer` (the two on-Amiga binaries this
release ships):

- AmigaOS 2.04 (V37) or later — the floor every API either binary calls
  actually needs (commodities-era Intuition, BOOPSI, `input.device`).
  See the
  [implementation plan](https://github.com/sidick/amipilot/blob/main/docs/implementation-plan.md#minimum-requirements)
  for the full rationale.
- Plain 68000, no FPU. Nothing in this release does float work or needs
  anything newer than a stock CPU.
- No meaningful RAM floor of its own — both are small Shell commands
  with no persistent state; the real floor is whatever application
  you're pointing them at.
- `AmiPilotServer` additionally needs `rexxsyslib.library` (it fails
  outright without it — there's no point running with no ARexx port)
  and a resident `RexxMast` for scripts to reach it through `rx`. See
  the [ARexx Reference](ARexx-Reference.md).

Recommended / what this release is actually tested against: AmigaOS 3.1
or later, 68020, 2 MB chip + 8 MB fast. `gadtools.library` (any version)
is optional but recommended for both binaries — without it open, the
walker can't distinguish a GadTools checkbox from a plain button (both
produce the same underlying gadget type; see
[Locator Tiers and Limits](Locator-Tiers-and-Limits.md)). `AmiPilotServer`
also opportunistically opens `keymap.library` — without it, its `TYPE`
command fails outright, but `TREE`/`CLICK`/`GETTEXT` still work.

## Installing

Download `amipilot.lha` from the
[GitHub release](https://github.com/sidick/amipilot/releases), extract
it, and copy `AmiInspect` and `AmiPilotServer` to `C:` (or anywhere on
your command `Path`). The archive also carries this documentation as
`amipilot.guide` (AmigaGuide/MultiView) and the license.

That's it — no reboot, no configuration. Run them from a Shell:

```
> AmiInspect
> Run AmiPilotServer
```

If you'd rather build it yourself than use the released binary, see
[Building and Testing](Building-and-Testing.md) (`make dist` produces the
same archive this release ships).

## Checking which version you have

Both binaries embed a standard AmigaDOS `$VER:` cookie — check it with
the Shell's own `Version` command:

```
> Version AmiInspect
AmiInspect x.y

> Version AmiPilotServer
AmiPilotServer x.y
```

(`x.y` will be the actual release version, e.g. `0.5` — shown
genericized here so this example doesn't go stale every release; see
the [Changelog](Changelog.md) for what's current.)

## Next steps

Continue with [Getting Started](Getting-Started.md) for a first
inspection walkthrough.
