#!/usr/bin/env python3
"""Drives AmiPilotServer's PICK verb end to end for the on-target
regression check (tests/copperline/run.sh) -- issue #65's interactive
"pick mode" discovery.

Target: fixtures/gadtools-app's own window ("AmiPilot GadTools
Fixture"), the golden-tree-tested fixture, so GID_ENABLED's exact
geometry is already checked-in and stable (GTApp.golden).

This test does NOT drive the pointer via Copperline's own
input.mouse_to control channel -- it reuses AmiPilotServer's own
already-verified CLICK/WINDOWMOVE actions to position the REAL live
pointer at known locations first (a real input.device click/drag,
exactly what a human would do), then calls PICK and checks the
reported locator against what those actions are known to target. This
keeps the check deterministic without needing a second control path
into Copperline, and stays consistent with the project's own "act with
real input, not shortcuts" principle.

Prints one greppable line per stage (run.sh asserts on them) and exits
non-zero on any failure.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "host"))

from amipilot import Amipilot, NotFound  # noqa: E402
from amipilot.wire import WireError  # noqa: E402

TARGET = "AmiPilot GadTools Fixture"
# GID_ENABLED (checkbox) -- deliberately NOT GID_CONNECT (button), whose
# own fixture header says it quits the app on press; the pointer needs
# to stay parked on a real, still-open gadget for PICK to find.
GID_ENABLED = 3


def main() -> int:
    hostport = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:1234"
    host, port = hostport.rsplit(":", 1)

    client = Amipilot.connect_with_retry(host, int(port), deadline_seconds=30)
    print(f"HANDSHAKE SERVER={client.info.server_version} "
          f"PROTOCOL={client.info.protocol}")

    # 1. A real CLICK on GID_ENABLED parks the live pointer at that
    #    gadget's own center (AmipClickAt() moves, then clicks, then
    #    doesn't move away) -- PICK should now report exactly that
    #    gadget.
    client.click(TARGET, GID_ENABLED)
    picked = client.pick()
    ok = (
        picked.title == TARGET
        and len(picked.gadgets) == 1
        and picked.gadgets[0].gadget_id == GID_ENABLED
        and picked.gadgets[0].role == "checkbox"
    )
    print(f"PICK-GADGET {'PASS' if ok else 'FAIL'} "
          f"title={picked.title!r} gadgets={[(g.gadget_id, g.role) for g in picked.gadgets]}")
    if not ok:
        return 1

    # 2. SCREEN= narrowed to the same (only) screen must give the
    #    identical result -- proves the parameter actually threads
    #    through to the server's own AmipFindScreen() call, not just
    #    that the bare/default form works.
    screens = client.screens()
    this_screen = next(s for s in screens if s.frontmost)
    picked_scoped = client.pick(screen=this_screen.title)
    ok = (
        len(picked_scoped.gadgets) == 1
        and picked_scoped.gadgets[0].gadget_id == GID_ENABLED
    )
    print(f"PICK-SCREEN-SCOPED {'PASS' if ok else 'FAIL'} screen={this_screen.title!r}")
    if not ok:
        return 1

    # 3. A small WINDOWMOVE drag (2px, negligible to the window's own
    #    geometry) parks the pointer on the title bar afterward --
    #    chrome, not any application gadget. TREE already reports the
    #    drag bar itself as a real system gadget (GA_ID 0, role=custom,
    #    class="gadgetclass" -- see GTApp.golden), so PICK correctly
    #    finding THAT here, not an application gadget, is the right
    #    result -- not "no gadget at all".
    client.window_move(TARGET, 2, 0)
    picked_chrome = client.pick()
    ok = (
        picked_chrome.title == TARGET
        and len(picked_chrome.gadgets) == 1
        and picked_chrome.gadgets[0].gadget_id == 0
        and picked_chrome.gadgets[0].class_name == "gadgetclass"
    )
    print(f"PICK-CHROME {'PASS' if ok else 'FAIL'} "
          f"title={picked_chrome.title!r} gadgets={picked_chrome.gadgets}")
    if not ok:
        return 1

    # 4. SCREEN= naming a screen that doesn't exist at all is a clean,
    #    deterministic RC 5 -- NotFound -- regardless of where the
    #    live pointer happens to be.
    try:
        client.pick(screen="NoSuchScreenAtAll12345")
        print("PICK-UNKNOWN-SCREEN FAIL accepted")
        return 1
    except NotFound:
        print("PICK-UNKNOWN-SCREEN PASS rejected")

    client.quit()
    client.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (WireError, TimeoutError) as e:
        print(f"PICK-TEST ERROR: {e}")
        sys.exit(1)
