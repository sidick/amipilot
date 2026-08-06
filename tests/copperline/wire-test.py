#!/usr/bin/env python3
"""Drives AmiPilotServer's wire transport (server/WIRE.md) end to end
for the on-target regression check (tests/copperline/run.sh) -- the
host-side half of the phase 0.3 loop.

Connects to Copperline's serial TCP bridge ([serial] mode = "tcp",
argv[1] as host:port, default 127.0.0.1:1234), which carries the guest's
serial.device byte stream, does the VERSION handshake, then mirrors
arexx-test.rexx's sequence: TYPE into fixtures/gadtools-app's Host
string gadget, read the value back, CLICK Connect, confirm the window is
gone -- state changed, driven and observed entirely over the wire from
the host. Also asserts the spec's RC 10 path for garbage input.

Prints one greppable line per probe (run.sh asserts on them) and exits
non-zero on any transport-level failure.
"""

import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "host"))

from amipilot.wire import WireClient, WireError  # noqa: E402


def main() -> int:
    hostport = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:1234"
    host, port = hostport.rsplit(":", 1)

    # The bridge listens from emulator startup, but be tolerant of the
    # runner racing us to it.
    deadline = time.time() + 30
    client = None
    while client is None:
        try:
            client = WireClient.connect(host, int(port), timeout=30)
        except OSError:
            if time.time() > deadline:
                print("CONNECT FAILED")
                return 1
            time.sleep(0.5)

    info = client.handshake()
    print(f"HANDSHAKE SERVER={info.server_version} PROTOCOL={info.protocol} "
          f"STABLE={','.join(info.stable)}")

    def probe(tag: str, line: str) -> None:
        reply = client.command(line)
        text = reply.text.replace("\n", "|")
        print(f"{tag} RC={reply.rc} RESULT={text}")

    probe("TREE-BEFORE", "TREE GadTools")
    probe("BADVERB", "NONSENSE 1 2 3")
    probe("TYPE-HOST", "TYPE GadTools 2 hello wire")
    probe("GETTEXT-HOST", "GETTEXT GadTools 2")
    probe("CLICK-CONNECT", "CLICK GadTools 1")
    probe("TREE-AFTER", "TREE GadTools")
    probe("QUIT", "QUIT")

    client.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except WireError as e:
        print(f"WIRE ERROR: {e}")
        sys.exit(1)
