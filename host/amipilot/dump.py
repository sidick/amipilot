"""`amipilot dump` -- pretty-prints a window's gadget tree from the host,
piped straight into a quirk profile or test file: dump, copy, script
(docs/implementation-plan.md, "The inspector"). The wire stays
JSON-free (server/WIRE.md); this is where any machine-readable
rendering happens, host-side, not on the wire.
"""

from __future__ import annotations

import argparse
import sys

from .client import Amipilot, AmipilotError
from .model import Window


def _escape(s: str) -> str:
    """Re-escapes a field model.py already unescaped, so render_text()'s
    output stays parseable by the same fixed format it claims to match
    (`AmiInspect`'s own text) -- a title/label containing a literal `"`
    must round-trip through this the same way it does over the wire."""
    return s.replace("\\", "\\\\").replace('"', '\\"')


def render_text(window: Window) -> str:
    lines = [f'window "{_escape(window.title)}" screen="{_escape(window.screen)}" '
             f"[{window.left},{window.top} {window.width}x{window.height}]"]
    for g in window.gadgets:
        value = f' value="{_escape(g.value)}"' if g.value is not None else ""
        lines.append(
            f"  gadget id={g.gadget_id} role={g.role} "
            f'class="{_escape(g.class_name)}" label="{_escape(g.label)}"{value} '
            f"[{g.left},{g.top} {g.width}x{g.height}]"
        )
    return "\n".join(lines)


def render_python(window: Window) -> str:
    """A quirk-profile-ready form: one `# name = <id>` suggestion per
    gadget, commented so it's copy/paste starting material, not a
    guess at final naming."""
    lines = [f'# window "{window.title}"']
    for g in window.gadgets:
        label = g.label or g.role.lower()
        slug = "".join(c if c.isalnum() else "_" for c in label.lower()).strip("_")
        lines.append(f"# {slug or 'gadget'} = {g.gadget_id}  "
                      f"# role={g.role} label={g.label!r}")
    return "\n".join(lines)


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

    render = render_text if args.format == "text" else render_python
    print(render(window))
    return 0


if __name__ == "__main__":
    sys.exit(main())
