# Locator Tiers and Limits

AmiPilot's stated design principle is **honest best-effort**: read what's
genuinely readable, and say plainly what isn't, rather than guess. This
page is the up-to-date, plain-language version of that promise —
covering both what `AmiInspect` can see and what `AmiPilotServer`'s
`CLICK`/`TYPE`/`GETTEXT` can act on, since a gadget invisible to one is
unreachable by the other.

## Which tier reaches which toolkit, and why

The [implementation plan](https://github.com/sidick/amipilot/blob/main/docs/implementation-plan.md#locator-tiers)
defines four locator tiers: **1** (manifest logical names — or a
community-authored [quirk profile](https://github.com/sidick/amipilot/blob/main/manifest/SPEC.md#quirk-profiles-the-same-format-for-apps-you-dont-control),
same format), **2** (semantic — window pattern + role/`GA_ID`), **3**
(the MUI-ARexx bridge), and **4** (raw coordinates, the fragile floor).
Between 1 and 2 sits a fifth, cooperative variant of tier 1 (issue
#49): a `WHEREGADGET` manifest entry, resolved by querying an ARexx
port the *target application itself* exposes for a gadget's live
geometry, rather than a `GA_ID` — see [ARexx Reference](ARexx-Reference.md#driving-layoutgadget-only-applications).
This table is the honest answer to "which tier actually reaches my
application's UI today":

| UI built from | Reaches | Why |
| --- | --- | --- |
| Plain GadTools gadgets (button/checkbox/string/integer/slider), top-level `BOOPSI`/ReAction gadgets attached directly to a window (`button.gadget`, `string.gadget`, etc., not nested inside a layout) | **Tier 1** with a manifest/quirk profile, or **Tier 2** without one | Fully classified and structurally reachable — see "What's classified today" below. |
| A `window.class` + `layout.gadget` window's nested button/string/checkbox children, where the application implements a `WHERE` port | **Tier 1 (cooperative)** — a `WHEREGADGET` manifest entry | The confirmed `layout.gadget` limit below — invisible to structural walking, so plain tiers 1–3 can't name them at all (not even via a quirk profile, since there's no `GA_ID` to record). The application's own cooperation closes the gap: it reports its own gadgets' geometry live, and AmiPilot still does the actual clicking with real `input.device` input. |
| The same nested children, where the application does **not** implement a `WHERE` port | **Tier 4 only** (raw coordinates) | Nothing changes without the target's own cooperation — a third party can't retrofit a `WHERE` port onto a binary that doesn't offer one, the same way a quirk profile can't retrofit a `GA_ID` onto an unreachable gadget. |
| MUI applications | **Tier 3** (`MUIREXX`) for whatever the app's own ARexx commands cover, **Tier 4** for anything else | `intuition-model`'s class-name walker has no MUI recognition, so Tier 2 doesn't reach MUI gadgets at all — confirmed live that a MUI window attaches only its own single top-level object to `window->FirstGadget`, the same limit `window.class`/`layout.gadget` has. Tier 3 (see [ARexx Reference](ARexx-Reference.md#driving-mui-applications)) drives through the ARexx port every MUI app carries automatically, but MUI's own *built-in* support there is a small, universal set (`quit`/`hide`/`show`/`activate`/`deactivate`/`info`/`help`) — confirmed against AmigaOS 3.2's own MUI-Demo, which registers zero commands beyond it. Genuine per-widget interaction needs the target application to have added its own ARexx commands; `MUIREXX` passes them through, it can't invent them. |
| Custom-rendered UIs (games, hand-rolled bitmap rendering) | **Tier 4 only** (raw coordinates) | Out of scope by definition — nothing structural exists for a walker to find, and there's no ARexx port to assume. |

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
`role=custom`) rather than a blank field. A gadget whose `GadgetType`
bits *claim* `GTYP_CUSTOMGADGET` but doesn't actually carry a real
BOOPSI object header (confirmed against a real, OS-shipped stock
application) degrades the same way — `role=custom`, no class or
label — rather than trusting the claim and dereferencing garbage.

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
today, you'll need to know its `GA_ID`s by other means (its source) —
and even then, a quirk profile can't help name them, since they're
invisible to structural walking regardless of who wrote the file down
(see the [quirk profiles section](https://github.com/sidick/amipilot/blob/main/manifest/SPEC.md#quirk-profiles-the-same-format-for-apps-you-dont-control)
of the manifest spec).

This particular limit now has a real escape hatch, though only for
applications that opt into it: a `WHEREGADGET` manifest entry
(format version 2) resolves such a gadget by querying a small,
optional ARexx port the application itself exposes, reporting its own
live geometry — see [Driving layout.gadget-only
applications](ARexx-Reference.md#driving-layoutgadget-only-applications).
It doesn't change what `AmiInspect`/structural walking can *see* (a
`WHEREGADGET`-addressed gadget still never appears in a `TREE`/
`AmiInspect` dump — this is a discovery mechanism cooperating with the
application, not a walker enhancement), only what `CLICK`/`TYPE` can
*act on*. A third party still can't retrofit this onto a binary that
doesn't implement the port, the same honest boundary a quirk profile
already has.

**Custom-rendered UIs are invisible.** Anything an application draws
directly into a bitmap rather than building from real gadget structures
has nothing for a structural walker to find. This applies to games and
any hand-rolled rendering, by definition.

## Why this matters for automation, not just inspection

Everything on this page describes what `AmiInspect` can *see* — and
`AmiPilotServer`'s `CLICK`/`TYPE`/`GETTEXT` (see the
[ARexx Reference](ARexx-Reference.md)) locate their target the exact
same way, by walking the live structure and matching a `GA_ID`. A gadget
`AmiInspect` can't classify or reach is a gadget no automation verb can
target either, until the underlying gap closes — a `layout.gadget`
child, for instance, has no `GA_ID` to `CLICK` by, for the same reason
`AmiInspect` can't list it. See the
[implementation plan](https://github.com/sidick/amipilot/blob/main/docs/implementation-plan.md#locator-tiers)
for the full tiered locator model this is building toward.
