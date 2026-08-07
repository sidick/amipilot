"""Parses the SCREENS text format -- the payload shape the wire emits
for `SCREENS`. Format is fixed by the `AMIP_AREXX_CMD_SCREENS` case in
server/src/amipilotserver/main.c:

    screen title="<title>" [<left>,<top> <width>x<height>] frontmost=<0|1>

One line per screen. `title` is the screen's `DefaultTitle`, not its
live `Title` field -- see server/include/action_engine.h's
`AmipFindWindow` doc comment for why: `Title` tracks whichever window
is currently active on that screen rather than naming the screen
itself, so it isn't a stable identity to match or report.

`title` is escaped by the server exactly like a C string literal
(`EscapeQuotes()`, server/src/amipilotserver/main.c) -- a screen's
`DefaultTitle` can legitimately contain a literal `"` -- so it uses
model.py's shared ESCAPED_FIELD/unescape() rather than a bare `[^"]*`.
"""

from __future__ import annotations

import re
from dataclasses import dataclass

from .model import ESCAPED_FIELD, unescape

_SCREEN_RE = re.compile(
    rf'^screen title="(?P<title>{ESCAPED_FIELD})" '
    r"\[(?P<left>-?\d+),(?P<top>-?\d+) (?P<width>\d+)x(?P<height>\d+)\] "
    r"frontmost=(?P<frontmost>[01])$"
)


@dataclass
class Screen:
    title: str
    left: int
    top: int
    width: int
    height: int
    frontmost: bool


class ScreenParseError(Exception):
    pass


def parse_screens(text: str) -> list[Screen]:
    """Parses a SCREENS payload (zero or more screen lines -- zero is
    not actually reachable in practice, Intuition always has at least
    one screen open, but nothing here assumes otherwise). Raises
    ScreenParseError on any non-blank line that doesn't match."""
    screens = []
    for line in text.splitlines():
        if line == "":
            continue
        m = _SCREEN_RE.match(line)
        if m is None:
            raise ScreenParseError(f"unrecognised screen line: {line!r}")
        screens.append(
            Screen(
                title=unescape(m["title"]),
                left=int(m["left"]),
                top=int(m["top"]),
                width=int(m["width"]),
                height=int(m["height"]),
                frontmost=m["frontmost"] == "1",
            )
        )
    return screens
