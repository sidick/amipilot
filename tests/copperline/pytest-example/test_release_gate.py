"""The phase 0.3 release gate itself (docs/implementation-plan.md):
"a host pytest clicks a button and asserts a label changed,
deterministically, under the emulator" -- driven entirely through the
`amipilot` fixture (host/amipilot/pytest_plugin.py), which boots
Copperline itself from `--amipilot-config` rather than run.sh launching
it by hand as the other on-target checks do.

Run via `tests/copperline/run.sh` (which supplies `--amipilot-config`
from the same `copperline.local.toml` every other check uses, and
stages the fixture + `AmiPilotServer SERIAL` via
`S:User-Startup` before Copperline boots) -- see that script's
`run_pytest_release_gate_check`. Needs `host/` installed
(`pip install -e 'host/[test]'`).
"""

import pytest
from amipilot import NotFound


def test_click_button_and_assert_label_changed(amipilot):
    amipilot.type("GadTools", 2, "hello pytest")
    assert amipilot.get_text("GadTools", 2) == "hello pytest"

    amipilot.click("GadTools", 1)  # Connect -- the fixture quits on this click

    with pytest.raises(NotFound):
        amipilot.tree("GadTools")
