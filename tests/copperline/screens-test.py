#!/usr/bin/env python3
"""Drives AmiPilotServer's SCREENS verb and SCREEN= qualifier (phase
0.4) end to end for the on-target regression check
(tests/copperline/run.sh).

The smoke script stages fixtures/gadtools-app (opens on the default
Workbench screen) THEN fixtures/second-screen-app (opens its own
custom screen, which becomes frontmost as a result), then
AmiPilotServer -- so the check exercises the actual scenario this
feature exists for: a window that needs its (background) screen
brought forward before it can be driven.

Asserts, over the wire:
  1. SCREENS lists both screens, with the second screen's app
     reporting frontmost=1 and Workbench frontmost=0 -- reached via
     wait_for_screen() rather than a fixed Wait in the smoke script,
     exercising that polling helper live rather than only against a
     fake clock in the host unit tests.
  2. A loose pattern ("GadTools") matching both fixture windows,
     without SCREEN=, still resolves to *some* window (the existing
     cross-screen search isn't broken by the new parameter).
  3. SCREEN=<second screen's title> disambiguates to
     "GadTools Fixture 2" specifically.
  4. The core proof: with the second screen frontmost, CLICK/TYPE
     against GTApp's window on the now-BACKGROUND Workbench screen
     still succeeds -- confirming AmipClickGadget()'s existing
     ScreenToFront()/WindowToFront()/ActivateWindow() calls genuinely
     bring a background screen forward, not just in the single-screen
     case every other check has ever exercised.

Prints one greppable line per stage (run.sh asserts on them) and exits
non-zero on any failure.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "host"))

from amipilot import Amipilot, NotFound  # noqa: E402
from amipilot.wire import WireError  # noqa: E402

SECOND_SCREEN = "AmiPilot Second Screen"
GTAPP_WINDOW = "AmiPilot GadTools Fixture"


def main() -> int:
    hostport = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:1234"
    host, port = hostport.rsplit(":", 1)

    client = Amipilot.connect_with_retry(host, int(port), deadline_seconds=30)
    print(f"HANDSHAKE SERVER={client.info.server_version} "
          f"PROTOCOL={client.info.protocol}")

    second = client.wait_for_screen(SECOND_SCREEN, timeout=20.0)
    screens = client.screens()
    workbench = next((s for s in screens if s.title != SECOND_SCREEN), None)
    if len(screens) == 2 and second.frontmost and workbench is not None \
            and not workbench.frontmost:
        print(f"SCREENS PASS second={second.title} workbench={workbench.title}")
    else:
        print(f"SCREENS FAIL screens={screens!r}")
        return 1

    loose = client.tree("GadTools")
    print(f"LOOSE-MATCH PASS title={loose.title}")

    narrowed = client.tree("GadTools", screen=SECOND_SCREEN)
    if narrowed.title == "GadTools Fixture 2" and narrowed.screen == SECOND_SCREEN:
        print(f"SCREEN-DISAMBIGUATE PASS title={narrowed.title} screen={narrowed.screen}")
    else:
        print(f"SCREEN-DISAMBIGUATE FAIL title={narrowed.title!r} screen={narrowed.screen!r}")
        return 1

    # The core proof: GTApp's window is on the Workbench screen, which
    # is currently in the BACKGROUND (second-screen-app's own screen
    # opened after it and is frontmost). TYPE/GETTEXT/CLICK against it
    # must still work -- AmipClickGadget()'s ScreenToFront() has to
    # genuinely bring Workbench forward first.
    client.type(GTAPP_WINDOW, 2, "background screen works")
    result = client.get_text(GTAPP_WINDOW, 2)
    if result == "background screen works":
        print(f"BACKGROUND-TYPE PASS RESULT={result}")
    else:
        print(f"BACKGROUND-TYPE FAIL RESULT={result}")
        return 1

    client.click(GTAPP_WINDOW, 1)
    try:
        client.tree(GTAPP_WINDOW)
        print("BACKGROUND-CLICK FAIL window still open")
        return 1
    except NotFound:
        print("BACKGROUND-CLICK PASS window closed")

    client.quit()
    client.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (WireError, TimeoutError) as e:
        print(f"SCREENS-TEST ERROR: {e}")
        sys.exit(1)
