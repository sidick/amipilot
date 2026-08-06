#!/usr/bin/env python3
"""Drives AmiPilotServer's LAUNCH verb (phase 0.4) end to end for the
on-target regression check (tests/copperline/run.sh).

Unlike every other check, THIS one's smoke.script stages only
AmiPilotServer itself -- no fixture pre-launched via User-Startup.
Connects over the wire, confirms the fixture's window genuinely isn't
there yet, sends LAUNCH with a non-default STACK to start it *over the
wire*, polls for the window to appear, then does a real TYPE/GETTEXT/
CLICK round trip to prove the launched process is fully functional
(not just that it didn't immediately crash) before confirming it quit.

Prints one greppable line per stage (run.sh asserts on them) and exits
non-zero on any failure.
"""

import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "host"))

from amipilot import Amipilot, NotFound  # noqa: E402
from amipilot.wire import WireError  # noqa: E402


def main() -> int:
    hostport = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:1234"
    host, port = hostport.rsplit(":", 1)

    client = Amipilot.connect_with_retry(host, int(port), deadline_seconds=30)
    print(f"HANDSHAKE SERVER={client.info.server_version} "
          f"PROTOCOL={client.info.protocol}")

    try:
        client.tree("GadTools")
        print("PRELAUNCH-CHECK FAIL window already present")
        return 1
    except NotFound:
        print("PRELAUNCH-CHECK PASS no window yet")

    client.launch("SRC:build/fixtures/GTApp", stack=8192)
    print("LAUNCH-SENT OK")

    deadline = time.time() + 20
    window = None
    while time.time() < deadline:
        try:
            window = client.tree("GadTools")
            break
        except NotFound:
            time.sleep(0.5)
    if window is None:
        print("WINDOW-APPEARED FAIL")
        return 1
    print(f"WINDOW-APPEARED PASS title={window.title}")

    client.type("GadTools", 2, "launched via wire")
    print(f"GETTEXT RESULT={client.get_text('GadTools', 2)}")

    client.click("GadTools", 1)
    try:
        client.tree("GadTools")
        print("WINDOW-GONE FAIL")
        return 1
    except NotFound:
        print("WINDOW-GONE PASS")

    client.quit()
    client.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (WireError, TimeoutError) as e:
        print(f"LAUNCH-TEST ERROR: {e}")
        sys.exit(1)
