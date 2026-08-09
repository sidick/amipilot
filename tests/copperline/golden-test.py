#!/usr/bin/env python3
"""Golden-tree fixture check (docs/implementation-plan.md, "The
inspector": "a saved dump doubles as a structural fixture -- 'this
app's UI still has this shape' as a one-line assertion") -- the host
half of the phase 0.5 loop, mirroring wire-test.py's structure.

Connects the same way wire-test.py does (Copperline's serial TCP
bridge), then asserts each fixture's live main-window tree still
matches its checked-in golden file
(fixtures/<app>/<App>.golden), via amipilot.golden.assert_golden().
A mismatch here means real, unintended UI drift -- this script never
auto-updates a golden file; that's a deliberate, separate step via
`amipilot dump ... --golden ... --update-golden` once a change is
confirmed intentional.

Prints one greppable line per fixture (run.sh asserts on them) and
exits non-zero on any mismatch or transport-level failure.
"""

import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "host"))

from amipilot.golden import GoldenMismatch, assert_golden  # noqa: E402
from amipilot.model import parse_tree  # noqa: E402
from amipilot.wire import WireClient, WireError  # noqa: E402

REPO_ROOT = os.path.join(os.path.dirname(__file__), "..", "..")

FIXTURES = [
    # Single-word substrings, deliberately -- TREE's window-pattern
    # argument is a bare token unless double-quoted (arexx_cmd.c's
    # read_token()), and this script sends commands unquoted like
    # wire-test.py does. A full, space-containing title sent unquoted
    # would parse as pattern "AmiPilot" only (its first token) for
    # BOTH fixtures, first-match-wins-ing onto whichever window
    # happens to be first in Intuition's list -- confirmed the hard
    # way generating these fixtures' golden files the first time.
    ("GTAPP", "GadTools",
     os.path.join(REPO_ROOT, "fixtures", "gadtools-app", "GTApp.golden")),
    ("CAAPP", "ClassAct",
     os.path.join(REPO_ROOT, "fixtures", "classact-app", "CAApp.golden")),
    ("RCAPP", "ReAction",
     os.path.join(REPO_ROOT, "fixtures", "reaction-classes-app", "ReactionClassesApp.golden")),
]


def main() -> int:
    hostport = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:1234"
    host, port = hostport.rsplit(":", 1)

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

    client.handshake()

    ok = True
    for tag, window_pattern, golden_path in FIXTURES:
        reply = client.command(f"TREE {window_pattern}")
        if reply.rc != 0:
            print(f"GOLDEN-{tag} RC={reply.rc} RESULT={reply.text}")
            ok = False
            continue
        window = parse_tree(reply.text)
        try:
            assert_golden(window, golden_path, update=False)
            print(f"GOLDEN-{tag} MATCH")
        except GoldenMismatch as e:
            print(f"GOLDEN-{tag} MISMATCH")
            print(e.diff)
            ok = False

    client.command("QUIT")
    client.close()
    return 0 if ok else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except WireError as e:
        print(f"WIRE ERROR: {e}")
        sys.exit(1)
