# Getting Started

This walks through the simplest possible use of `AmiInspect`: dumping the
active window's gadget tree, then targeting a specific window by title.

## Dump the active window

With `AmiInspect` on your `Path` (see [Installation](Installation.md)),
just run it with no arguments from a Shell — it inspects whichever window
is currently active:

```
> AmiInspect
window "AmigaShell" [0,11 640x130]
  gadget id=0 role=custom class="buttongclass" label="" [-17,-9 18x10]
  gadget id=0 role=custom class="buttongclass" label="" [-22,0 24x11]
  gadget id=0 role=custom class="buttongclass" label="" [-45,0 24x11]
  gadget id=0 role=custom class="gadgetclass" label="" [-68,0 24x11]
```

That's a plain Shell window: the four entries are its own window-chrome
gadgets (close, depth, zoom, size) — Intuition itself implements these as
real BOOPSI objects (`buttongclass`/`gadgetclass`), which is why
`AmiInspect` can name them precisely. A Shell window has no application
gadgets of its own, so that's the whole tree.

## Target a specific window

Point it at any window by a substring of its title with `WINDOW=`:

```
> AmiInspect WINDOW=Prefs
window "ScreenMode Preferences" [0,11 636x193]
  gadget id=0 role=custom class="buttongclass" label="" [-22,0 24x11]
  gadget id=0 role=custom class="buttongclass" label="" [-45,0 24x11]
  gadget id=0 role=custom class="gadgetclass" label="" [0,0 0x10]
  gadget id=0 role=unknown class="" label="" [0,0 0x0]
  gadget id=17 role=string class="" label="Width:" [134,102 56x8]
  gadget id=18 role=string class="" label="Height:" [134,118 56x8]
  gadget id=19 role=checkbox class="" label="Default" [204,99 26x12]
  gadget id=20 role=checkbox class="" label="Default" [204,115 26x12]
  gadget id=21 role=checkbox class="" label="AutoScroll:" [128,147 26x12]
  ...
```

That's a real run against the stock `ScreenMode` Prefs editor, unmodified
— `AmiInspect` correctly reads the `Width:`/`Height:` string gadgets and
the `Default`/`Default`/`AutoScroll:` checkboxes by role and label, with
real `GA_ID`s. Anything it can't classify (the mode-list widget, the
`Colors:` text label) is reported honestly as `custom`/`unknown` rather
than guessed — see [Locator Tiers and Limits](Locator-Tiers-and-Limits.md)
for exactly what that means and why.

If no window title matches, `AmiInspect` says so and exits with a warning
return code rather than printing nothing silently:

```
> AmiInspect WINDOW=NoSuchWindow
AmiInspect: no matching window found
```

## Next steps

See the [AmiInspect Reference](AmiInspect-Reference.md) for the full
command-line template and output format, or
[Locator Tiers and Limits](Locator-Tiers-and-Limits.md) for what
`AmiInspect` can and can't see today.

Rather point at the one gadget you care about than read through a
whole tree? Try `AmiInspect PICK` — see [Pick
mode](AmiInspect-Reference.md#pick-mode-pick).

Once you've found the `GA_ID`s you need, drive them with
`AmiPilotServer` — click, type, and read gadget state back from an
ARexx script, no host machine involved. See the
[ARexx Reference](ARexx-Reference.md).
