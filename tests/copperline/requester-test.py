#!/usr/bin/env python3
"""Drives AmiPilotServer's WAITFOR REQUESTER condition end to end for
the on-target regression check (tests/copperline/run.sh) -- issue
#52's detection-only slice: a way to know a genuine Intuition
Requester appeared, without addressing its own gadgets.

Target: fixtures/gadtools-app's own "Ask" button (GID_ASK), whose
IDCMP_GADGETUP handler calls a real, blocking AutoRequest() -- a
genuine struct Requester attached to the fixture's own window
(window->FirstRequest non-NULL for its whole duration), not a
simulation. CLICKing it is fire-and-forget: the button's own handler
blocks the guest inside AutoRequest(), so the click's own event
injection still completes and returns immediately (same async
semantics every other CLICK against a blocking handler already
relies on elsewhere in this suite).

Two assertions:
- WAITFOR REQUESTER *before* clicking Ask correctly times out (RC 15)
  -- proving this doesn't false-positive against GTApp's own ordinary,
  requester-free window.
- WAITFOR REQUESTER *after* clicking Ask succeeds once the requester
  is actually up -- proving real detection, not a hypothetical.

Prints one greppable line per stage (run.sh asserts on them) and exits
non-zero on any failure.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "host"))

from amipilot import Amipilot, Timeout  # noqa: E402
from amipilot.wire import WireError  # noqa: E402

WINDOW = "AmiPilot GadTools Fixture"
GID_ASK = 7


def main() -> int:
    hostport = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:1234"
    host, port = hostport.rsplit(":", 1)

    client = Amipilot.connect_with_retry(host, int(port), deadline_seconds=30)
    print(f"HANDSHAKE SERVER={client.info.server_version} "
          f"PROTOCOL={client.info.protocol}")

    client.tree(WINDOW)
    print("WINDOW-FOUND OK")

    try:
        client.wait_for("requester", timeout=2)
        print("NO-REQUESTER-YET FAIL falsepositive")
        return 1
    except Timeout:
        print("NO-REQUESTER-YET PASS")

    client.click(WINDOW, GID_ASK)
    print("ASK-CLICKED OK")

    try:
        client.wait_for("requester", timeout=10)
        print("REQUESTER-DETECTED PASS")
    except Timeout:
        print("REQUESTER-DETECTED FAIL")
        return 1

    client.quit()
    client.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (WireError, TimeoutError) as e:
        print(f"REQUESTER-TEST ERROR: {e}")
        sys.exit(1)
