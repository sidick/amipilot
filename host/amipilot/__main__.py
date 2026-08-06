"""`amipilot` CLI entry point (`python -m amipilot`, or the installed
`amipilot` console script). Currently one subcommand: `dump`.
"""

from __future__ import annotations

import sys

from . import dump as _dump


def main(argv: list[str] | None = None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    if not argv or argv[0] not in ("dump",):
        print("usage: amipilot dump <window> [--host HOST] [--port PORT] "
              "[--format {text,python}]", file=sys.stderr)
        return 2
    return _dump.main(argv[1:])


if __name__ == "__main__":
    sys.exit(main())
