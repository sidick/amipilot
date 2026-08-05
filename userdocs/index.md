# AmiPilot

**Object-level GUI automation for classic AmigaOS.**

Automation on the Amiga has always been one of two things: coordinate-level
input injection (works on anything, breaks on any layout change, verifies
nothing), or ARexx (semantic, but only reaches what an application chose
to export). AmiPilot is the missing piece between them — find a window by
title, find a gadget by ID, label, or role, read its state back, and
assert on it **semantically, not by pixels**.

0.1 ships the read side of that: `AmiInspect`, a standalone Shell command
that walks any window's gadget tree and prints its roles, labels, classes,
IDs, positions, and states — the platform's first UIA-Inspect / browser
element-picker equivalent. No host machine, server, or session required;
it runs standing at the machine itself.

It supports **AmiGUI-generated interfaces as first-class citizens** in the
eventual locator model (a later phase — see the
[implementation plan](https://github.com/sidick/amipilot/blob/main/docs/implementation-plan.md))
and makes **honest best efforts at everything else**: plain GadTools
gadgets and BOOPSI/ReAction class instances are both readable today, with
documented limits stated up front rather than silent gaps — see
[Locator Tiers and Limits](Locator-Tiers-and-Limits.md).

## Where to start

- **New user?** Read [Installation](Installation.md), then
  [Getting Started](Getting-Started.md) for a first inspection.
- **Using `AmiInspect` day to day?** See the
  [AmiInspect Reference](AmiInspect-Reference.md).
- **Wondering what it can and can't see?** See
  [Locator Tiers and Limits](Locator-Tiers-and-Limits.md).

## Documentation

| Page | What it covers |
|------|-----------------|
| [Installation](Installation.md) | Requirements, copying `AmiInspect` into place |
| [Getting Started](Getting-Started.md) | Your first `AmiInspect` dump |
| [AmiInspect Reference](AmiInspect-Reference.md) | Command-line template, output format, exit codes |
| [Locator Tiers and Limits](Locator-Tiers-and-Limits.md) | What's classified today, and the honest, documented gaps |
| [Troubleshooting and FAQ](Troubleshooting-and-FAQ.md) | Common problems and questions |
| [Building and Testing](Building-and-Testing.md) | Cross-building `AmiInspect` and running the on-target conformance check |
| [Changelog](Changelog.md) | What landed in each release |

Developer-facing design documents (the full phase plan, protocol notes,
and the reasoning behind specific implementation choices) live in the
repository under
[`docs/`](https://github.com/sidick/amipilot/tree/main/docs), and
day-to-day build/architecture notes for contributors live in
[`CLAUDE.md`](https://github.com/sidick/amipilot/blob/main/CLAUDE.md).

## AI-assisted development

Be aware: **AmiPilot was written largely by an AI coding agent**
(Anthropic's Claude, via Claude Code), working under human direction,
review, and on-target testing. Every feature in this release was verified
on-target under the [Copperline](https://copperline.dev) emulator against
real AmigaOS 3.2.3 — including catching and fixing a genuine crash bug
found only by that testing — before being considered done. See
[Building and Testing](Building-and-Testing.md). The entire source is
BSD-licensed and open for review.

## License

BSD 2-Clause. Copyright © 2026 Simon Dick.
