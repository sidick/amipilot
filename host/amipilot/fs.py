"""Parses the FSLIST/FSSTAT entry line format -- the one payload shape
the wire emits for both verbs (a listing is just one-or-more of these
lines; FSSTAT is exactly one). Format is fixed by `AppendEntry` in
server/src/fs.c:

    entry name="<name>" type=file|dir size=<bytes> prot=<rwed> date="<DD-Mon-YY HH:MM:SS>" comment="<comment>"

Plain text, not a grammar meant to grow -- see model.py's own note
about the plan's no-JSON decision. FSMKDIR/FSDELETE return plain
human-readable text (not this format) and FSGET returns raw file
bytes, so none of those three need a parser here.

`name`/`comment` are escaped by the server exactly like a C string
literal (`EscapeQuotesInto()`, server/src/fs.c) -- a filename or
comment can legitimately contain a literal `"` (e.g. a `12" disk`
comment) -- so both use model.py's shared ESCAPED_FIELD/unescape()
rather than a bare `[^"]*`. `date` is server-formatted
(`DD-Mon-YY HH:MM:SS`) and never contains a quote, so it's left as a
plain `[^"]*`.
"""

from __future__ import annotations

import re
from dataclasses import dataclass

from .model import ESCAPED_FIELD, unescape

_ENTRY_RE = re.compile(
    rf'^entry name="(?P<name>{ESCAPED_FIELD})" type=(?P<type>file|dir) '
    r"size=(?P<size>\d+) prot=(?P<prot>[rwed-]{4}) "
    rf'date="(?P<date>[^"]*)" comment="(?P<comment>{ESCAPED_FIELD})"$'
)


@dataclass
class FsEntry:
    name: str
    is_dir: bool
    size: int
    prot: str
    date: str
    comment: str


class FsParseError(Exception):
    pass


def parse_fs_entries(text: str) -> list[FsEntry]:
    """Parses an FSLIST payload (zero or more entry lines) or an
    FSSTAT payload (exactly one). Raises FsParseError on any non-blank
    line that doesn't match -- a mismatch means the server and client
    have drifted, which should fail loudly."""
    entries = []
    for line in text.splitlines():
        if line == "":
            continue
        m = _ENTRY_RE.match(line)
        if m is None:
            raise FsParseError(f"unrecognised entry line: {line!r}")
        entries.append(
            FsEntry(
                name=unescape(m["name"]),
                is_dir=m["type"] == "dir",
                size=int(m["size"]),
                prot=m["prot"],
                date=m["date"],
                comment=unescape(m["comment"]),
            )
        )
    return entries


def parse_fs_entry(text: str) -> FsEntry:
    """FSSTAT's single-entry form -- raises FsParseError if the
    payload isn't exactly one entry line."""
    entries = parse_fs_entries(text)
    if len(entries) != 1:
        raise FsParseError(f"expected exactly one entry, got {len(entries)}: {text!r}")
    return entries[0]
