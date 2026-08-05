#!/usr/bin/env python3
"""docs2guide.py - build amipilot.guide from the userdocs/ pages.

userdocs/ in this repository is the single source of truth for user
documentation (built as the MkDocs site, see mkdocs.yml); this converts its
Markdown into one hyperlinked AmigaGuide document for on-Amiga reading
(AmigaGuide/MultiView, OS 2.x+).

Usage: docs2guide.py <userdocs-dir> <output.guide>

Deliberately a pragmatic subset-of-Markdown converter, tuned to how the
pages are actually written (see PAGES): headings, bold, inline code, fenced
code blocks, pipe tables, lists, blockquotes, [md](links) - links to sibling
.md pages become guide node links. Output is plain non-wordwrapped
AmigaGuide (works in every viewer back to 2.x): paragraphs are hard-wrapped
here, code/tables pass through verbatim.

Adapted verbatim (algorithm unchanged) from sibling projects amiauth/
sana2loop's own tools/docs2guide.py -- only PAGES, version(), and the
header/footer text below are amipilot-specific.
"""

import re
import sys
import os

WIDTH = 76

# Site nav order; (file-slug, node title). MAIN is index (the front page).
PAGES = [
    ('index', 'AmiPilot'),
    ('Installation', 'Installation'),
    ('Getting-Started', 'Getting Started'),
    ('AmiInspect-Reference', 'AmiInspect Reference'),
    ('Locator-Tiers-and-Limits', 'Locator Tiers and Limits'),
    ('Troubleshooting-and-FAQ', 'Troubleshooting and FAQ'),
    ('Building-and-Testing', 'Building and Testing'),
    ('Changelog', 'Changelog'),
]
SLUGS = {slug for slug, _ in PAGES}

# Non-Latin-1 transliteration (AmigaGuide is ISO-8859-1).
TRANSLIT = {
    '—': ' - ', '–': '-', '‘': "'", '’': "'",
    '“': '"', '”': '"', '…': '...', '→': '->',
    '≈': '~', '≤': '<=', '≥': '>=', ' ': ' ',
    '\U0001f7e2': '', '\U0001f7e0': '', '\U0001f534': '',  # LED emoji
    '\U0001f389': '',                                      # party popper
}


def latin1(s):
    for k, v in TRANSLIT.items():
        s = s.replace(k, v)
    return s.encode('latin-1', 'replace').decode('latin-1')


def esc(s):
    """Escape AmigaGuide specials in literal text."""
    return s.replace('\\', '\\\\').replace('@', '\\@')


def inline(s):
    """Markdown inline -> AmigaGuide markup. Escapes literal text as it goes."""
    s = re.sub(r'!\[[^\]]*\]\([^)]*\)', '', s)   # images have no guide analogue
    out = []
    i = 0
    # [text](url) / **bold** / *emphasis* / `code`
    # AmigaGuide has no italic markup used elsewhere in this converter, so
    # single-asterisk emphasis renders identically to **bold** (@{b}...@{ub})
    # rather than passing through as literal asterisks - group 3 (**bold**)
    # is tried first in the alternation, so it always wins at a `**` run;
    # group 4 only matches a lone `*` (no `*`/whitespace as its next char,
    # ruling out `**` and stray "3 * 4"-style spaced asterisks - userdocs
    # doesn't use `*` for anything else, e.g. no `* ` list bullets).
    pat = re.compile(
        r'\[([^\]]+)\]\(([^)]+)\)'            # 1: text, 2: url
        r'|\*\*([^*]+)\*\*'                   # 3: bold
        r'|\*([^*\s][^*]*?)\*'                # 4: single-asterisk emphasis
        r'|`([^`]+)`')                        # 5: code
    for m in pat.finditer(s):
        out.append(esc(s[i:m.start()]))
        if m.group(1) is not None:
            text = esc(m.group(1).replace('`', ''))
            url = m.group(2)
            page = re.fullmatch(r'([A-Za-z0-9-]+)\.md(?:#\S*)?', url)
            if page and page.group(1) in SLUGS:
                out.append('@{"%s" link "%s"}' % (text, page.group(1)))
            elif url.startswith('#'):
                out.append('@{b}%s@{ub}' % text)   # in-page anchor: text only
            else:
                out.append('%s <%s>' % (text, esc(url)))
        elif m.group(3) is not None:
            out.append('@{b}%s@{ub}' % esc(m.group(3)))
        elif m.group(4) is not None:
            out.append('@{b}%s@{ub}' % esc(m.group(4)))
        else:
            out.append(esc(m.group(5)))
        i = m.end()
    out.append(esc(s[i:]))
    return ''.join(out)


