"""Parses the MENU text format -- the payload shape the wire emits for
a window's menu strip. Format is fixed by `BuildMenuResult`/
`AppendMenuItemLine` in server/src/amipilotserver/main.c (identical to
AmiInspect's own `PrintMenus`, amiinspect/src/main.c):

    window "<title>" screen="<screen-title>" [<left>,<top> <width>x<height>]
    menu num=<n> title="<title>" enabled=<0|1>
      item num=<menu>/<item> text="<text>" [shortcut=<c>] checkit=<0|1> checked=<0|1> enabled=<0|1>
        subitem num=<menu>/<item>/<sub> text="<text>" [shortcut=<c>] checkit=<0|1> checked=<0|1> enabled=<0|1>

Plain text, not a grammar meant to grow -- see model.py's own note
about the plan's no-JSON decision. `menu_num`/`item_num`/`sub_num` are
the same 0-based chain positions `menu_pick()` addresses a pick by,
and what Intuition itself reports via IDCMP_MENUPICK's MENUNUM()/
ITEMNUM()/SUBNUM() macros.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

from .model import ESCAPED_FIELD, _WINDOW_RE, unescape

_MENU_RE = re.compile(
    rf'^menu num=(?P<num>\d+) title="(?P<title>{ESCAPED_FIELD})" enabled=(?P<enabled>[01])$'
)
_ITEM_RE = re.compile(
    r"^  item num=(?P<menu>\d+)/(?P<item>\d+)"
    rf' text="(?P<text>{ESCAPED_FIELD})"(?: shortcut=(?P<shortcut>\S))?'
    r" checkit=(?P<checkit>[01]) checked=(?P<checked>[01]) enabled=(?P<enabled>[01])$"
)
_SUBITEM_RE = re.compile(
    r"^    subitem num=(?P<menu>\d+)/(?P<item>\d+)/(?P<sub>\d+)"
    rf' text="(?P<text>{ESCAPED_FIELD})"(?: shortcut=(?P<shortcut>\S))?'
    r" checkit=(?P<checkit>[01]) checked=(?P<checked>[01]) enabled=(?P<enabled>[01])$"
)


@dataclass
class MenuItem:
    menu_num: int
    item_num: int
    sub_num: int | None  # None for a top-level item, else a submenu position
    text: str
    shortcut: str | None
    checkit: bool
    checked: bool
    enabled: bool
    sub_items: list["MenuItem"] = field(default_factory=list)


@dataclass
class Menu:
    menu_num: int
    title: str
    enabled: bool
    items: list[MenuItem] = field(default_factory=list)


@dataclass
class MenuStrip:
    window_title: str
    screen: str
    menus: list[Menu] = field(default_factory=list)

    def find(self, text: str) -> MenuItem | None:
        """First item or subitem (depth-first) whose text matches
        exactly -- for picking a menu entry by its label rather than
        by hand-counting chain positions."""
        for menu in self.menus:
            for item in menu.items:
                if item.text == text:
                    return item
                for sub in item.sub_items:
                    if sub.text == text:
                        return sub
        return None


class MenuParseError(Exception):
    pass


def _parse_item(m: "re.Match[str]", sub_num: int | None) -> MenuItem:
    return MenuItem(
        menu_num=int(m["menu"]),
        item_num=int(m["item"]),
        sub_num=sub_num,
        text=unescape(m["text"]),
        shortcut=m["shortcut"],
        checkit=m["checkit"] == "1",
        checked=m["checked"] == "1",
        enabled=m["enabled"] == "1",
    )


def parse_menu_strip(text: str) -> MenuStrip:
    """Parses one MENU payload (e.g. `Reply.text` from a `MENU ...`
    command) into a MenuStrip. Raises MenuParseError on any line that
    doesn't match the fixed format."""
    lines = text.splitlines()
    if not lines:
        raise MenuParseError("empty MENU payload")

    wm = _WINDOW_RE.match(lines[0])
    if wm is None:
        raise MenuParseError(f"unrecognised window line: {lines[0]!r}")
    strip = MenuStrip(window_title=unescape(wm["title"]), screen=unescape(wm["screen"]))

    current_menu: Menu | None = None
    current_item: MenuItem | None = None

    for line in lines[1:]:
        if line == "":
            continue
        mm = _MENU_RE.match(line)
        if mm is not None:
            current_menu = Menu(
                menu_num=int(mm["num"]), title=unescape(mm["title"]),
                enabled=mm["enabled"] == "1"
            )
            current_item = None
            strip.menus.append(current_menu)
            continue

        im = _ITEM_RE.match(line)
        if im is not None:
            if current_menu is None:
                raise MenuParseError(f"item line before any menu line: {line!r}")
            current_item = _parse_item(im, sub_num=None)
            current_menu.items.append(current_item)
            continue

        sm = _SUBITEM_RE.match(line)
        if sm is not None:
            if current_item is None:
                raise MenuParseError(f"subitem line before any item line: {line!r}")
            current_item.sub_items.append(_parse_item(sm, sub_num=int(sm["sub"])))
            continue

        raise MenuParseError(f"unrecognised menu line: {line!r}")

    return strip
