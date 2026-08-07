# The AmiPilot manifest contract, format version 1

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
  guessing. This file documents version **1**.

## File format

Plain text (ASCII/ISO-8859-1), one record per line, LF or CR/LF line
endings. Fields are separated by one or more spaces or tabs. A field
containing spaces is double-quoted (`"..."`, no embedded-quote
escaping). Blank lines and lines starting with `;` or `#` are ignored.

Record types, in the order they must appear:

```
MANIFEST <format-version>
APP <name>
WINDOW <logical-name> <title-substring>
GADGET <logical-name> <window-logical-name> <ga-id>
```

- **`MANIFEST`** — must be the first record. `<format-version>` is a
  positive integer; this spec is version `1`. A consumer that doesn't
  speak the declared version must reject the whole file (with a clear
  error), not skim it for records it recognises.
- **`APP`** — the application's name, informational (error messages,
  tooling output). Exactly one.
- **`WINDOW`** — declares a logical window name and the title substring
  that locates it (the same first-match-wins substring matching
  AmiPilot's other locators use). At least one.
- **`GADGET`** — declares a logical gadget name: which logical window
  it lives in, and its `GA_ID` there. The window must have been
  declared before it.

Logical names are `[a-z0-9_]+` (lowercase by convention; consumers
match them case-insensitively). Names must be unique within their kind
(no two windows with the same logical name; no two gadgets either —
gadget names are globally unique in the file, not per-window, so a
script can say `@connect_button` without qualifying the window).

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

## Resolution semantics

Resolving a logical gadget name yields a `(title-substring, GA_ID)`
pair. The consumer then locates the window by title substring (first
match, across all screens) and the gadget by `GA_ID` within that
window's own gadget list — **at action time, against live structure**,
never cached. The manifest pins the *identity* of a target; it says
nothing about position, size, label, or ordering, which is exactly why
relayout and relabelling can't break it.

What a manifest can name is bounded by what AmiPilot's walker can reach:
a gadget invisible to structural walking (e.g. a `layout.gadget` child
on classic OS 3.x — see the project's documented limits) can't be
clicked by `GA_ID` no matter what the manifest says. An application
whose scriptable gadgets sit behind that limit needs to restructure
(attach them where they're reachable) before a manifest helps — the
manifest format deliberately has no way to express "unreachable but
trust me", because that would be a lie waiting to be shipped.

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
structural walking, but recording it doesn't make it reachable — the
same "no way to express 'unreachable but trust me'" honesty applies
regardless of who wrote the file.

## Versioning policy

- Format version bumps only for changes that would make a version-1
  parser misread a file (new record types, field-order changes). Adding
  a new *optional* record type is still a version bump — version-1
  consumers must be able to trust that a file they accept contains
  nothing they silently skipped.
- This spec lives at `manifest/SPEC.md` in the AmiPilot repository and
  is versioned with it; released spec versions never change meaning
  after the fact.
