#!/usr/bin/env python3
"""Drives AmiPilotServer's SCREENSHOT verb (phase 1.0, GitHub issue
#41, server/include/screenshot.h) end to end for the on-target
regression check (tests/copperline/run.sh) -- against a real planar
Workbench screen (fixtures/gadtools-app/GTApp), not a synthetic
payload (amipilot.screenshot's own dedicated unit tests already cover
the parsing/encoding logic against synthetic captures).

Proves: a bare (default-screen) capture has sane, non-zero dimensions
and a palette/plane-size shape matching its own declared depth; a
WINDOW= capture reports a non-empty crop rectangle; a bad SCREEN=
substring is rejected (NotFound); and both to_ilbm()/to_png() produce
real, correctly-signed files on the HOST filesystem (this script runs
host-side, same as every other tests/copperline/*.py here).

Prints one greppable line per stage (run.sh asserts on them) and exits
non-zero on any failure.
"""

import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "host"))

from amipilot import Amipilot, NotFound  # noqa: E402
from amipilot.wire import WireError  # noqa: E402


def main() -> int:
    hostport = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:1234"
    host, port = hostport.rsplit(":", 1)

    client = Amipilot.connect_with_retry(host, int(port), deadline_seconds=30)
    print(f"HANDSHAKE SERVER={client.info.server_version} "
          f"PROTOCOL={client.info.protocol}")

    shot = client.screenshot()
    plane_size = shot.bytes_per_row * shot.height
    ok = (
        shot.width > 0 and shot.height > 0
        and 1 <= shot.depth <= 8
        and len(shot.planes) == shot.depth
        and all(len(p) == plane_size for p in shot.planes)
        and len(shot.palette) == min(2 ** shot.depth, 256)
        and shot.crop is None
    )
    print(f"BARE-CAPTURE {'PASS' if ok else 'FAIL'} "
          f"width={shot.width} height={shot.height} depth={shot.depth} "
          f"colors={len(shot.palette)}")
    if not ok:
        return 1

    window_shot = client.screenshot(window="GadTools")
    ok = window_shot.crop is not None and all(v > 0 for v in window_shot.crop[2:])
    print(f"WINDOW-CROP {'PASS' if ok else 'FAIL'} crop={window_shot.crop}")
    if not ok:
        return 1

    # save()/to_png() default to cropping a window= capture down to
    # just that window (not the whole screen it was captured on) --
    # verify the PNG's own IHDR actually reports the smaller,
    # window-sized dimensions, and that crop=False opts back out.
    import struct
    cropped_png = window_shot.to_png()
    cropped_w, cropped_h = struct.unpack(">II", cropped_png[16:24])
    full_png = window_shot.to_png(crop=False)
    full_w, full_h = struct.unpack(">II", full_png[16:24])
    ok = (
        (cropped_w, cropped_h) == (window_shot.crop[2], window_shot.crop[3])
        and (full_w, full_h) == (window_shot.width, window_shot.height)
        and (cropped_w, cropped_h) != (full_w, full_h)
    )
    print(f"WINDOW-CROP-DEFAULT {'PASS' if ok else 'FAIL'} "
          f"cropped={cropped_w}x{cropped_h} full={full_w}x{full_h}")
    if not ok:
        return 1

    with tempfile.TemporaryDirectory() as tmpdir:
        base = os.path.join(tmpdir, "screenshot")
        ilbm_path, png_path = shot.save(base)

        with open(ilbm_path, "rb") as f:
            ilbm_data = f.read()
        with open(png_path, "rb") as f:
            png_data = f.read()

        ok = ilbm_data[:4] == b"FORM" and ilbm_data[8:12] == b"ILBM"
        print(f"ILBM-FILE {'PASS' if ok else 'FAIL'} size={len(ilbm_data)}")
        if not ok:
            return 1

        ok = png_data[:8] == b"\x89PNG\r\n\x1a\n"
        print(f"PNG-FILE {'PASS' if ok else 'FAIL'} size={len(png_data)}")
        if not ok:
            return 1

    try:
        client.screenshot(screen="NoSuchScreenAtAll")
        print("BAD-SCREEN FAIL accepted")
        return 1
    except NotFound:
        print("BAD-SCREEN PASS rejected")

    client.quit()
    client.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (WireError, TimeoutError) as e:
        print(f"SCREENSHOT-TEST ERROR: {e}")
        sys.exit(1)
