# Troubleshooting and FAQ

## `AmiInspect` fails immediately with "requires intuition.library V37"

`intuition.library` V37 (AmigaOS 2.04) is `AmiInspect`'s hard floor — see
[Installation](Installation.md). There's no way around this short of an
OS upgrade; nothing about `AmiInspect` itself can lower it, since the
BOOPSI/commodities-era Intuition it walks simply doesn't exist before
V37.

## A gadget's `label` is blank when I know the application set one

Two specific, documented cases genuinely have no readable label at this
tier — see [Locator Tiers and Limits](Locator-Tiers-and-Limits.md):

- A GadTools `BUTTON_KIND` gadget using `PLACETEXT_IN` (the common case)
  bakes its text into rendered imagery instead of the field `AmiInspect`
  reads.
- A `window.class`/`layout.gadget` window's individual gadgets aren't
  reachable at all yet — only the top-level layout object is, so you
  won't see their labels (or anything else about them) in the tree.

If neither of those applies and you're still seeing an unexpected blank
label, that's worth filing as an issue.

## A checkbox reports as `role=button`

This means `gadtools.library` wasn't open when `AmiInspect` ran (it opens
it itself, opportunistically, so this would mean the library is
genuinely unavailable on your system) — without it, `AmiInspect` can't
tell a GadTools `BUTTON_KIND` from a `CHECKBOX_KIND`, since they're
structurally identical. See
[Locator Tiers and Limits](Locator-Tiers-and-Limits.md).

## `AmiInspect WINDOW=...` finds nothing, but the window is clearly open

- `WINDOW=` matches a **substring** of the title, case-sensitively. Check
  capitalization and spelling.
- It searches every window on every public screen — but not windows on a
  private/custom screen your Shell session can't see, and not requesters
  (which aren't windows).
- A window with no title at all (`(untitled)` in `AmiInspect`'s own
  output when it's the active window) can never match a `WINDOW=`
  substring search — omit `WINDOW=` to fall back to the active window
  instead.

## Where do I report a bug or ask a question?

[github.com/sidick/amipilot/issues](https://github.com/sidick/amipilot/issues).
