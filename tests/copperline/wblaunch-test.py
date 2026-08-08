#!/usr/bin/env python3
"""Drives AmiPilotServer's WBLAUNCH verb (phase 1.0, server/include/
wblaunch.h) end to end for the on-target regression check (tests/
copperline/run.sh) -- a REAL Workbench-style start (genuine WBStartup/
WBArg message), not launch()'s Shell-style one.

The target is fixtures/wbapp (WBApp): a non-GUI, Workbench-startable
fixture that reports what it actually received -- its own icon's
tooltypes (via the standard CurrentDir()+GetDiskObject() self-lookup
idiom every real Workbench-aware program uses) and every WBArg it was
launched with -- to a plain text file at T:amipilot-wblaunch-result.txt
(this check's own smoke.script grants FSROOT=T: so the file API can
read it back). Its own icon is stamped once, before the server starts,
by the fixtures/wbapp/src/makeicon.c helper (MakeIcon), reading the
SYSTEM's own default WBTOOL icon and baking in two known tooltypes
(GREETING, PORT) -- proven against a real, icon.library-round-tripped
icon, not a hand-authored one.

Proves: a bare launch (no overrides) reports both baked-in tooltypes
unchanged; a TOOLTYPE= override changes PORT while leaving GREETING
alone (the actual "merge", not a full replace); an ARG= path becomes a
second WBArg the target sees; and a bad icon path is rejected (RC 10)
rather than silently accepted.

Prints one greppable line per stage (run.sh asserts on them) and exits
non-zero on any failure.
"""

import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "host"))

from amipilot import Amipilot, CommandError, NotFound  # noqa: E402
from amipilot.wire import WireError  # noqa: E402

RESULT_PATH = "T:amipilot-wblaunch-result.txt"


def read_result(client, timeout=15.0):
    """Polls for RESULT_PATH, retrying past a still-partial write (WBApp
    writes several lines before Close()) by waiting specifically for its
    LAST line, TOOLTYPE_PORT=, to show up -- not just the file's mere
    existence, which could observe a truncated read mid-write."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            text = client.fs_get(RESULT_PATH).decode("latin-1")
            if "TOOLTYPE_PORT=" in text:
                return text
        except NotFound:
            pass
        time.sleep(0.3)
    raise TimeoutError(f"{RESULT_PATH} never showed a complete report within {timeout}s")


def parse(text):
    fields = {}
    for line in text.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            fields[k] = v
    return fields


def main():
    hostport = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:1234"
    host, port = hostport.rsplit(":", 1)

    client = Amipilot.connect_with_retry(host, int(port), deadline_seconds=30)
    print(f"HANDSHAKE SERVER={client.info.server_version} "
          f"PROTOCOL={client.info.protocol}")

    client.wb_launch("SRC:build/fixtures/WBApp")
    fields = parse(read_result(client))
    ok = (
        fields.get("STARTED_FROM") == "WORKBENCH"
        and fields.get("NUMARGS") == "1"
        and fields.get("ARG0") == "WBApp"
        and fields.get("TOOLTYPE_GREETING") == "hello from the default icon"
        and fields.get("TOOLTYPE_PORT") == "1111"
    )
    print(f"BARE-LAUNCH {'PASS' if ok else 'FAIL ' + repr(fields)}")
    if not ok:
        return 1
    client.fs_delete(RESULT_PATH)

    client.wb_launch("SRC:build/fixtures/WBApp", tooltypes={"PORT": "7777"})
    fields = parse(read_result(client))
    ok = (
        fields.get("TOOLTYPE_PORT") == "7777"
        and fields.get("TOOLTYPE_GREETING") == "hello from the default icon"
    )
    print(f"TOOLTYPE-OVERRIDE {'PASS' if ok else 'FAIL ' + repr(fields)}")
    if not ok:
        return 1
    client.fs_delete(RESULT_PATH)

    client.wb_launch("SRC:build/fixtures/WBApp", args=["SRC:build/fixtures/GTApp"])
    fields = parse(read_result(client))
    ok = fields.get("NUMARGS") == "2" and fields.get("ARG1") == "GTApp"
    print(f"ARG-EXTRA {'PASS' if ok else 'FAIL ' + repr(fields)}")
    if not ok:
        return 1
    client.fs_delete(RESULT_PATH)

    try:
        client.wb_launch("SRC:build/fixtures/NoSuchApp")
        print("BAD-ICON FAIL accepted")
        return 1
    except CommandError:
        print("BAD-ICON PASS rejected")

    # Regression check for a real bug found in code review: a
    # TOOLTYPE= override writes a scratch icon to T: BEFORE the rest
    # of the launch is attempted; every failure after that point
    # (ARG=/LoadSeg()/allocation/CreateNewProc() all failing) used to
    # return without ever deleting it, orphaning a
    # T:amipilot-wb-<n>.info file permanently. Force one of those
    # failures (a TOOLTYPE= override plus a nonexistent ARG= path) and
    # confirm T: has no leftover amipilot-wb-*.info files afterward.
    # Note: FillArg() only locks an ARG='s own PARENT DIRECTORY (real
    # WBArg semantics -- the file itself is never opened/verified at
    # registration time), so a nonexistent FILE in a real directory
    # does NOT fail -- it takes a nonexistent DIRECTORY to make
    # FillArg()'s own Lock() fail.
    before = {e.name for e in client.fs_list("T:") if e.name.startswith("amipilot-wb-")}
    try:
        client.wb_launch(
            "SRC:build/fixtures/WBApp",
            tooltypes={"PORT": "9999"},
            args=["SRC:build/fixtures/no-such-dir-at-all/somefile"],
        )
        print("SCRATCH-ICON-LEAK FAIL launch with a bad ARG= was accepted")
        return 1
    except CommandError:
        pass
    after = {e.name for e in client.fs_list("T:") if e.name.startswith("amipilot-wb-")}
    leaked = after - before
    if leaked:
        print(f"SCRATCH-ICON-LEAK FAIL orphaned={sorted(leaked)}")
        return 1
    print("SCRATCH-ICON-LEAK PASS no orphaned scratch icon")

    client.quit()
    client.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (WireError, TimeoutError) as e:
        print(f"WBLAUNCH-TEST ERROR: {e}")
        sys.exit(1)
