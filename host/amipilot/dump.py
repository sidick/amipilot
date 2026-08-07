"""`amipilot dump` -- pretty-prints a window's gadget tree from the host,
piped straight into a quirk profile or test file: dump, copy, script
(docs/implementation-plan.md, "The inspector"). The wire stays
JSON-free (server/WIRE.md); this is where any machine-readable
rendering happens, host-side, not on the wire.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .client import Amipilot, AmipilotError
from .golden import GoldenMismatch, assert_golden
from .render import render_python, render_text


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="amipilot dump",
                                     description=__doc__.strip().splitlines()[0])
    parser.add_argument("window", help="window-title substring to match")
    parser.add_argument("--screen", default=None,
                        help="restrict the match to screens whose own name "
                             "(DefaultTitle) contains this substring -- for "
                             "disambiguating two same-titled windows on "
                             "different screens")
    parser.add_argument("--host", default="127.0.0.1",
                        help="wire transport host (default 127.0.0.1)")
    parser.add_argument("--port", type=int, default=1234,
                        help="wire transport port (default 1234, "
                             "Copperline's serial TCP bridge default)")
    parser.add_argument("--format", choices=["text", "python"], default="text",
                        help="text: same format AmiInspect prints; "
                             "python: quirk-profile-ready name suggestions")
    parser.add_argument("--golden", default=None, metavar="PATH",
                        help="compare the live tree against a golden-tree "
                             "file instead of printing it -- exits 1 with a "
                             "diff on a mismatch, 0 on a match. Ignores "
                             "--format (golden files always use the text "
                             "shape). A path that doesn't exist yet is "
                             "created, same as --update-golden.")
    parser.add_argument("--update-golden", action="store_true",
                        help="with --golden, (re)write the golden file from "
                             "the live tree instead of comparing against it "
                             "-- use once a UI change is confirmed "
                             "intentional")
    args = parser.parse_args(argv)

    try:
        with Amipilot.connect(args.host, args.port) as client:
            window = client.tree(args.window, screen=args.screen)
    except AmipilotError as e:
        print(f"amipilot dump: {e}", file=sys.stderr)
        return 1
    except OSError as e:
        print(f"amipilot dump: could not connect to {args.host}:{args.port}: {e}",
              file=sys.stderr)
        return 1

    if args.golden:
        golden_path = Path(args.golden)
        wrote = args.update_golden or not golden_path.exists()
        try:
            assert_golden(window, golden_path, update=args.update_golden)
        except GoldenMismatch as e:
            print(f"amipilot dump: {e}", file=sys.stderr)
            return 1
        print(f"amipilot dump: wrote {golden_path}" if wrote
              else f"amipilot dump: {golden_path} matches")
        return 0

    render = render_text if args.format == "text" else render_python
    print(render(window))
    return 0


if __name__ == "__main__":
    sys.exit(main())
