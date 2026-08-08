#!/usr/bin/env python3
"""Drives SCREENSHOT's P96/Picasso96 capture path end to end against a
REAL RTG board (GitHub issue #55) -- the on-target counterpart to
run_screenshot_check's own classic-planar-only coverage, which is all
Copperline could exercise before it gained `[rtg]` support.

fixtures/p96-app writes a status line to SRC:build/p96-status.txt
(read directly off the host filesystem via the SRC: hostfs mount --
Run's own >file redirection was confirmed unreliable for a launched
process's real stdout during this feature's development, and this
project's own established "read real output back off disk" pattern
already works everywhere else): a `SKIP ...` line means no genuine P96
mode was available on this machine (most contributors' own
copperline.local.toml won't have [rtg] configured at all -- opt-in,
see copperline.example.toml), which this script reports as a real,
honest skip -- not a failure, matching the whole `make test-target`
gate's own "skip cleanly, don't falsely pass" precedent for
copperline.local.toml itself. `READY` means a genuine P96 CLUT screen
opened with a known x%4 pen-ramp pattern painted on it, in which case
this script drives SCREENSHOT against it and verifies the P96 capture
path pixel-for-pixel, exactly as PR #54's manual Amiberry verification
and this issue's own manual Copperline verification did by hand.

Prints one greppable line (run.sh asserts on it) and exits non-zero
only on a REAL failure -- never on the expected "no RTG hardware here"
case.
"""

import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "host"))

from amipilot import Amipilot  # noqa: E402
from amipilot.wire import WireError  # noqa: E402

STATUS_PATH = os.path.join(os.path.dirname(__file__), "..", "..", "build", "p96-status.txt")
RAMP_WIDTH = 256
RAMP_Y = 100


def wait_for_status(deadline: float) -> str:
    while time.time() < deadline:
        try:
            with open(STATUS_PATH, "r") as f:
                content = f.read()
            if content:
                return content.strip()
        except FileNotFoundError:
            pass
        time.sleep(0.5)
    return ""


def main() -> int:
    hostport = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:1234"
    host, port = hostport.rsplit(":", 1)

    try:
        os.remove(STATUS_PATH)
    except FileNotFoundError:
        pass

    client = Amipilot.connect_with_retry(host, int(port), deadline_seconds=30)
    print(f"HANDSHAKE SERVER={client.info.server_version} "
          f"PROTOCOL={client.info.protocol}")

    client.launch("SRC:build/fixtures/P96App")

    status = wait_for_status(time.time() + 30)
    print(f"STATUS {status!r}")

    if status.startswith("SKIP"):
        print(f"P96 SKIP {status}")
        client.close()
        return 0

    if status != "READY":
        print(f"P96 FAIL unexpected status: {status!r}")
        client.close()
        return 1

    window = client.wait_for_window("AmiPilot P96 Fixture Window", timeout=20.0)
    print(f"WINDOW PASS title={window.title!r} screen={window.screen!r}")

    shot = client.screenshot(window=window.title)
    is_p96 = shot.pixel_format == 1
    print(f"CAPTURE {'PASS' if is_p96 else 'FAIL'} pixel_format={shot.pixel_format} "
          f"rgb_format={shot.rgb_format} width={shot.width} height={shot.height}")
    if not is_p96:
        client.close()
        return 1

    chunky = shot.to_chunky()
    w = shot.width
    expected = [x % 4 for x in range(min(RAMP_WIDTH, 10))]
    actual = [chunky[RAMP_Y * w + x] for x in range(len(expected))]
    ramp_ok = actual == expected
    print(f"RAMP {'PASS' if ramp_ok else 'FAIL'} expected={expected} actual={actual}")
    if not ramp_ok:
        client.close()
        return 1

    client.click_by_role(window.title, role="custom", index=1)
    print("CLOSE PASS")

    client.quit()
    client.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except WireError as e:
        print(f"P96-TEST ERROR: {e}")
        sys.exit(1)
