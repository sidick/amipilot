"""Golden-tree structural fixtures (docs/implementation-plan.md, "The
inspector": "a saved dump doubles as a structural fixture -- 'this
app's UI still has this shape' as a one-line assertion, catching
upstream UI changes before quirk-profile scripts fail confusingly").

Reuses dump.py's render_text() -- the same deterministic text form
AmiInspect prints -- as the golden file's own content, so a golden
file is readable output a human can review in a diff, not a bespoke
serialization format.

**Locale is part of the golden file's environment, not just its
content.** Comparison here is an exact text match, and several fields
in that text are not process-invariant: a window's title (the
`window "..."` line) and a screen's own name both come from strings
the running OS/application supplies, and either can be sourced from a
locale.library catalog on a genuinely localized system -- an app whose
gadget labels or window title are looked up via catalog translation
will render different text under a different Locale preference, with
no code change involved at all. Regenerate (and compare) golden files
against a machine/config with a fixed, known Locale -- see
`tests/copperline/README.md`'s own note on what this repository's
checked-in golden fixtures were generated against.
"""

from __future__ import annotations

import difflib
from pathlib import Path

from .model import Window
from .render import render_text


class GoldenMismatch(AssertionError):
    """Raised by assert_golden() when a live tree no longer matches
    its golden file. Carries the unified diff (`.diff`) and the
    golden file's path (`.path`) so a test failure is immediately
    actionable, not just "assertion failed"."""

    def __init__(self, path: Path, diff: str):
        self.path = path
        self.diff = diff
        super().__init__(f"tree no longer matches golden file {path}:\n{diff}")


def assert_golden(window: Window, path: str | Path, *, update: bool = False) -> None:
    """Compares `window` (rendered the same way `amipilot dump
    --format text`/`AmiInspect` print it) against the golden file at
    `path`, raising GoldenMismatch on any difference -- geometry
    included; a golden file is an exact snapshot, not a fuzzy one.
    Regenerate it with `update=True` once a UI change is confirmed
    intentional, not accidental drift.

    A golden file that doesn't exist yet is always written rather
    than compared against, regardless of `update` -- creating one the
    first time a test runs against it is the common case, not a call
    an author should have to spell out separately from updating it.
    """
    path = Path(path)
    rendered = render_text(window) + "\n"

    if update or not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(rendered, encoding="latin-1")
        return

    existing = path.read_text(encoding="latin-1")
    if existing == rendered:
        return

    diff = "".join(
        difflib.unified_diff(
            existing.splitlines(keepends=True),
            rendered.splitlines(keepends=True),
            fromfile=str(path),
            tofile="live tree",
        )
    )
    raise GoldenMismatch(path, diff)
