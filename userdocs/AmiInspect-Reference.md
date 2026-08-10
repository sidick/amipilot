# AmiInspect Reference

## Command line

```
AmiInspect [WINDOW=<substring>]
AmiInspect PICK [SCREEN=<substring>]
```

| Argument | Meaning |
|----------|---------|
| `WINDOW` | Optional. A substring to match against window titles, across every screen. The first match wins. Omit it to inspect the active window instead. Ignored (with a warning) if `PICK` is also given. |
| `PICK` | Switch. Interactive "pick mode" (issue #65) instead of a one-shot dump — see below. |
| `SCREEN` | Optional, `PICK` mode only. A substring to match against screen titles (`DefaultTitle`), narrowing which screen's windows are hit-tested. Omit it for the frontmost screen. |

Examples:

```
> AmiInspect
> AmiInspect WINDOW=Prefs
> AmiInspect WINDOW="ScreenMode Preferences"
> AmiInspect PICK
> AmiInspect PICK SCREEN="Second Screen"
```

## Pick mode (`PICK`)

The platform's first genuine element-picker equivalent, standing at
the machine itself — no host or server session at all. Once started,
it polls the live pointer position roughly 5 times a second and
prints the window/gadget under it *only when that changes* (not a
fresh line every poll tick), in the same `window "..." [...]` /
`gadget id=... role=...` shape a one-shot dump prints — the exact
locator material a manifest's `GADGET` record needs. Point at a
gadget, watch its identity appear:

```
> AmiInspect PICK
AmiInspect: pick mode -- move the pointer, Ctrl-C to stop
window "AmiPilot GadTools Fixture" screen="Workbench Screen" [40,0 220x256]
  gadget id=3 role=checkbox class="" label="Enabled" [20,72 26x11]
window "AmiPilot GadTools Fixture" screen="Workbench Screen" [40,0 220x256]
  gadget id=6 role=button class="" label="" [20,144 100x14]
AmiInspect: pick mode stopped
```

A window hit with no gadget under the pointer still prints the window
line alone (chrome/background — not an error; this can genuinely
include a real system gadget, e.g. `id=0 class="gadgetclass"` for the
drag bar/close/depth/size decorations, printed the same as anywhere
else). No window at all under the pointer on the target screen prints
`(no window under the pointer)`. Stop with Ctrl-C.

This is the same mechanism the wire's own `PICK` verb uses
(`server/README.md`'s own PICK section, `Amipilot.pick()` on the host
side) — `AmipHitTest()`/`AmipReadPointerPosition()`
(`intuition-model`), including that function's own live-confirmed
pointer-Y correction. Nothing here needs a host connection or
`AmiPilotServer` running at all.

## Output format

One line for the window, then one line per gadget in walk order (the
window's `FirstGadget` chain):

```
window "<title>" [<left>,<top> <width>x<height>]
  gadget id=<id> role=<role> class="<class>" label="<label>" [<left>,<top> <width>x<height>]
  gadget id=<id> role=string class="<class>" label="<label>" value="<value>" [<left>,<top> <width>x<height>]
```

- **`id`** — the gadget's `GA_ID`. `0` for gadgets that never had one set
  (most window-chrome system gadgets).
- **`role`** — one of `button`, `string`, `integer`, `checkbox`,
  `radio_button`, `cycle`, `slider`, `scroller`, `listview`,
  `listbrowser`, `text`, `menu`, `menu_item`, `custom`, or `unknown`.
  `custom` means a real BOOPSI/ReAction class was identified (see
  `class=`) but isn't mapped to a role yet; `unknown` means neither the
  classic gadget-type flags nor a BOOPSI class could be determined. See
  [Locator Tiers and Limits](Locator-Tiers-and-Limits.md) for exactly
  which kinds map to which role today.
- **`class`** — the real BOOPSI/ReAction class name (`button.gadget`,
  `checkbox.gadget`, `layout.gadget`, ...) for gadgets that are true class
  instances. Empty for classic Intuition/GadTools gadgets, which don't
  carry one.
- **`label`** — the gadget's text, where readable. Empty is not always a
  bug — see [Locator Tiers and Limits](Locator-Tiers-and-Limits.md) for
  the specific, documented cases where a label is genuinely unreadable at
  this tier.
- **`value`** — a string or integer gadget's live editable contents,
  read straight out of its `StringInfo` buffer. Only present for those
  two roles; omitted entirely for everything else (a button has no
  separate "value" from its label, for instance).
- Position/size are in the gadget's own coordinate space (window-relative
  for ordinary gadgets; several window-chrome system gadgets use negative
  offsets from the window's right/bottom edge — that's correct, not a
  bug).

## Drafting a manifest from its output

`AmiInspect`'s `id=`/`role=`/`label=` columns are exactly the raw
material a [manifest](https://github.com/sidick/amipilot/blob/main/manifest/SPEC.md)
needs — a first draft is a direct transcription, not a rewrite:

```
> AmiInspect WINDOW="AmiPilot GadTools Fixture"
window "AmiPilot GadTools Fixture" [0,11 400x150]
  gadget id=1 role=button class="" label="_Connect" [10,10 80x20]
  gadget id=2 role=string class="" label="Host" value="" [10,40 200x20]
  gadget id=3 role=checkbox class="" label="_Enabled" [10,70 100x20]
```

becomes:

```
MANIFEST 1
APP GTApp
WINDOW main "AmiPilot GadTools Fixture"
GADGET connect_button main 1
GADGET host_field main 2
GADGET enabled_checkbox main 3
```

— the window title (or a stable, less brittle substring of it) becomes
the `WINDOW` record's title-substring field, and each `gadget id=<n>`
line becomes one `GADGET <logical-name> <window-name> <n>` record,
with a logical name you choose (lowercase, `[a-z0-9_]+`) based on the
gadget's `label=`/`role=`. Any gadget missing from the tree entirely
(most often a `layout.gadget`'s nested children — see
[Locator Tiers and Limits](Locator-Tiers-and-Limits.md)) can't be
named this way; there's nothing to transcribe.

The host-side equivalent, `amipilot dump <window> --format python`
(see `host/README.md`), automates the tedious part of this by
printing one `# <slug> = <id>` suggestion per gadget straight from a
live `TREE`, generated from the same `label=`/`role=` data —
copy/paste starting material for the `GADGET` records above, not a
finished manifest (it still needs the `MANIFEST`/`APP`/`WINDOW`
header lines and a human decision on naming).

Whether the result ships as the application's own manifest or stays a
standalone file you keep for your own scripts against an app you
don't control, the format is identical — see
[the manifest spec's "Quirk profiles" section](https://github.com/sidick/amipilot/blob/main/manifest/SPEC.md#quirk-profiles-the-same-format-for-apps-you-dont-control)
for the latter.

## Golden trees (catching UI drift, not just clicking)

A saved dump doubles as a structural fixture: "this app's UI still
has this shape" as a one-line assertion, catching an upstream UI
change before a manifest's `GA_ID`s or a quirk profile's locators
start failing in a confusing way. On the host side (`host/README.md`,
`amipilot.golden`):

```sh
amipilot dump "AmiPilot GadTools Fixture" --golden GTApp.golden
# first run: "amipilot dump: wrote GTApp.golden"
# every run after, unchanged: "amipilot dump: GTApp.golden matches"
# after a real UI change: exits 1 with a unified diff on stderr
```

or, inside a test, `Amipilot.assert_tree_matches(window_pattern,
golden_path)` does the same thing as one call. Regenerate deliberately
with `--update-golden` (or `update=True`) once a change is confirmed
intentional — a golden file is meant to fail loudly on drift, not
silently absorb it.

**Locale is part of a golden file's environment.** The saved text
includes the window's title and screen name verbatim, and — for a
real, catalog-driven application — its gadget labels too; any of
these can differ under a different system Locale preference with no
actual UI change involved. A golden file taken on one machine isn't
guaranteed to reproduce on another unless both share the same Locale
(see `tests/copperline/README.md`'s own note on the fixtures this
project ships golden files for).

## Exit codes

| Code | Meaning |
|------|---------|
| `0` (`RETURN_OK`) | A window was found and its tree printed. |
| `5` (`RETURN_WARN`) | No window matched `WINDOW=`. Nothing printed to stdout; a message goes to stderr. |
| `20` (`RETURN_FAIL`) | `intuition.library` V37+ isn't available, argument parsing failed, or the walk ran out of memory. A message goes to stderr. |

## What it needs open

`AmiInspect` always opens `intuition.library` (V37+; fails outright
without it) and opportunistically opens `gadtools.library` (any version)
to distinguish a GadTools checkbox from a plain button — see
[Locator Tiers and Limits](Locator-Tiers-and-Limits.md). Neither is left
open after it exits.
