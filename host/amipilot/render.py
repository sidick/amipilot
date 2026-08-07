"""Deterministic text rendering of a Window model -- the shared basis
for `amipilot dump`'s `--format text`/`--format python` output and
golden-tree comparisons (golden.py). Split out from dump.py so
golden.py (which client.py depends on) doesn't have to import dump.py,
which itself imports client.py for its CLI entry point -- that would
be a circular import.
"""

from __future__ import annotations

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
