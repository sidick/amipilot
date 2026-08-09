# host

The host-side Python client for AmiPilot: talks server/WIRE.md's line
protocol over any byte-stream transport (a real serial cable, or
Copperline's `[serial] mode = "tcp"` bridge under an emulator; TCP
transport itself is phase 0.4).

Stdlib-only by design, matching the wire's own no-dependency spirit
(`docs/implementation-plan.md`, "Protocol and client") -- nothing here
needs installing beyond Python 3.9+.

## Current state (1.0)

- **`amipilot.wire`** -- `WireClient`: transport-level framing (strict
  by-byte-count reads, `VERSION` handshake with protocol pinning). The
  layer everything else is built on.
- **`amipilot.model`** -- parses the `TREE` text format (identical to
  `AmiInspect`'s own output, see `server/WIRE.md`) into `Window`/
  `Gadget` dataclasses.
- **`amipilot.client`** -- `Amipilot`, the object API test code
  actually imports: `tree()`, `click()`, `type()`, `get_text()`,
  `manifest()`, `drag()`, `wait_for()`, `launch()`, `wb_launch()`,
  `mui_command()`, the file API (`fs_list()`/`fs_get()`/`fs_put()`
  etc.), `window_move()`/`window_resize()`, `screenshot()`, `quit()`,
  plus `@name`- and
  tier-2 `ROLE=`/`LABEL=`/`INDEX=`-locator variants
  (`click_by_name()`, `click_by_role()`, etc.). Non-OK RCs raise typed
  exceptions (`NotFound`/`CommandError`/`ActionFailed`/`Timeout`)
  rather than requiring the caller to check codes by hand. Matches the
  verb set `AmiPilotServer` currently implements -- see
  `server/README.md`.
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
- **`amipilot.screenshot`** (1.0) -- the host half of `SCREENSHOT`'s
  "wire stays simple, host does the rendering" split: decodes a raw
  capture (classic planar or a genuine Picasso96 pixel format,
  including the 16-bit `PC`-suffix byte-swap pitfall) and writes PNG
  and IFF ILBM, stdlib-only, no Pillow. `Amipilot.screenshot()` is
  the client entry point; the byte-exact decode/encode logic has its
  own unit tests against synthetic captures.
- **The MUI-ARexx bridge tier** (phase 0.5 -- `Amipilot.mui_command()`):
  `MUIREXX <app-base> [TIMEOUT=<n>] <command...>`'s host wrapper --
  sends `command` verbatim to a MUI application's own ARexx port and
  returns its result text. A genuinely different mechanism from
  `click()`/`type()`/`get_text()` (no structural walk), and an honest
  one: MUI's own built-in ARexx support is a small, universal command
  set (`quit`/`hide`/`show`/`activate`/`deactivate`/`info`/`help`), not
  a generic widget-value accessor -- confirmed live against AmigaOS
  3.2's own MUI-Demo, which registers no application-specific commands
  at all. See `server/README.md`'s own section for the full design
  rationale and `userdocs/ARexx-Reference.md#driving-mui-applications`
  for the user-facing story.
- Verified end to end against a real guest under Copperline
  (`tests/copperline/run.sh`'s wire check, and `amipilot dump` run by
  hand against the same session): connect, handshake, `TREE`/`TYPE`/
  `GETTEXT`/`CLICK` all round-trip correctly, RC-5 (not found) surfaces
  as a clean CLI error. The golden-tree check (`run_golden_check`) is
  verified the same way, against real checked-in golden files for
  both fixtures. `mui_command()` is verified live too
  (`run_mui_check`, skips cleanly when MUI isn't installed on the
  configured Workbench volume -- it's a third-party archive, not part
  of any standard install) against real MUI-Demo: `info title`, a
  not-found app base, an unrecognised command, and `quit` genuinely
  closing the target's window, not just the wire round trip
  succeeding.

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
