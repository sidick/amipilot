"""Parses the TREE text format -- the one payload shape the wire (and
ARexx, and AmiInspect) all emit for a window's gadget tree. Format is
fixed by `AppendGadgetLine`/`BuildTreeResult` in
server/src/amipilotserver/main.c (identical to AmiInspect's own
`PrintModel`, amiinspect/src/main.c):

    window "<title>" screen="<screen-title>" [<left>,<top> <width>x<height>]
      gadget id=<id> role=<ROLE> class="<class>" label="<label>" [value="<value>"] [<left>,<top> <width>x<height>]

This is plain text, not a grammar meant to grow -- see the plan's
no-JSON decision (docs/implementation-plan.md, "Protocol and client").
The parser here is a fixed-format reader, not a general one.

Quoted text fields (title, screen, class, label, value, and every
other quoted field this project's other parsers read) are escaped by
the server exactly like a C string literal: a literal quote becomes a
backslash-quote pair, and a literal backslash becomes a backslash pair
(`EscapeQuotes()`, server/src/amipilotserver/main.c) -- otherwise a
real Amiga gadget label or window title containing a literal quote
(e.g. a `3.5" Drive` label) would produce a well-formed response no
fixed-format regex could delimit correctly. `ESCAPED_FIELD`
and `unescape()` below are shared with this package's other parsers
(menu.py imports both) so every wire consumer treats quoting the same
way.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

# Matches the *escaped* contents of one quoted field: any run of
# characters that are neither a bare quote nor a bare backslash, or a
# backslash followed by any one character (an escape sequence) --
# NOT a bare `[^"]*`, which stops at the first embedded quote even
# when that quote is itself escaped (`\"`).
ESCAPED_FIELD = r'(?:[^"\\]|\\.)*'


def unescape(s: str) -> str:
    """Reverses the server's own `\\"`/`\\\\` escaping. Call this on
    every field captured via ESCAPED_FIELD before handing it back --
    the raw regex match still contains the escape sequences verbatim."""
    out = []
    i = 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            out.append(s[i + 1])
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


_WINDOW_RE = re.compile(
    rf'^window "(?P<title>{ESCAPED_FIELD})" screen="(?P<screen>{ESCAPED_FIELD})" '
    r"\[(?P<left>-?\d+),(?P<top>-?\d+) (?P<width>\d+)x(?P<height>\d+)\]$"
)
_GADGET_RE = re.compile(
    r"^  gadget id=(?P<id>\d+) role=(?P<role>\S+) "
    rf'class="(?P<class>{ESCAPED_FIELD})" label="(?P<label>{ESCAPED_FIELD})"'
    rf'(?: value="(?P<value>{ESCAPED_FIELD})")?'
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
    screen: str
    left: int
    top: int
    width: int
    height: int
    gadgets: list[Gadget] = field(default_factory=list)

    def find(self, gadget_id: int) -> Gadget | None:
        return next((g for g in self.gadgets if g.gadget_id == gadget_id), None)

    def find_by_role(
        self, role: str | None = None, label: str | None = None, index: int = 0
    ) -> Gadget | None:
        """Pure-Python equivalent of the wire's tier-2 ROLE=/LABEL=/
        INDEX= locator (server/include/arexx_cmd.h), for callers who
        already have a `tree()`/`TREE` model in hand and don't want a
        second round trip just to resolve one gadget's ID. `label` is
        matched case-sensitively as a substring (`in`), same
        convention the server's own strstr-based matching uses -- not
        case-insensitive. Returns None if fewer than `index + 1`
        gadgets match, same as `find()`'s own "no match" convention."""
        matches = [
            g
            for g in self.gadgets
            if (role is None or g.role == role) and (label is None or label in g.label)
        ]
        return matches[index] if 0 <= index < len(matches) else None


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
        title=unescape(m["title"]),
        screen=unescape(m["screen"]),
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
                class_name=unescape(gm["class"]),
                label=unescape(gm["label"]),
                value=unescape(gm["value"]) if gm["value"] is not None else None,
                left=int(gm["left"]),
                top=int(gm["top"]),
                width=int(gm["width"]),
                height=int(gm["height"]),
            )
        )

    return window
