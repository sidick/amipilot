# Installation

AmiPilot 0.1 has **no installer**: copy one file into place and it works.

## Requirements

`AmiInspect` (the only on-Amiga binary this release ships):

- AmigaOS 2.04 (V37) or later — the floor every API `AmiInspect` calls
  actually needs (commodities-era Intuition, BOOPSI). See the
  [implementation plan](https://github.com/sidick/amipilot/blob/main/docs/implementation-plan.md#minimum-requirements)
  for the full rationale.
- Plain 68000, no FPU. Nothing in this release does float work or needs
  anything newer than a stock CPU.
- No meaningful RAM floor of its own — `AmiInspect` is a small, one-shot
  Shell command with no resident component; the real floor is whatever
  application you're pointing it at.

Recommended / what this release is actually tested against: AmigaOS 3.1
or later, 68020, 2 MB chip + 8 MB fast. `gadtools.library` (any version)
is optional but recommended — without it open, `AmiInspect` can't
distinguish a GadTools checkbox from a plain button (both produce the
same underlying gadget type; see
[Locator Tiers and Limits](Locator-Tiers-and-Limits.md)).

## Installing

Download `amipilot.lha` from the
[GitHub release](https://github.com/sidick/amipilot/releases), extract
it, and copy `AmiInspect` to `C:` (or anywhere on your command `Path`).
The archive also carries this documentation as `amipilot.guide`
(AmigaGuide/MultiView) and the license.

That's it — no reboot, no configuration. Run it from a Shell:

```
> AmiInspect
```

If you'd rather build it yourself than use the released binary, see
[Building and Testing](Building-and-Testing.md) (`make dist` produces the
same archive this release ships).

## Checking which version you have

`AmiInspect` does not yet embed a `$VER:` cookie (a known gap — see
[Changelog](Changelog.md)), so check the release you downloaded it from
instead: [github.com/sidick/amipilot/releases](https://github.com/sidick/amipilot/releases).

## Next steps

Continue with [Getting Started](Getting-Started.md) for a first
inspection walkthrough.