VISIBLE = re.compile(r'@\{"([^"]*)" link "[^"]*"\}|@\{u?b\}|@\{u?i\}')


def vislen(s):
    """Visible length of a converted chunk (guide markup is zero-width)."""
    def repl(m):
        return m.group(1) or ''
    return len(VISIBLE.sub(repl, s).replace('\\@', '@').replace('\\\\', '\\'))


def wrap(s, indent=0, hang=None):
    """Hard-wrap converted text at WIDTH visible columns.

    @{...} commands contain spaces (link markup) and must never be split
    across lines: protect their spaces with a sentinel around the split.
    """
    hang = indent if hang is None else hang
    s = re.sub(r'@\{[^}]*\}', lambda m: m.group(0).replace(' ', '\x00'), s)
    words = [w.replace('\x00', ' ') for w in s.split()]
    lines, cur, cw, first = [], [], 0, True
    for w in words:
        wl = vislen(w)
        pad = indent if first else hang
        if cur and pad + cw + 1 + wl > WIDTH:
            lines.append(' ' * (indent if first else hang) + ' '.join(cur))
            cur, cw, first = [w], wl, False
        else:
            cw += (1 if cur else 0) + wl
            cur.append(w)
    if cur:
        lines.append(' ' * (indent if first else hang) + ' '.join(cur))
    return lines


def table(rows):
    """Pipe-table rows -> aligned plain text."""
    cells = []
    for r in rows:
        parts = [c.strip() for c in r.strip().strip('|').split('|')]
        if all(re.fullmatch(r':?-+:?', p or '-') for p in parts):
            continue                      # separator row
        cells.append([inline(p) for p in parts])
    if not cells:
        return []
    ncol = max(len(r) for r in cells)
    for r in cells:
        r += [''] * (ncol - len(r))
    widths = [max(vislen(r[c]) for r in cells) for c in range(ncol)]
    if 2 + sum(widths) + 2 * (ncol - 1) <= WIDTH:
        out = []
        for idx, r in enumerate(cells):
            line = '  '.join(r[c] + ' ' * (widths[c] - vislen(r[c]))
                             for c in range(ncol)).rstrip()
            out.append('  ' + line)
            if idx == 0:                  # underline the header row
                out.append('  ' + '  '.join('-' * w for w in widths))
        return out
    # Too wide for one line: definition-list layout - first cell as the term
    # (emitted verbatim, so links stay clickable), the rest wrapped under it.
    # The header row is dropped; it carries no information in this shape.
    out = []
    for r in cells[1:]:
        out.append('  ' + r[0])
        out.extend(wrap(' - '.join(c for c in r[1:] if c), indent=6))
        out.append('')
    while out and out[-1] == '':
        out.pop()
    return out


