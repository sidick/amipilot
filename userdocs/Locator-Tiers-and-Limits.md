# Locator Tiers and Limits

AmiPilot's stated design principle is **honest best-effort**: read what's
genuinely readable, and say plainly what isn't, rather than guess. This
page is the up-to-date, plain-language version of that promise for 0.1.

## What's classified today

**Plain GadTools gadgets** (`BOOLGADGET`/`STRGADGET`/`PROPGADGET`):

- `STRING_KIND` and `INTEGER_KIND` → `role=string` (both share the same
  underlying `GTYP_STRGADGET` type, and aren't yet distinguished from
  each other — a smaller version of the button/checkbox problem below,
  not yet solved).
- `PROPGADGET` (sliders/scrollers) → `role=slider`.
- `BUTTON_KIND` and `CHECKBOX_KIND` both produce the exact same
  `GTYP_BOOLGADGET` — nothing in the gadget structure itself tells them
  apart. `AmiInspect` distinguishes them using `GT_GetGadgetAttrsA`'s
  documented contract: it only fills in attributes that apply to a
  gadget's real kind, so asking a plain button for the checkbox-only
  `GTCB_Checked` attribute and checking whether it actually got filled in
  is an officially sanctioned way to tell them apart — not a guess. This
  needs `gadtools.library` open; without it (see
  [AmiInspect Reference](AmiInspect-Reference.md)), every `BOOLGADGET`
  reports as `button`.

**BOOPSI/ReAction gadgets** (`CUSTOMGADGET`): AmiPilot reads the real,
live class name via `OCLASS()` — a documented NDK mechanism for exactly
this, not a private hack — and maps known classes to a role:
`button.gadget`, `checkbox.gadget`, `string.gadget`/`getstring.gadget`,
`integer.gadget`, `radiobutton.gadget`, `chooser.gadget`,
`scroller.gadget`, `slider.gadget`, `listbrowser.gadget`. An unrecognised
class still gets its real name reported (`class="..."`,
`role=custom`) rather than a blank field.

## Documented gaps

These are permanent, stated limits of what a structural walk can see —
not bugs waiting to be fixed:

**A `PLACETEXT_IN` button's label reads empty.** GadTools only populates
the classic `GadgetText` field for labels placed
`PLACETEXT_LEFT`/`RIGHT`/`ABOVE`/`BELOW`. A button using `PLACETEXT_IN`
(text drawn inside the button's own imagery — the common case) bakes that
text into rendered graphics instead, so there is genuinely nothing to
read back at this tier.

**A `window.class` window's `layout.gadget` children are invisible.** A
ReAction window built from `window.class` + `layout.gadget` attaches
exactly one gadget to the window's own gadget list — the top-level layout
object itself, correctly identified by class name — not its individual
button/string/checkbox children. There is no documented, public API to
enumerate a `layout.gadget`'s children on classic AmigaOS 3.x: the
methods that would do it (`LM_ADDCHILD`/`LM_REMOVECHILD`/`LM_MODIFYCHILD`)
are OS4-only. Seeing those children would require reading
`layout.gadget`'s private, undocumented internal data — the kind of
version-fragile reverse-engineering this project deliberately doesn't do.
If you need to drive a specific ReAction application's nested gadgets
today, you'll need to know its `GA_ID`s by other means (its source, or a
per-application quirk profile in a future release) rather than discovering
them via `AmiInspect`.

**Custom-rendered UIs are invisible.** Anything an application draws
directly into a bitmap rather than building from real gadget structures
has nothing for a structural walker to find. This applies to games and
any hand-rolled rendering, by definition.

## Why this matters for automation, not just inspection

Everything on this page describes what `AmiInspect` can *see*. A future
release's *locator* model (finding a specific gadget to act on, not just
listing everything) inherits exactly these same limits — a gadget
`AmiInspect` can't classify or reach today is a gadget no later automation
verb will be able to target either, until the underlying gap closes. See
the [implementation plan](https://github.com/sidick/amipilot/blob/main/docs/implementation-plan.md#locator-tiers)
for the full tiered locator model this is building toward.
