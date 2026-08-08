#!/usr/bin/env python3
"""Drives AmiPilotServer's real TCP transport (bsdsocket.library, NOT
the serial-to-TCP bridge wire-test.py uses) end to end for the
on-target regression check (tests/copperline/run.sh) -- Copperline
0.15's `net = "host"` hostsocket backend (github.com/sidick/amipilot
issue #55), which delegates directly to a real host OS socket for
connect/send/recv/bind/listen/accept. Confirms a host Python client
can reach a genuine `AmiPilotServer TCP` listener running inside the
guest with zero bridge/feth/root setup at all -- unlike the `bridge`
hostsocket backend's own real caveats (root, /dev/bpf, static
address/gateway, not reboot-persistent -- see this project's own
`copperline-hostsocket-constraint` notes), `net="host"` needs none of
that.

Connects to argv[1] (host:port, default 127.0.0.1:1236) -- the real
host port AmiPilotServer's own bsdsocket.library listen() call bound,
reachable directly since the guest shares the host's own network
identity under `net="host"`.

Prints one greppable line per probe (run.sh asserts on them) and exits
non-zero on any transport-level failure.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "host"))

from amipilot import Amipilot  # noqa: E402
from amipilot.wire import WireError  # noqa: E402


def main() -> int:
    hostport = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:1236"
    host, port = hostport.rsplit(":", 1)

    client = Amipilot.connect_with_retry(host, int(port), deadline_seconds=30)
    print(f"HANDSHAKE SERVER={client.info.server_version} "
          f"PROTOCOL={client.info.protocol}")

    window = client.tree("GadTools")
    ok = window.title == "AmiPilot GadTools Fixture" and len(window.gadgets) > 0
    print(f"TREE {'PASS' if ok else 'FAIL'} title={window.title!r} "
          f"gadgets={len(window.gadgets)}")
    if not ok:
        return 1

    client.click(window.title, 1)
    print("CLICK PASS")

    client.quit()
    client.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except WireError as e:
        print(f"TCP-HOST-TEST ERROR: {e}")
        sys.exit(1)
