"""Parses the TREE text format -- the one payload shape the wire (and
ARexx, and AmiInspect) all emit for a window's gadget tree. Format is
fixed by `AppendGadgetLine`/`BuildTreeResult` in
server/src/amipilotserver/main.c (identical to AmiInspect's own
`PrintModel`, amiinspect/src/main.c):

    window "<title>" [<left>,<top> <width>x<height>]
      gadget id=<id> role=<ROLE> class="<class>" label="<label>" [value="<value>"] [<left>,<top> <width>x<height>]

This is plain text, not a grammar meant to grow -- see the plan's
no-JSON decision (docs/implementation-plan.md, "Protocol and client").
The parser here is a fixed-format reader, not a general one.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

_WINDOW_RE = re.compile(
    r'^window "(?P<title>.*)" \[(?P<left>-?\d+),(?P<top>-?\d+) '
    r"(?P<width>\d+)x(?P<height>\d+)\]$"
)
_GADGET_RE = re.compile(
    r"^  gadget id=(?P<id>\d+) role=(?P<role>\S+) "
    r'class="(?P<class>[^"]*)" label="(?P<label>[^"]*)"'
    r'(?: value="(?P<value>[^"]*)")?'
    r" \[(?P<left>-?\d+),(?P<top>-?\d+) (?P<width>\d+)x(?P<height>\d+)\]$"
)


@dataclass
class Gadget:
    gadget_id: int
    role: str
    class_name: str
    label: str
    value: str | None
    left: int
    top: int
    width: int
    height: int

    @property
    def text(self) -> str:
        """The gadget's assertable text: its live value if it has one
        (string/integer gadgets), else its label -- matches the
        server's own GETTEXT semantics (FindGadgetText in main.c)."""
        return self.value if self.value is not None else self.label


@dataclass
class Window:
    title: str
    left: int
    top: int
    width: int
    height: int
    gadgets: list[Gadget] = field(default_factory=list)

    def find(self, gadget_id: int) -> Gadget | None:
        return next((g for g in self.gadgets if g.gadget_id == gadget_id), None)


class TreeParseError(Exception):
    pass


def parse_tree(text: str) -> Window:
    """Parses one TREE payload (e.g. `Reply.text` from a `TREE ...`
    command) into a Window. Raises TreeParseError on any line that
    doesn't match the fixed format -- a mismatch means the server and
    client have drifted, which should fail loudly, not silently drop
    data."""
    lines = text.splitlines()
    if not lines:
        raise TreeParseError("empty TREE payload")

    m = _WINDOW_RE.match(lines[0])
    if m is None:
        raise TreeParseError(f"unrecognised window line: {lines[0]!r}")
    window = Window(
        title=m["title"],
        left=int(m["left"]),
        top=int(m["top"]),
        width=int(m["width"]),
        height=int(m["height"]),
    )

    for line in lines[1:]:
        if line == "":
            continue
        gm = _GADGET_RE.match(line)
        if gm is None:
            raise TreeParseError(f"unrecognised gadget line: {line!r}")
        window.gadgets.append(
            Gadget(
                gadget_id=int(gm["id"]),
                role=gm["role"],
                class_name=gm["class"],
                label=gm["label"],
                value=gm["value"],
                left=int(gm["left"]),
                top=int(gm["top"]),
                width=int(gm["width"]),
                height=int(gm["height"]),
            )
        )

    return window
