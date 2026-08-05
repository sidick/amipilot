# AmiInspect Reference

## Command line

```
AmiInspect [WINDOW=<substring>]
```

| Argument | Meaning |
|----------|---------|
| `WINDOW` | Optional. A substring to match against window titles, across every screen. The first match wins. Omit it to inspect the active window instead. |

Examples:

```
> AmiInspect
> AmiInspect WINDOW=Prefs
> AmiInspect WINDOW="ScreenMode Preferences"
```

## Output format

One line for the window, then one line per gadget in walk order (the
window's `FirstGadget` chain):

```
window "<title>" [<left>,<top> <width>x<height>]
  gadget id=<id> role=<role> class="<class>" label="<label>" [<left>,<top> <width>x<height>]
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
- Position/size are in the gadget's own coordinate space (window-relative
  for ordinary gadgets; several window-chrome system gadgets use negative
  offsets from the window's right/bottom edge — that's correct, not a
  bug).

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
