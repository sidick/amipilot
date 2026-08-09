# The AmiPilot manifest contract, format versions 1 and 2

A manifest is a small, machine-readable text file an application ships
alongside its binary, mapping **stable logical names** to the window
titles and `GA_ID`s its GUI actually uses. A test or automation script
that addresses `@connect_button` keeps working forever, regardless of
relabelling, relayout, or translation — tier 1 of AmiPilot's locator
model (see `docs/implementation-plan.md`, "Locator tiers"): zero runtime
cost to the application, zero fragility for the script.

Publishing this contract is itself an AmiPilot deliverable: any
application (or GUI-generating tool) can adopt it without linking
against, or even knowing about, any AmiPilot code. The only obligations
on the application side are the ones good GUI code has anyway — give
every scriptable gadget a stable `GA_ID`, and don't reuse IDs within a
window.

## Design constraints (why this format)

- **Parseable everywhere AmiPilot runs**: host Python, on-Amiga C on a
  plain 68000, and a plain ARexx script doing `PARSE VAR` must all be
  able to consume it without a JSON parser. Hence line-oriented records
  with whitespace-separated fields, not JSON/XML.
- **Diff-friendly and hand-writable**: an application author maintains
  this by hand next to their source; one record per line keeps diffs
  reviewable.
- **Versioned from day one**: the first record declares the format
  version. Consumers reject versions they don't speak rather than
  guessing. This file documents versions **1** and **2**.

## File format

Plain text (ASCII/ISO-8859-1), one record per line, LF or CR/LF line
endings. Fields are separated by one or more spaces or tabs. A field
containing spaces is double-quoted (`"..."`, no embedded-quote
escaping). Blank lines and lines starting with `;` or `#` are ignored.

Record types, in the order they must appear:

```
MANIFEST <format-version>
APP <name>
WHEREPORT <port-name>
WINDOW <logical-name> <title-substring>
GADGET <logical-name> <window-logical-name> <ga-id>
WHEREGADGET <logical-name> <window-logical-name>
```

- **`MANIFEST`** — must be the first record. `<format-version>` is a
  positive integer; this spec documents `1` and `2`. A consumer that
  doesn't speak the declared version must reject the whole file (with a
  clear error), not skim it for records it recognises. A version-`1`
  file must not contain `WHEREPORT`/`WHEREGADGET` records — those
  require version `2` (see "Versioning policy" below).
- **`APP`** — the application's name, informational (error messages,
  tooling output). Exactly one.
