# Changelog

AmiPilot is developed phase by phase; each one is validated on-target
under Copperline against real AmigaOS before being considered done. This
page summarizes what landed in each release, in user-facing terms — see
the repository's
[`docs/implementation-plan.md`](https://github.com/sidick/amipilot/blob/main/docs/implementation-plan.md)
for the full engineering detail and phase sequencing behind each one.

## v0.2 — 2026-08-05

The act side of object-level GUI automation: a server commodity, driven
by ARexx, with no host machine involved.

- **`AmiPilotServer`**: a commodity hosting the action engine and the
  `intuition-model` walker behind a genuine public ARexx port. See the
  [ARexx Reference](ARexx-Reference.md).
- **`TREE`/`CLICK`/`TYPE`/`GETTEXT`/`QUIT`** — locate a window by title,
  click a gadget by `GA_ID` through a real `input.device` event (the
  documented `IECLASS_NEWPOINTERPOS`/`IESUBCLASS_PIXEL` mechanism, not a
  coordinate hack), type text into it via genuine `IECLASS_RAWKEY`
  events paced to approximate human typing, and read state back —
  proven end to end by driving `AmiPilotServer`'s own test fixture from
  a real ARexx script: type into a field, read the value back, click a
  button, confirm the window closed.
- `AmiInspect`'s gadget-tree output gains a `value=` field for string
  and integer gadgets — their live editable contents, not just their
  label. See the updated [AmiInspect Reference](AmiInspect-Reference.md).
- BOOPSI/ReAction gadget geometry (`GA_Left`/`GA_Top`/`GA_Width`/
  `GA_Height`) is now read correctly, including the classic
  `GFLG_RELWIDTH`/`RELHEIGHT`/`RELRIGHT`/`RELBOTTOM` convention (a
  gadget's size/position stored as an offset from its window's own
  dimensions) — needed for `CLICK`/`TYPE` to land on a BOOPSI gadget at
  all, not just for `AmiInspect` to report sane numbers.
- Both `AmiInspect` and `AmiPilotServer` now embed a standard `$VER:`
  cookie — see [Installation](Installation.md#checking-which-version-you-have).
- `amipilot.lha` now ships both binaries — see
  [Installation](Installation.md).

**Known gaps, tracked as real follow-up work, not silently accepted:**

- `STRING_KIND` and `INTEGER_KIND` GadTools gadgets still aren't
  distinguished from each other (both report as `string`) — carried
  over from 0.1.
- No wire protocol yet — ARexx only reaches scripts running on the same
  Amiga. Serial.device and a host Python client are phase 0.3 scope.
- No wait/expectation primitives yet (`click` that waits for an expected
  change, timeouts) — a script has to add its own `Wait`/polling for now.
- No public CI on-target run yet, same reason as 0.1: `make test-target`
  needs a machine-specific Workbench install CI doesn't have.

## v0.1 — 2026-08-05

First release: the read side of object-level GUI automation.

- `intuition-model`: a reusable Intuition/BOOPSI walker library, reading
  windows and gadgets under strict `LockIBase()` discipline (brief holds,
  copy-out, no live pointers handed out, no patching or `SetFunction()`
  anywhere).
- `AmiInspect`: a standalone Shell command that prints any window's
  gadget tree by role, label, class, ID, position, and state. See the
  [AmiInspect Reference](AmiInspect-Reference.md).
- Plain GadTools role classification, including officially-sanctioned
  `GT_GetGadgetAttrsA` kind-probing to distinguish a checkbox from a
  button (both produce the same underlying gadget type).
- BOOPSI/ReAction class reading via `OCLASS()` — a documented NDK
  mechanism, not a hack — correctly identifying real class names and
  mapping known ones to roles.
- Verified against two purpose-built conformance fixtures and a real,
  unmodified stock AmigaOS Prefs editor (`ScreenMode`) — not just
  software built for this project.
- An automated on-target regression check (`make test-target`, headless
  under Copperline) — see [Building and Testing](Building-and-Testing.md).
- Documented, permanent limits rather than silent gaps: `PLACETEXT_IN`
  button labels and `layout.gadget`-nested gadgets are both genuinely
  unreadable at this tier — see
  [Locator Tiers and Limits](Locator-Tiers-and-Limits.md).
- `amipilot.lha` — a pre-built binary release archive (`AmiInspect` +
  license + this documentation as an AmigaGuide) — see
  [Installation](Installation.md).

**Known gaps, tracked as real follow-up work, not silently accepted:**

- `AmiInspect` doesn't yet embed a `$VER:` cookie.
- `STRING_KIND` and `INTEGER_KIND` GadTools gadgets aren't distinguished
  from each other (both report as `string`).
- No public CI on-target run yet — `make test-target` needs a
  machine-specific Workbench install (see
  [Building and Testing](Building-and-Testing.md)) that CI doesn't have.
