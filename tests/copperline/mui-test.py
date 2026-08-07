#!/usr/bin/env python3
"""Drives the MUI-ARexx bridge tier (MUIREXX, phase 0.5) end to end
against a real MUI application for the on-target regression check
(tests/copperline/run.sh) -- see server/include/muirexx.h and
host/amipilot/client.py's mui_command() for the full design rationale.

Target: MUI:Demos/MUI-Demo, the stock demo app shipped with MUI itself
(installed on this developer's Workbench volume at Workbench:MUI --
gated in run.sh on that actually being present, since MUI is a
third-party archive, not part of any standard Workbench install, and
this check must skip cleanly rather than fail for anyone without it).

Confirms: the universal `info <item>` command works (MUI-Demo's own
title/base), a nonexistent app base raises NotFound (RC 5), an
unrecognised command raises CommandError (RC 10) carrying the app's
own numeric result code, and -- the one genuinely useful universal
command every MUI app answers -- `quit` really closes the window,
checked the same way every other teardown check in this suite does:
a subsequent TREE finding no matching window.

Prints one greppable line per stage (run.sh asserts on them) and
exits non-zero on any failure.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "host"))

from amipilot import Amipilot, CommandError, NotFound  # noqa: E402
from amipilot.wire import WireError  # noqa: E402

APP_BASE = "MUIDEMO"
WINDOW = "MUI-Demo"


def main() -> int:
    hostport = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:1234"
    host, port = hostport.rsplit(":", 1)

    client = Amipilot.connect_with_retry(host, int(port), deadline_seconds=30,
                                          connect_timeout=20)
    print(f"HANDSHAKE SERVER={client.info.server_version} "
          f"PROTOCOL={client.info.protocol}")

    title = client.mui_command(APP_BASE, "info title")
    print(f"INFO-TITLE RESULT={title}")
    if title != "MUI-Demo":
        print("INFO-TITLE FAIL")
        return 1

    try:
        client.mui_command("NOSUCHAPP12345", "info title", timeout=3)
        print("NOTFOUND-CHECK FAIL")
        return 1
    except NotFound:
        print("NOTFOUND-CHECK PASS")

    try:
        client.mui_command(APP_BASE, "thisisnotarealcommand")
        print("APPERROR-CHECK FAIL")
        return 1
    except CommandError as e:
        print(f"APPERROR-CHECK PASS {e}")

    try:
        client.tree(WINDOW)
        print("BEFORE-QUIT PASS")
    except NotFound:
        print("BEFORE-QUIT FAIL")
        return 1

    client.mui_command(APP_BASE, "quit")
    print("QUIT-SENT OK")

    import time
    time.sleep(2)
    try:
        client.tree(WINDOW)
        print("AFTER-QUIT FAIL")
        return 1
    except NotFound:
        print("AFTER-QUIT PASS")

    client.quit()
    client.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except WireError as e:
        print(f"MUI-TEST ERROR: {e}")
        sys.exit(1)
