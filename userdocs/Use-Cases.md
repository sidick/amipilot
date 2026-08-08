# Use Cases

AmiPilot exists because Amiga automation has historically meant one of two
things: coordinate-level input injection (works on anything, breaks on any
layout change, verifies nothing) or ARexx (semantic, but only reaches
what an application chose to export). Everything below is a real, grounded
use of the verbs and tools this project actually ships — not aspirational
marketing copy — with a pointer to the feature that makes it possible.

## Automated regression testing

The core use case: drive a real GUI application the way a user would
(`click()`/`type()`/`get_text()`), assert on its state, and run that as
part of a normal test suite. The [pytest plugin](Wire-Protocol.md#the-host-client)
boots a configured emulator, hands your test a connected client, and tears
down after — `pytest` sees it as an ordinary fixture. `wait_for()`/
`click(expect=...)` (the "Async by design" wait/expectation primitives)
eliminate the classic act-then-check race a naive `click()` followed
immediately by an assertion has against anything that opens a window,
changes a label, or closes asynchronously.

## Catching UI regressions before they ship

[Golden-tree fixtures](AmiInspect-Reference.md) (`amipilot dump --golden`,
`Amipilot.assert_tree_matches()`) snapshot a window's whole structural
shape — every gadget's role, label, position — as a checked-in fixture.
A later build that accidentally moves a gadget, drops a label, or changes
a control's type fails the comparison immediately, the same value a
snapshot test gives a web/mobile team, but for a real AmigaOS window
walked structurally rather than screenshotted pixel-for-pixel (see
[Locator Tiers and Limits](Locator-Tiers-and-Limits.md) for what "walked
structurally" can and can't see).

## CI for Amiga software

Because the whole stack is scriptable (Copperline's own `--control` JSON-RPC
server, headless, frame-accurate `run_until` waits) a build pipeline can
boot a real AmigaOS environment, launch the software under test
(`LAUNCH`/`WBLAUNCH`), drive it, and assert on the outcome — gating a merge
on **real, observed behavior**, not just "it compiled." No physical
machine, no human clicking through a manual test plan before every
release.

## Cross-version and cross-configuration compatibility matrices

The same test suite can run unmodified against different Copperline/
Amiberry configs — AmigaOS 2.04 through 3.2, 68000 through 68020+, plain
chip RAM through an expanded fast-RAM setup — surfacing "works on 3.1,
breaks on 2.04" or "needs more than the default stack" long before a user
does. This project's own `make test-target` does exactly this against its
own fixtures already.

## Automated screenshots for documentation

`SCREENSHOT` (raw capture, host-side `amipilot.screenshot` turning it into
real `.png`/`.iff` files — see [Wire Protocol](Wire-Protocol.md#screenshot))
means a scripted walkthrough — open this window, fill in this field, click
this button — can capture a real, correctly-rendered image at every step
without a human running a screen-grab tool by hand. A documentation build
can regenerate every manual screenshot from a real running application
whenever the UI changes, instead of manually re-capturing and re-cropping
each one.

## Bug reproduction and triage

A scripted repro (`click()`/`type()` sequence plus a `SCREENSHOT` at the
point of failure) turns "I can't reproduce this" into an attachable,
re-runnable artifact — the exact sequence of actions plus a real captured
image of the broken state, not a verbal description of what someone saw.

## Exploratory GUI inspection

`AmiInspect`/`amipilot dump` are usable standing at the machine itself,
with no host session at all — the platform's first UIA-Inspect/browser-
devtools-element-picker equivalent for classic Intuition. Useful on its
own for understanding an unfamiliar application's structure before writing
any automation against it, or just to answer "what is this control,
actually" when working on a foreign codebase.

## Localization and catalog testing

Because golden-tree comparisons and `get_text()` both read live label
text, a suite can run once per locale (wherever the target
`locale.library` catalog is switched) and confirm translated strings
actually appear where expected — catching a missing catalog entry or a
truncated translation the same way a screenshot-diff localization test
would, without needing pixel comparison at all (see
`tests/copperline/README.md`'s own note on locale variance in golden
trees for the real caveat this surfaced).

## Workbench-launch and tooltype behavior testing

`WBLAUNCH` (a genuine `WBStartup`/`WBArg` message, not a Shell start) lets
a test verify an application's own icon/tooltype handling — does it
respect a `PORT=` override, does a project-icon launch pick the right
default tool — without a human double-clicking an icon by hand for every
test run.

## Self-contained test runs on bare hardware

The file API (`FSPUT`/`FSGET`/`FSLIST`/`FSMKDIR`/`FSDELETE`) means a test
against real hardware with no shared drive or host-mounted filesystem can
still be entirely self-contained: connect, stage input fixtures, launch,
drive, harvest output/log files, clean up, disconnect — the wire is the
only road in, and it's enough.

## Remote and real-hardware test farms

Since the wire protocol is transport-agnostic (serial.device or TCP,
listen-mode today) and carries the exact same verb grammar either way, a
CI runner can drive an actual physical Amiga over a real serial cable or
network link — not just an emulator. The same test suite that runs against
Copperline in CI can run against a real A1200 on a shelf somewhere,
unchanged.

## Long-running reliability (soak) testing

Because actions compose with `wait_for()`/`expect=` rather than fixed
sleeps, a repeated interaction loop (open, interact, close, repeat
hundreds of times) can run unattended for hours, surfacing a slow memory
leak, a resource exhaustion crash, or a rare race that a single manual
test pass would never catch.

## Teaching and onboarding

A scripted, reproducible walkthrough of an application's UI — driven the
same way a real user would use it, with real screenshots captured along
the way — makes a much better onboarding artifact than a static manual:
it's runnable, it stays accurate as long as the test suite does, and it
can be regenerated on demand.

## Preservation and archival documentation

For software whose original hardware or media is degrading, a scripted
session that drives the real application and captures real screenshots
(`SCREENSHOT`, saved as both PNG for modern viewing and IFF ILBM for
archival fidelity to the original platform) produces a documented,
reproducible record of exactly how the software actually behaved — done
systematically, once, rather than by hand and only when someone happens
to remember to do it before the hardware fails entirely.

## MUI application testing

`MUIREXX` bridges to a MUI application's own built-in ARexx port (`quit`/
`hide`/`show`/`activate`/`deactivate`/`info`/`help`) — enough to script a
MUI app's lifecycle (start it, confirm it's alive via `info`, quit it
cleanly) as part of a larger automated flow, without pretending to a
generic widget-value accessor MUI's built-in ARexx support doesn't
actually provide (see `server/README.md`'s own section for the honest
limits here).
