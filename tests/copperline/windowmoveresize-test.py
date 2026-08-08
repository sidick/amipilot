#!/usr/bin/env python3
"""Drives AmiPilotServer's WINDOWMOVE/WINDOWSIZE verbs end to end for
the on-target regression check (tests/copperline/run.sh) -- real
title-bar and sizing-gadget drags, not a simulated move/resize.

Target: fixtures/second-screen-app's own window ("GadTools Fixture
2") -- the only fixture with a real sizing gadget (WA_SizeGadget;
neither fixtures/gadtools-app nor fixtures/classact-app gets one,
deliberately, since either would risk shifting their own checked-in
golden-tree fixture -- see that fixture's own header comment). GTApp
IS reused here for the "no sizing gadget" honest-failure case, since
it already has a drag bar but no size gadget.

Prints one greppable line per stage (run.sh asserts on them) and exits
non-zero on any failure.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "host"))

from amipilot import ActionFailed, Amipilot, NotFound  # noqa: E402
from amipilot.wire import WireError  # noqa: E402

TARGET = "GadTools Fixture 2"


def main() -> int:
    hostport = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:1234"
    host, port = hostport.rsplit(":", 1)

    client = Amipilot.connect_with_retry(host, int(port), deadline_seconds=30)
    print(f"HANDSHAKE SERVER={client.info.server_version} "
          f"PROTOCOL={client.info.protocol}")

    before = client.tree(TARGET)
    print(f"INITIAL left={before.left} top={before.top} "
          f"width={before.width} height={before.height}")

    client.window_move(TARGET, 30, 20)
    after = client.tree(TARGET)
    ok = (after.left, after.top) == (before.left + 30, before.top + 20)
    print(f"WINDOWMOVE {'PASS' if ok else 'FAIL'} "
          f"left={after.left} top={after.top}")
    if not ok:
        return 1

    target_w, target_h = before.width + 60, before.height + 40
    client.window_resize(TARGET, target_w, target_h)
    resized = client.tree(TARGET)
    ok = (resized.width, resized.height) == (target_w, target_h)
    print(f"WINDOWSIZE {'PASS' if ok else 'FAIL'} "
          f"width={resized.width} height={resized.height} "
          f"target={target_w}x{target_h}")
    if not ok:
        return 1

    try:
        client.window_resize("AmiPilot GadTools Fixture", 300, 300)
        print("WINDOWSIZE-NO-SIZEGADGET FAIL accepted")
        return 1
    except ActionFailed:
        print("WINDOWSIZE-NO-SIZEGADGET PASS rejected")

    try:
        client.window_move("NoSuchWindowAtAll", 10, 10)
        print("WINDOWMOVE-NO-MATCH FAIL accepted")
        return 1
    except NotFound:
        print("WINDOWMOVE-NO-MATCH PASS rejected")

    client.quit()
    client.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (WireError, TimeoutError) as e:
        print(f"WINDOWMOVERESIZE-TEST ERROR: {e}")
        sys.exit(1)
