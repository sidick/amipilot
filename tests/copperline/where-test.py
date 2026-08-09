#!/usr/bin/env python3
"""Drives the cooperative geometry port (WHERE, issue #49) end to end
against fixtures/classact-app for the on-target regression check
(tests/copperline/run.sh) -- see manifest/SPEC.md's "The cooperative
geometry port" and server/include/where.h for the full design
rationale.

Target: fixtures/classact-app's own CAAPP.WHERE ARexx port, launched
directly by run.sh's smoke script (same pattern run_mui_check uses for
MUI-Demo). All three of CAApp's gadgets (connect_button/host_field/
enabled_checkbox) are children of a window.class window's
layout.gadget -- permanently invisible to structural walking (the
project's documented "Confirmed limit") -- so CAApp.manifest (format
version 2) names every one of them as a WHEREGADGET instead of a plain
GADGET.

Confirms: WHERE @connect_button returns four plausible integers inside
the window's own TREE-reported bounds; an unknown manifest name and a
GETTEXT on a WHEREGADGET (a stated limit -- geometry only, no read-back)
both raise CommandError (RC 10); TYPE @host_field genuinely lands text
in the layout child's own string gadget (confirmed indirectly, via the
fixture's own "caapp: host=..." diagnostic log line -- GETTEXT can't
read this back, so this is the only external confirmation available);
and CLICK @connect_button really reaches and presses the real button,
confirmed the same way every other teardown check in this suite does:
CLICK's own EXPECT=NOWINDOW waiting for the window to close.

Prints one greppable line per stage (run.sh asserts on them) and exits
non-zero on any failure.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "host"))

from amipilot import Amipilot, CommandError, NotFound  # noqa: E402
from amipilot.wire import WireError  # noqa: E402

WINDOW = "AmiPilot ClassAct Fixture"
MANIFEST_PATH = "SRC:fixtures/classact-app/CAApp.manifest"
TYPED_TEXT = "AmigaTest"


def main() -> int:
    hostport = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:1234"
    host, port = hostport.rsplit(":", 1)

    client = Amipilot.connect_with_retry(host, int(port), deadline_seconds=30,
                                          connect_timeout=20)
    print(f"HANDSHAKE SERVER={client.info.server_version} "
          f"PROTOCOL={client.info.protocol}")

    report = client.manifest(MANIFEST_PATH)
    print(f"MANIFEST-LOADED {report}")

    window = client.tree(WINDOW)
    win_w, win_h = window.width, window.height

    x, y, w, h = client.where("connect_button")
    print(f"WHERE-GEOMETRY RESULT={x} {y} {w} {h}")
    if not (0 <= x < win_w and 0 <= y < win_h and w > 0 and h > 0
            and x + w <= win_w and y + h <= win_h):
        print("WHERE-GEOMETRY FAIL (geometry outside window bounds "
              f"{win_w}x{win_h})")
        return 1
    print("WHERE-GEOMETRY PASS")

    try:
        client.where("nosuchgadget")
        print("UNKNOWN-NAME FAIL")
        return 1
    except CommandError:
        print("UNKNOWN-NAME PASS")

    try:
        client.get_text_by_name("host_field")
        print("GETTEXT-LIMIT FAIL")
        return 1
    except CommandError:
        print("GETTEXT-LIMIT PASS")

    client.type_by_name("host_field", TYPED_TEXT)
    print("TYPE-VIA-WHERE SENT")

    try:
        client.click_by_name("connect_button", expect="nowindow", timeout=15)
        print("CLICK-VIA-WHERE PASS")
    except Exception as e:  # noqa: BLE001 -- report whatever went wrong, then fail
        print(f"CLICK-VIA-WHERE FAIL {e}")
        return 1

    try:
        client.tree(WINDOW)
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
    except WireError as e:
        print(f"WHERE-TEST ERROR: {e}")
        sys.exit(1)