- **`WHEREPORT`** (version 2 only) — declares the name of the ARexx
  port this application exposes to answer `WHERE` queries (see "The
  cooperative geometry port" below). At most one; if present, it must
  appear before any `WHEREGADGET` record. Optional — a version-2 file
  with no `WHEREPORT` simply has no `WHEREGADGET` entries either.
- **`WINDOW`** — declares a logical window name and the title substring
  that locates it (the same first-match-wins substring matching
  AmiPilot's other locators use). At least one.
- **`GADGET`** — declares a logical gadget name: which logical window
  it lives in, and its `GA_ID` there. The window must have been
  declared before it.
- **`WHEREGADGET`** (version 2 only, requires a `WHEREPORT` declared
  above it) — declares a logical gadget name resolved not by `GA_ID`
  but by querying the declared `WHEREPORT` at action time (see below).
  No `GA_ID` field — there deliberately isn't one, since a
  `WHEREGADGET` exists precisely for gadgets with no reachable one.

Logical names are `[a-z0-9_]+` (lowercase by convention; consumers
match them case-insensitively). Names must be unique within their kind
(no two windows with the same logical name), and `GADGET`/`WHEREGADGET`
names share one namespace — no two gadget records of either kind may
share a name, gadget names are globally unique in the file, not
per-window, so a script can say `@connect_button` without qualifying
the window or caring which of the two record types resolved it.

## Example

```
; GTApp.manifest -- ships next to the GTApp binary.
MANIFEST 1
APP GTApp
WINDOW main "AmiPilot GadTools Fixture"
GADGET connect_button main 1
GADGET host_field main 2
GADGET enabled_checkbox main 3
```

A version-2 example, for an app whose gadgets sit behind a
`layout.gadget` wall (see "The cooperative geometry port" below):

```
; CAApp.manifest -- ships next to the CAApp binary.
MANIFEST 2
APP CAApp
WHEREPORT CAAPP.WHERE
WINDOW main "AmiPilot ClassAct Fixture"
WHEREGADGET connect_button main
WHEREGADGET host_field main
WHEREGADGET enabled_checkbox main
```

## Resolution semantics

Resolving a logical gadget name yields a `(title-substring, GA_ID)`
pair. The consumer then locates the window by title substring (first
match, across all screens) and the gadget by `GA_ID` within that
window's own gadget list — **at action time, against live structure**,
never cached. The manifest pins the *identity* of a target; it says
nothing about position, size, label, or ordering, which is exactly why
relayout and relabelling can't break it.

What a plain `GADGET` record can name is bounded by what AmiPilot's
walker can reach: a gadget invisible to structural walking (e.g. a
`layout.gadget` child on classic OS 3.x — see the project's documented
limits) can't be clicked by `GA_ID` no matter what the manifest says.
Version 1 of this format deliberately had no way to express
"unreachable but trust me", because that would have been a lie waiting
to be shipped. Version 2's `WHEREGADGET` record is the honest answer to
that limit, not an exception to it: it doesn't ask AmiPilot to trust an
unreachable `GA_ID` — it names a gadget whose *geometry* the
application itself will report, live, on request. See "The cooperative
geometry port" below.

Resolving a `WHEREGADGET` name yields `(title-substring, wherePort)`
instead of `(title-substring, GA_ID)`. The consumer locates the window
by title substring exactly as above, then queries `wherePort` for the
gadget's current geometry — again at action time, against the live
application, never cached.

## Naming and shipping conventions

- File name: `<AppName>.manifest`, shipped in the same directory as the
  application binary (so `Prog:MyApp` has `Prog:MyApp.manifest`).
- The manifest describes the application's *current* GUI. It changes in
  the same commit that changes a `GA_ID` or adds a scriptable gadget —
  treating it as release documentation that can drift is exactly the
  failure mode logical names exist to prevent.
- Removing a logical name is a breaking change to every script using
  it; renaming a gadget's on-screen label is not a change at all. This
  asymmetry is the entire point.

## The cooperative geometry port (WHERE)

A `window.class`/`layout.gadget` window attaches only its single
top-level layout object to `window->FirstGadget` — the layout's own
button/string/checkbox children aren't individually walkable, and
there's no public API to enumerate them on classic OS 3.x (see the
project's documented "Confirmed limit"). This blocks a plain `GADGET`
record for any such gadget, permanently — no manifest can fix an
enumeration limit.

The escape hatch: the application itself already holds a live object
pointer to every gadget it created (it needs them for its own event
dispatch). It can expose a small, optional ARexx port that answers a
`WHERE <logical-name>` query by calling
`GetAttr(GA_Left/GA_Top/GA_Width/GA_Height)` on its own object and
reporting the live, current geometry back. AmiPilot resolves a
`WHEREGADGET` name to that geometry and then acts with a genuine
`input.device` click at the resolved coordinates — discovery is
cooperative (the app tells AmiPilot where things are), but **actuation
stays real input through the real event path**, the same as every
other locator tier. No coordinates ever appear in a script; they are
resolved live, at action time, by the application itself, so relayout
and font changes can't break anything — the same immunity a plain
manifest already gives `GA_ID`-addressed gadgets, extended to the one
place structural walking can't reach.

### Request

A standard ARexx command message (`RXCOMM|RXFF_RESULT`, the same
message shape ARexx's own `ADDRESS` mechanism uses), with the command
string:

```
WHERE <logical-name>
```

`<logical-name>` matches case-insensitively and is otherwise passed
through verbatim (no quoting rules beyond ARexx's own).

### Reply

- **Success**: `rm_Result1` (the RC) is `0`; `rm_Result2` is an
  argstring of exactly four whitespace-separated decimal integers,
  `"<x> <y> <w> <h>"`, and nothing else. Units are pixels, relative to
  the gadget's own window's top-left corner **including the window's
  border and title bar** — i.e. exactly what
  `GetAttr(GA_Left/GA_Top/GA_Width/GA_Height)` already returns; a
  consumer converts to screen coordinates by adding the window's own
  `LeftEdge`/`TopEdge` and nothing else (do not add `BorderLeft`/
  `BorderTop` — they're already folded in).
- **Unknown name**: `rm_Result1` is nonzero (`10` by convention,
  matching this project's own RC scale); `rm_Result2` may optionally
  carry a short reason string.
- The application must reply to every `WHERE` message it receives, from
  the same task that owns the objects being queried (an ARexx message
  handled off-task risks reading geometry mid-relayout). A sub-second
  response is expected — AmiPilot's own default query timeout is 10
  seconds, generous enough for a busy app but not for a hung one.

### Clash guard: pick a dedicated port name

`WHERE` is an ordinary ARexx command string on whatever port the
manifest's `WHEREPORT` record names — nothing about this contract
reserves the word globally. If an application points `WHEREPORT` at a
general-purpose port that also implements its own command vocabulary,
a genuine collision (an existing `WHERE` command doing something else
entirely) is possible, and would silently misdirect every `WHEREGADGET`
click on that app. To make that risk structural rather than incidental:

- **An application implementing this contract MUST make `WHERE` behave
  exactly as specified above on the port its manifest names** — it may
  not repurpose the word for something else on that same port.
- **Applications SHOULD expose a dedicated port for this purpose**
  (e.g. `<APPNAME>.WHERE`, matching the naming convention this spec's
  own example manifest uses) rather than reusing a port that already
  serves a broader, app-specific command set — the smaller the
  vocabulary sharing that port, the smaller the chance any future
  command it gains collides with this one.
- AmiPilot's own port resolution for `WHERE` queries matches the
  declared port name **exactly** — it does not apply MUIREXX's own
  `<base>.1` fallback probe (a MUI-specific naming convention that
  doesn't apply here), so a manifest that names the wrong port fails
  loudly (port-not-found) rather than silently guessing at a related
  one. Combined with the strict four-integer reply format above, a
  reply that doesn't parse as expected is a hard error, not a silent
  misclick — an accidental clash with an unrelated `WHERE` command on
  the same port is far more likely to surface as an obvious rejected
  reply than as a plausible-looking wrong coordinate.

## Quirk profiles: the same format for apps you don't control

Nothing about the `MANIFEST <path>` wire verb (`server/WIRE.md`,
`userdocs/ARexx-Reference.md`) requires the file it loads to have been
authored, or even known about, by the application itself — it just
parses whatever version-1 manifest sits at that path. That means this
exact format also serves a second, equally real use case the app-
author story above doesn't cover: driving a third-party application
you have no source access to, where you've worked out the window
titles and `GA_ID`s yourself (by hand, or via `amipilot dump --format
python`, which already emits manifest-ready `# name = <id>` name
suggestions — see `host/README.md`). AmiPilot calls a manifest written
this way, by a user or the community rather than the app's own
developer, a **quirk profile** — same `MANIFEST`/`APP`/`WINDOW`/
`GADGET` records, same loader, same versioning rules above. There is
deliberately no second file format or parser for this: one grammar,
authored by whoever happens to know the target's structure, is less
machinery than two.

What a quirk profile adds beyond the plain mapping is a documented
convention for **known-oddity notes**: a `;`/`#` comment line placed
directly above (or after) the `WINDOW`/`GADGET` record it concerns,
recording something a script author would otherwise have to
rediscover the hard way. These are ordinary comment lines — already
ignored by every consumer per the "File format" section above — so
this adds no new syntax, only a convention for where to put them:

```
; quirk: gadtools.library's BUTTON_KIND never populates GadgetText
; under any PLACETEXT_* value -- LABEL= can't find this button.
; Located by GA_ID here for exactly that reason.
GADGET ok_button main 7

; quirk: this field only settles ~2s after the async request opens --
; a script clicking it immediately should WAITFOR its TEXT= first
; rather than assume it's populated on the window's first appearance.
GADGET status_field req_window 3
```

A quirk profile is not a substitute for a manifest an app ships
itself — if the app later adopts the contract, prefer that copy, since
it changes in the same commit as the `GA_ID`s it describes and a
third-party file can silently drift out of date. The "Resolution
semantics" section's limit above applies here too, and matters more:
a quirk profile can *record* a `GA_ID` for a gadget invisible to
structural walking, but recording it doesn't make it reachable — a
plain `GADGET` record naming such a gadget is still a lie waiting to be
shipped, `WHEREGADGET` or not. A version-2 quirk profile can name a
`WHEREGADGET` only for an application that genuinely implements the
`WHERE` port itself; a third party cannot retrofit cooperative geometry
onto a binary that doesn't offer it.

## Versioning policy

- Format version bumps only for changes that would make an older
  parser misread a file (new record types, field-order changes). Adding
  a new *optional* record type is still a version bump — a version-1
  consumer must be able to trust that a file it accepts contains
  nothing it silently skipped. Version 2 added `WHEREPORT`/
  `WHEREGADGET`; a version-1 file must not contain either record, and a
  consumer that only speaks version 1 correctly rejects any file that
  declares version 2.
- This spec lives at `manifest/SPEC.md` in the AmiPilot repository and
  is versioned with it; released spec versions never change meaning
  after the fact.
