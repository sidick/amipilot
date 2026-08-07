# host

The host-side Python client for AmiPilot: talks server/WIRE.md's line
protocol over any byte-stream transport (a real serial cable, or
Copperline's `[serial] mode = "tcp"` bridge under an emulator; TCP
transport itself is phase 0.4).

Stdlib-only by design, matching the wire's own no-dependency spirit
(`docs/implementation-plan.md`, "Protocol and client") -- nothing here
needs installing beyond Python 3.9+.

## Current state (phase 0.3, in progress)

- **`amipilot.wire`** -- `WireClient`: transport-level framing (strict
  by-byte-count reads, `VERSION` handshake with protocol pinning). The
  layer everything else is built on.
- **`amipilot.model`** -- parses the `TREE` text format (identical to
  `AmiInspect`'s own output, see `server/WIRE.md`) into `Window`/
  `Gadget` dataclasses.
- **`amipilot.client`** -- `Amipilot`, the object API test code
  actually imports: `tree()`, `click()`, `type()`, `get_text()`,
  `manifest()`, `quit()`, plus `@name`-locator variants
  (`click_by_name()` etc.). Non-OK RCs raise typed exceptions
  (`NotFound`/`CommandError`/`ActionFailed`) rather than requiring the
  caller to check codes by hand. Matches the verb subset
  `AmiPilotServer` currently implements -- see `server/README.md`;
  `windows/list`, `find`, `drag`, `menu-pick`, `wait-for`, launch, and
  the file API are 0.4 scope, not stubbed here ahead of the server
  offering them.
- **`amipilot dump`** (`amipilot.dump`, wired up as the `amipilot`
  console script via `pyproject.toml`) -- the host half of "the
  inspector" (`docs/implementation-plan.md`): connects, prints a
  window's gadget tree (`--format text`, the same shape `AmiInspect`
  prints) or quirk-profile-ready name suggestions (`--format python`).
- **Golden-tree fixtures** (`amipilot.golden`, phase 0.5 --
  `docs/implementation-plan.md`'s "Golden trees": "a saved dump
  doubles as a structural fixture"): `assert_golden(window, path)`
  compares a live `tree()` against a checked-in text snapshot,
  auto-creating it the first time and raising `GoldenMismatch` (with a
  unified diff) on drift thereafter. `amipilot dump <window> --golden
  PATH [--update-golden]` is the CLI form of the same check --
  `Amipilot.assert_tree_matches()` is the one-liner for use inside a
  test. **Locale matters here**: the rendered text includes window/
  screen titles verbatim, which a real (catalog-driven) application's
  own localization can change without any UI change at all -- see
  `tests/copperline/README.md`'s "Golden-tree fixtures and Locale"
  section before treating a golden file as portable across machines
  with different Locale preferences.
- Verified end to end against a real guest under Copperline
  (`tests/copperline/run.sh`'s wire check, and `amipilot dump` run by
  hand against the same session): connect, handshake, `TREE`/`TYPE`/
  `GETTEXT`/`CLICK` all round-trip correctly, RC-5 (not found) surfaces
  as a clean CLI error. The golden-tree check (`run_golden_check`) is
  verified the same way, against real checked-in golden files for
  both fixtures.

- **`amipilot.pytest_plugin`** -- the pytest plugin (auto-registered via
  the `pytest11` entry point once `host/` is installed): the
  session-scoped `amipilot` fixture boots a Copperline config that
  already stages `AmiPilotServer SERIAL` (same `S:User-Startup` staging
  technique `tests/copperline/run.sh` uses), waits for the wire
  transport to answer, and hands the test a connected `Amipilot`. This
  is phase 0.3's actual release gate: "a host pytest clicks a button and
  asserts a label changed, deterministically, under the emulator."
  Options: `--amipilot-config PATH` (or the `amipilot_config` ini
  setting) -- unconfigured tests **skip cleanly**, the same "skip, don't
  fake a pass" contract `make test-target` uses without a real
  Kickstart/Workbench asset. `--amipilot-copperline`/
  `--amipilot-copperline-ctl` (default from `$COPPERLINE`/
  `$COPPERLINE_CTL`, else the bare binary names), `--amipilot-wire-port`
  (default 1234), `--amipilot-boot-timeout` (default 60s), and the
  `amipilot_copperline_args` ini setting for extra Copperline flags
  (model/chipset/chip/fast/etc. -- `--config`/`--serial tcp`/`--control`
  are added automatically).

  ```python
  def test_connect_click_asserts_label(amipilot):
      amipilot.type("GadTools", 2, "aminet.net")
      assert amipilot.get_text("GadTools", 2) == "aminet.net"
      amipilot.click("GadTools", 1)
  ```

## Running the tests

```sh
python3 -m pytest tests -v   # from this directory, or:
make test-host                # from the repo root (installs host/[test] first)
```

The plugin's own boot/skip logic is covered without a real emulator
(monkeypatched subprocesses, `pytest`'s own `pytester` fixture driving
real sub-sessions); `tests/copperline/run.sh` additionally exercises it
live against a real guest under Copperline.

## Installing (for `amipilot dump`)

```sh
pip install -e host/
amipilot dump "My Window Title"
```
