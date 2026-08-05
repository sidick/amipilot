# Changelog

AmiPilot is developed phase by phase; each one is validated on-target
under Copperline against real AmigaOS before being considered done. This
page summarizes what landed in each release, in user-facing terms — see
the repository's
[`docs/implementation-plan.md`](https://github.com/sidick/amipilot/blob/main/docs/implementation-plan.md)
for the full engineering detail and phase sequencing behind each one.

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

**Known gaps, tracked as real follow-up work, not silently accepted:**

- `AmiInspect` doesn't yet embed a `$VER:` cookie.
- `STRING_KIND` and `INTEGER_KIND` GadTools gadgets aren't distinguished
  from each other (both report as `string`).
- No pre-built binary is attached to this release yet — source only; see
  [Installation](Installation.md).
- No public CI on-target run yet — `make test-target` needs a
  machine-specific Workbench install (see
  [Building and Testing](Building-and-Testing.md)) that CI doesn't have.