def convert(md):
    """One page's Markdown body -> AmigaGuide lines."""
    lines = md.splitlines()
    out = []
    i = 0
    in_code = False
    while i < len(lines):
        ln = latin1(lines[i].rstrip())
        if ln.strip().startswith('```'):
            in_code = not in_code
            if in_code and (not out or out[-1] != ''):
                out.append('')
            i += 1
            continue
        if in_code:
            out.append('    ' + esc(ln))
            i += 1
            continue
        if not ln.strip():
            if out and out[-1] != '':
                out.append('')
            i += 1
            continue
        if ln.startswith('# '):
            i += 1
            continue                       # page title: the node line has it
        m = re.match(r'^(#{2,})\s+(.*)$', ln)
        if m:
            if out and out[-1] != '':
                out.append('')
            title = inline(m.group(2))
            out.append('@{b}%s@{ub}' % title)
            if len(m.group(1)) == 2:
                out.append('-' * min(WIDTH, vislen(title)))
            out.append('')
            i += 1
            continue
        if ln.lstrip().startswith('|'):
            rows = []
            while i < len(lines) and lines[i].lstrip().startswith('|'):
                rows.append(latin1(lines[i]))
                i += 1
            out.extend(table(rows))
            continue
        if ln.startswith('    ') and (not out or out[-1] == ''):
            out.append('    ' + esc(ln.strip()))   # indented code block
            while i + 1 < len(lines) and (
                    lines[i + 1].startswith('    ') or not lines[i + 1].strip()):
                i += 1
                nxt = latin1(lines[i].rstrip())
                out.append(('    ' + esc(nxt.strip())) if nxt.strip() else '')
            while out and out[-1] == '':
                out.pop()
            out.append('')
            i += 1
            continue
        m = re.match(r'^(\s*)[-*]\s+(.*)$', ln)
        if m:
            # gather the whole (possibly wrapped-in-source) list item
            item = m.group(2)
            depth = len(m.group(1))
            while (i + 1 < len(lines) and lines[i + 1].strip()
                   and not re.match(r'^\s*([-*#>|]|\d+\.)', lines[i + 1])
                   and not lines[i + 1].startswith('    ')):
                i += 1
                item += ' ' + latin1(lines[i].strip())
            out.extend(_bullet(' ' * depth, inline(item)))
            i += 1
            continue
        m = re.match(r'^>\s?(.*)$', ln)
        if m:
            quote = m.group(1)
            while i + 1 < len(lines) and lines[i + 1].startswith('>'):
                i += 1
                quote += ' ' + latin1(re.sub(r'^>\s?', '', lines[i]).strip())
            out.extend(wrap(inline(quote), indent=2))
            out.append('')
            i += 1
            continue
        # plain paragraph: merge following plain lines
        para = ln
        while (i + 1 < len(lines) and lines[i + 1].strip()
               and not re.match(r'^(\s*[-*#>|]|\s{4}|```|\d+\.)', lines[i + 1])):
            i += 1
            para += ' ' + latin1(lines[i].strip())
        out.extend(wrap(inline(para)))
        out.append('')
        i += 1
    while out and out[-1] == '':
        out.pop()
    return out


def _bullet(pre, body):
    ls = wrap(body, 0, 0)
    res = []
    for n, l in enumerate(ls):
        res.append(pre + ('- ' if n == 0 else '  ') + l)
    return res


def version(root):
    try:
        vmk = open(os.path.join(root, 'version.mk')).read()
        v = re.search(r'^VERSION\s*:?=\s*(\S+)', vmk, re.M).group(1)
        r = re.search(r'^REVISION\s*:?=\s*(\S+)', vmk, re.M).group(1)
        return '%s.%s' % (v, r), ''
    except Exception:
        return '0.1', ''


def main():
    if len(sys.argv) != 3:
        sys.exit('usage: docs2guide.py <userdocs-dir> <output.guide>')
    docsdir, outpath = sys.argv[1], sys.argv[2]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ver, vdate = version(root)

    head = [
        '@database amipilot',
        '@$VER: amipilot.guide %s (%s)' % (ver, vdate),
        '@author Simon Dick',
        '@(c) BSD 2-Clause; https://github.com/sidick/amipilot',
        '@width %d' % (WIDTH + 2),
        '',
    ]
    body = []
    for n, (slug, title) in enumerate(PAGES):
        path = os.path.join(docsdir, slug + '.md')
        md = open(path, encoding='utf-8').read()
        node = 'MAIN' if slug == 'index' else slug
        body.append('@node %s "%s"' % (node, latin1(title)))
        if n > 0:
            body.append('@toc MAIN')
        if 0 < n - 1 < len(PAGES):
            pass
        if n > 1:
            body.append('@prev %s' % PAGES[n - 1][0])
        elif n == 1:
            body.append('@prev MAIN')
        if n + 1 < len(PAGES):
            body.append('@next %s' % PAGES[n + 1][0])
        body.append('')
        if slug == 'index':
            body.append('@{b}AmiPilot %s@{ub} - object-level GUI '
                        'automation for classic AmigaOS' % ver)
            body.append('')
        body.extend(convert(md))
        body.append('')
        body.append('@endnode')
        body.append('')

    # Fix node cross-references: pages link with slugs; index's node is MAIN.
    text = '\n'.join(head + body)
    text = text.replace('link "index"', 'link "MAIN"')
    with open(outpath, 'w', encoding='latin-1', errors='replace') as f:
        f.write(text + '\n')
    print('%s: %d nodes, %d bytes' % (outpath, len(PAGES),
                                      os.path.getsize(outpath)))


if __name__ == '__main__':
    main()
