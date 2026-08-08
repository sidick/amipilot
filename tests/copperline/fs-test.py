#!/usr/bin/env python3
"""Drives AmiPilotServer's file API (FSLIST/FSSTAT/FSMKDIR/FSDELETE/
FSGET/FSPUT, phases 0.4-1.0) end to end for the on-target regression
check (tests/copperline/run.sh).

The server is started with `FSROOT=RAM:amipilot-fs-test` (see this
check's smoke.script in run.sh, which creates that directory first via
`MakeDir` before `Run`ning AmiPilotServer -- the grant is a Lock() at
startup, so the directory must already exist). Proves both the happy
path (create a dir, create a file inside it via FSMKDIR won't do files
-- so a small file is written by a Shell redirect ahead of time and
read back via FSGET) and the containment check (a path outside the
granted root, e.g. SYS:, must be rejected, not served).

Prints one greppable line per stage (run.sh asserts on them) and exits
non-zero on any failure.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "host"))

from amipilot import Amipilot, CommandError, NotFound  # noqa: E402
from amipilot.wire import WireError  # noqa: E402

ROOT = "RAM:amipilot-fs-test"


def main() -> int:
    hostport = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:1234"
    host, port = hostport.rsplit(":", 1)

    client = Amipilot.connect_with_retry(host, int(port), deadline_seconds=30)
    print(f"HANDSHAKE SERVER={client.info.server_version} "
          f"PROTOCOL={client.info.protocol}")

    entries = client.fs_list(ROOT)
    names = {e.name for e in entries}
    if "seed.txt" in names:
        print(f"FSLIST-SEED PASS found={sorted(names)}")
    else:
        print(f"FSLIST-SEED FAIL found={sorted(names)}")
        return 1

    stat = client.fs_stat(f"{ROOT}/seed.txt")
    print(f"FSSTAT RESULT name={stat.name} is_dir={stat.is_dir} size={stat.size}")

    data = client.fs_get(f"{ROOT}/seed.txt")
    print(f"FSGET RESULT={data.decode('latin-1')!r}")

    client.fs_mkdir(f"{ROOT}/subdir")
    substat = client.fs_stat(f"{ROOT}/subdir")
    if substat.is_dir:
        print("FSMKDIR PASS")
    else:
        print("FSMKDIR FAIL not a directory")
        return 1

    client.fs_delete(f"{ROOT}/subdir")
    try:
        client.fs_stat(f"{ROOT}/subdir")
        print("FSDELETE FAIL still present")
        return 1
    except NotFound:
        print("FSDELETE PASS")

    # FSPUT (phase 1.0) round trip: write a file host-to-Amiga over
    # this check's own transport (serial.device, via Copperline's
    # `--serial tcp` bridge -- see run.sh's own comment), then read it
    # straight back to confirm the bytes (including an embedded NUL)
    # survived intact.
    put_data = b"written by fs_put\x00trailer"
    client.fs_put(f"{ROOT}/put.dat", put_data)
    got = client.fs_get(f"{ROOT}/put.dat")
    if got == put_data:
        print("FSPUT PASS")
    else:
        print(f"FSPUT FAIL got={got!r}")
        return 1

    try:
        client.fs_put("SYS:amipilot-fsput-reject.dat", b"x")
        print("FSPUT-CONTAINMENT FAIL SYS: was served")
        return 1
    except CommandError:
        print("FSPUT-CONTAINMENT PASS SYS: rejected")

    try:
        client.fs_list("SYS:")
        print("CONTAINMENT FAIL SYS: was served")
        return 1
    except CommandError:
        print("CONTAINMENT PASS SYS: rejected")

    client.quit()
    client.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (WireError, TimeoutError) as e:
        print(f"FS-TEST ERROR: {e}")
        sys.exit(1)
