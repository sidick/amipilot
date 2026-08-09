#!/usr/bin/env python3
"""Drives AmiPilotServer's WAITFOR REQUESTER condition, and (issue #52
follow-up, 2026-08-09) actually dismisses the requester via plain
CLICK, end to end for the on-target regression check
(tests/copperline/run.sh).

Target: fixtures/gadtools-app's own "Ask" button (GID_ASK), whose
IDCMP_GADGETUP handler calls a real, blocking AutoRequest() -- a
genuine struct Requester attached to the fixture's own window
(window->FirstRequest non-NULL for its whole duration), not a
simulation. CLICKing it is fire-and-forget: the button's own handler
blocks the guest inside AutoRequest(), so the click's own event
injection still completes and returns immediately (same async
semantics every other CLICK against a blocking handler already
relies on elsewhere in this suite).

Real finding this session, confirmed live (not assumed from the RKRM
alone): a window-owned AutoRequest()/BuildSysRequest() does NOT attach
its content to the owning window's own FirstGadget/FirstRequest chain
the way the issue's original design sketch assumed -- confirmed here
by TREE: while the requester is open, `TREE <same window pattern>`
resolves to a SEPARATE struct Window (different bounds, a completely
different gadget list: two `frbuttonclass` objects for Yes/No, no sign
of the app's own Host/Enabled/etc gadgets at all), which just happens
to carry the EXACT SAME title text as the owning window (RKRM: "a new
window is opened in the same screen as the one containing your
window" -- confirmed, not documented as same-titled, but observed
that way live). AmipFindWindow()'s own pattern resolution already
picks up that new window correctly with zero code changes -- meaning
CLICK's existing plain `<window-pattern> <gadget-id>` form already
reaches a window-owned requester's own gadgets today. The GadgetID
values are RKRM-documented, fixed, and app-independent (BuildSysRequest's
own autodoc): PosText's gadget always gets GadgetID TRUE (1), NegText's
always gets GadgetID FALSE (0) -- confirmed live below by actually
clicking id=1 and getting a real dismiss. No REQ= locator, no wire
protocol change, no new manifest field needed for this case -- issue
#52's own "sketch, not yet scoped in detail" design turned out to be
solving a problem this project's real target OS/ROM doesn't actually
have. The still-real, still-open gap: a SYSTEM-WIDE requester (no
owning window -- BuildSysRequest(NULL, ...), the disk-swap/DOS-error/
Guru case) opens on the default public screen with no known title to
pattern-match against at all, and remains genuinely unaddressed by
this test or by CLICK/WAITFOR.

Assertions:
- WAITFOR REQUESTER *before* clicking Ask correctly times out (RC 15)
  -- proving this doesn't false-positive against GTApp's own ordinary,
  requester-free window.
- WAITFOR REQUESTER *after* clicking Ask succeeds once the requester
  is actually up -- proving real detection, not a hypothetical.
- CLICK against the SAME window pattern, gadget id 1 (the documented
  GadgetID TRUE convention) actually dismisses the requester -- proving
  real action, not just detection.
- WAITFOR REQUESTER once more, now correctly timing out again -- proving
  the dismiss was genuine (FreeSysRequest() ran, the duplicate window
  and FirstRequest are both gone), not a false PASS on stale state.

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

    # GadgetID TRUE (1) is the RKRM-documented, fixed, app-independent
    # ID BuildSysRequest() gives the PosText ("Yes") gadget -- see this
    # file's own header comment. Same window PATTERN as the owning
    # window (they share exact title text) -- AmipFindWindow() already
    # resolves to the requester's own separate window correctly.
    client.click(WINDOW, 1)
    print("REQUESTER-YES-CLICKED OK")

    try:
        client.wait_for("requester", timeout=2)
        print("REQUESTER-DISMISSED FAIL stillpresent")
        return 1
    except Timeout:
        print("REQUESTER-DISMISSED PASS")

    client.quit()
    client.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (WireError, TimeoutError) as e:
        print(f"REQUESTER-TEST ERROR: {e}")
        sys.exit(1)
