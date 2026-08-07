# manifest

The manifest-ID locator contract: the versioned specification for the
machine-readable manifest an application publishes mapping logical names
(`connect_button`) to window/gadget IDs — tier 1 of AmiPilot's locator
model, and the tier with zero runtime cost and zero fragility.

- **[`SPEC.md`](SPEC.md)** — the contract itself, format version 1:
  file format, resolution semantics, naming/shipping conventions, and
  the versioning policy.
- **Real examples** live with the fixtures they describe, exactly where
  a shipping application would put its own:
  [`fixtures/gadtools-app/GTApp.manifest`](../fixtures/gadtools-app/GTApp.manifest)
  (the full shape) and
  [`fixtures/classact-app/CAApp.manifest`](../fixtures/classact-app/CAApp.manifest)
  (the honest-limits shape: a window whose gadgets are structurally
  unreachable names no gadgets).
- **[Quirk profiles](SPEC.md#quirk-profiles-the-same-format-for-apps-you-dont-control)**
  — the same format, same parser, no new machinery, but authored by a
  user or the community for a third-party application rather than by
  the app's own developer, with a documented convention for recording
  known behavioral oddities alongside the locator data.

Consumed by `AmiPilotServer`'s ARexx port (`MANIFEST` command + `@name`
locators — see `userdocs/ARexx-Reference.md`) and the host Python
client (`client.py`'s `@name` methods — see `host/README.md`).
