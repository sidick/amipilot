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
- Verified end to end against a real guest under Copperline
  (`tests/copperline/run.sh`'s wire check, and `amipilot dump` run by
  hand against the same session): connect, handshake, `TREE`/`TYPE`/
  `GETTEXT`/`CLICK` all round-trip correctly, RC-5 (not found) surfaces
  as a clean CLI error.

Still to come: the pytest plugin (fixtures that boot an emulator config,
wait for the server, and expose `Amipilot`).

## Running the tests

```sh
python3 -m unittest discover -s tests -v   # from this directory, or:
make test-host                              # from the repo root
```

## Installing (for `amipilot dump`)

```sh
pip install -e host/
amipilot dump "My Window Title"
```
