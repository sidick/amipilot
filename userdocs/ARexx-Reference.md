# ARexx Reference

`AmiPilotServer` is a small commodity that hosts AmiPilot's action engine
(genuine `input.device` click/type synthesis) and the `intuition-model`
walker behind a public ARexx port — so an ARexx script running on the
*same* Amiga can locate and drive another program's GUI. No host
machine, transport, or emulator involved.

## Starting it

```
> Run SRC:AmiPilotServer
```

(or wherever you've copied it — see [Installation](Installation.md)). It
opens an ARexx port named `AMIPILOT.1` (or the next free slot —
`AMIPILOT.2`, `AMIPILOT.3`, ... — if more than one instance is running)
and prints the name it actually got:

```
AmiPilotServer: ARexx port AMIPILOT.1 ready
```

It has no window of its own; it just sits waiting for ARexx commands
until it receives `QUIT` or a break signal (Ctrl-C).

## Commands

Every command's first argument is a **window-title substring** — the
same matching `AmiInspect WINDOW=` uses, across every screen, first
match wins. Quote it if it contains spaces. `TREE`/`CLICK`/`TYPE`/
`GETTEXT`/`MENU`/`MENUPICK`'s classic form additionally accept an
optional leading `SCREEN=<substring>` token right after the command
keyword, narrowing the search to screens whose own name contains it —
for disambiguating two same-titled windows on different screens; see
[Screens](Wire-Protocol.md#screens).

| Command | Arguments | What it does |
|---------|-----------|---------------|
| `TREE` | `<window-pattern>` | Returns the matched window's full gadget tree as a multi-line string, in the same format `AmiInspect` prints (`RESULT` gains embedded newlines — most REXX interpreters handle that fine in a variable). |
| `CLICK` | `<window-pattern> (<gadget-id> \| ROLE=<r> [LABEL=<l>] [INDEX=<n>]) [EXPECT=WINDOW=<pattern> \| EXPECT=NOWINDOW] [TIMEOUT=<n>]` | Clicks the target gadget — a genuine `input.device` click, not a shortcut. Either a numeric `GA_ID`, or a [tier-2 locator](#tier-2-semantic-locators) below. Optional `EXPECT=` composes the click atomically with a server-side wait — see [Wait/expectation primitives](#waitexpectation-primitives). |
| `TYPE` | `<window-pattern> (<gadget-id> \| ROLE=<r> [LABEL=<l>] [INDEX=<n>]) <text...>` | Clicks the gadget (to focus it), then types `text` into it via real `IECLASS_RAWKEY` events, human-paced. Everything after the locator is taken verbatim as the text — no quoting needed unless the text itself starts with `"`. |
| `GETTEXT` | `<window-pattern> (<gadget-id> \| ROLE=<r> [LABEL=<l>] [INDEX=<n>])` | Returns that gadget's current text: a string/integer gadget's live value if it has one, otherwise its label. |
| `MANIFEST` | `<file-path>` | Loads an application's manifest (see below). Replaces any previously loaded one. `RESULT` reports what loaded (`loaded GTApp: 1 windows, 3 gadgets`); a rejected manifest returns `RC=10` with the reason (including line number) in `RESULT`. |
| `VERSION` | (none) | Returns the server version, wire-protocol number, and the stable/experimental verb lists (multi-line, same payload the [wire handshake](Wire-Protocol.md) uses) — for feature-testing from a script. |
| `LAUNCH` | `[STACK=<n>] <command-line...>` | Starts `command-line` as an AmigaDOS process (asynchronous — the commodity keeps servicing the port while it runs). `STACK` sets the new process's stack in bytes (default 4000, AmigaDOS's own `CreateNewProc()` default — most Intuition/ReAction GUI apps need more). `RC=0` means the process itself could be created, **not** that the command was found or ran successfully; assert on the expected effect (a window appearing) instead. See [Wire Protocol](Wire-Protocol.md#launch) for the full contract and its honest limits. |
| `WBLAUNCH` | `<icon-path> [TOOLTYPE=<key>=<value> ...] [ARG=<path> ...]` | Launches `<icon-path>` (WITHOUT ".info") as if its icon had been double-clicked — a real, hand-built Workbench-style start (genuine `WBStartup`/`WBArg` message), not `LAUNCH`'s Shell-style one. `TOOLTYPE=` overrides (or adds) one tooltype for this launch only, everything else on the real icon left alone; `ARG=` adds further project-file arguments. Same async honesty as `LAUNCH`: `RC=0` means the process was created and the startup message queued, not that it finished starting. See [Wire Protocol](Wire-Protocol.md#wblaunch) for the full mechanism. |
| `SCREENSHOT` | `[SCREEN=<substring>] [WINDOW=<pattern>]` | Raw, uncompressed bitmap capture (planar, or Picasso96/RTG when a real P96 board is present — optional, never required, see [issue #44](https://github.com/sidick/amipilot/issues/44)) — inspector tooling for human viewing/debugging, not a locator mechanism. With both omitted, captures the frontmost screen; `WINDOW=` captures that window's owning screen in full, plus its rectangle for cropping (the host client crops to just that window by default). Host-side (`amipilot.screenshot`) turns the raw capture into a real `.iff` (IFF ILBM) and `.png` — true-colour PNG (no palette) for a P96 truecolor/hicolor capture, which has no ILBM equivalent. See [Wire Protocol](Wire-Protocol.md#screenshot) for the full payload layout and serial transfer-time estimates. |
| `FSLIST` | `<path>` | Lists a directory's entries (name, file/dir, size, protection, date, comment). Only works inside a root granted at startup — see [File API](Wire-Protocol.md#file-api). |
| `FSSTAT` | `<path>` | The metadata for a single file or directory, without listing its contents. |
| `FSMKDIR` | `<path>` | Creates a directory. Its parent must already exist and be inside a granted root. |
| `FSDELETE` | `<path>` | Deletes a file or empty directory. |
| `FSGET` | `<path>` | Returns a file's full contents (`RESULT` is the raw bytes, capped at the server's own small internal buffer — a test-staging channel, not a file manager). `FSPUT` (the opposite direction) is **not** listed here: it's wire-only, not answerable over ARexx at all — see [File API](Wire-Protocol.md#file-api). |
| `SCREENS` | (none) | Lists every open screen: title, position, size, and whether it's frontmost. Title is each screen's own name (`DefaultTitle`), not the live title-bar text a window's `WA_ScreenTitle` can override. |
| `MENU` | `<window-pattern>` | Returns the matched window's full menu strip — every pulldown menu, its items, and (one level deep) their submenu items, with checkit/checked/enabled state and any keyboard shortcut, in the same text shape `AmiInspect` prints. |
| `MENUPICK` | `<window-pattern> <menu-num> <item-num> [<sub-num>]` | Selects a menu item — the numbers are the same 0-based chain positions `MENU`'s own output reports. Items with a keyboard shortcut are picked via Right-Amiga + the shortcut character; items with none are picked via a genuine synthesized RMB-down/move/RMB-up sequence instead, automatically. `RC=20` if the item is disabled, or (pointer path only) if the window traps the right mouse button (`WFLG_RMBTRAP` — no synthesized RMB-down can ever open its menu). See [Wire Protocol](Wire-Protocol.md#menus) for the full contract. |
| `DRAG` | `<window-pattern> <locator> <dx> <dy>` or `<window-pattern> <locator> TO (<dest-gadget-id> \| @<dest-name>)` | A genuine press/move/release drag. The offset form (`<dx> <dy>`) moves the gadget's current center by a pixel delta — the natural shape for a slider/scroller. The `TO` form drags onto a second gadget's center instead, both resolved live, for drag-and-drop/reorder — the destination must be in the same window as the source. `<locator>` is the same numeric `GA_ID`, `ROLE=`/`LABEL=`/`INDEX=`, or `@name` form CLICK/TYPE/GETTEXT accept. |
| `WINDOWMOVE` | `[SCREEN=<s>] <window-pattern> <dx> <dy>` | Moves the WHOLE window by a pixel offset — a genuine title-bar drag (`WFLG_DRAGBAR`), built on the same primitive `DRAG`'s gadget forms use. Classic locator form only, no `@name` — a window-level action, same scope as `TREE`/`MENU`. `RC=20` ("window has no drag bar") if the window never had one. No separate "get position" verb — `TREE`'s own `[left,top WxH]` header already carries it. |
| `WINDOWSIZE` | `[SCREEN=<s>] <window-pattern> <width> <height>` | Resizes the WHOLE window to an ABSOLUTE target size — a genuine sizing-gadget drag (`WFLG_SIZEGADGET`) from its current bottom-right corner. Doesn't pre-check against the window's own min/max — Intuition clamps the drag as it would a real one; confirm the actual result with a follow-up `TREE`. `RC=20` ("window has no sizing gadget") if the window never had one. Same classic-form-only scope as `WINDOWMOVE`. |
| `WAITFOR` | `[SCREEN=<s>] WINDOW=<pattern> [TIMEOUT=<n>]` or `[SCREEN=<s>] NOWINDOW=<pattern> [TIMEOUT=<n>]` or `[SCREEN=<s>] <window-pattern> (<gadget-id> \| ROLE=<r> [LABEL=<l>] [INDEX=<n>]) TEXT=<value> [TIMEOUT=<n>]` or `@<name> TEXT=<value> [TIMEOUT=<n>]` or `[SCREEN=<s>] REQUESTER [TIMEOUT=<n>]` | Polls (server-side, one round trip) until a window matching `<pattern>` appears, until none does, until a gadget's text exactly equals `<value>`, or until a genuine Intuition Requester appears (window-attached only — issue #52; once detected, its own gadgets are reachable via ordinary `CLICK` against the same window pattern, no separate verb needed — see below). `TIMEOUT` defaults to 10 seconds. `RC=15` if the condition never becomes true in time — a new RC distinct from "nothing matched" (`RC=5`) or "the action itself failed" (`RC=20`). See [Wait/expectation primitives](#waitexpectation-primitives). |
| `AUTH` | `<password>` | Authenticates a TCP connection (no effect on ARexx or serial.device, which have their own implicit trust boundaries). Until it succeeds, the TCP transport refuses every command except `VERSION`/`AUTH`/`QUIT` with `RC=10`. See [Securing TCP](Wire-Protocol.md#securing-tcp). |
| `MUIREXX` | `<app-base> [TIMEOUT=<n>] <command...>` | Sends `<command>` verbatim to a MUI application's own ARexx port. The MUI-ARexx bridge tier — see [Driving MUI applications](#driving-mui-applications). |
| `WHERE` | `@<name> [TIMEOUT=<n>]` | Diagnostic query of the cooperative geometry port: returns `<name>`'s current `"<x> <y> <w> <h>"` geometry from the application's own declared `WHEREPORT`. Manifest-only, `WHEREGADGET` names only — see [Driving layout.gadget-only applications](#driving-layoutgadget-only-applications). |
| `QUIT` | (none) | Shuts the commodity down cleanly. |

The same command set is also reachable from a host machine over
serial.device — see [Wire Protocol](Wire-Protocol.md).

## Manifest locators

If the application you're driving ships an
[AmiPilot manifest](https://github.com/sidick/amipilot/blob/main/manifest/SPEC.md)
— a small text file mapping stable logical names to its window titles
and `GA_ID`s — load it with `MANIFEST`, and `CLICK`/`TYPE`/`GETTEXT`
then accept `@<logical-name>` in place of the
`<window-pattern> <gadget-id>` pair:

```rexx
'MANIFEST Prog:MyApp.manifest'
'TYPE @host_field aminet.net'
'CLICK @connect_button'
```

No `GA_ID`, window title, or position appears in the script at all —
the manifest pins the *identity* of each target, and the actual window
and gadget are still located live at action time. Relayout, relabelling,
and translation can't break the script; only a `GA_ID` change would
touch anything, and then only the manifest.

Using `@name` with no manifest loaded, or with a name the manifest
doesn't define, is `RC=10` with the reason in `RESULT`.

A format-version-2 manifest can also declare a name via `WHEREGADGET`
instead of `GADGET` — for a gadget structural walking can never reach
at all (a `layout.gadget` child on classic OS 3.x). `CLICK @name`/
`TYPE @name` work exactly the same either way; see [Driving
layout.gadget-only applications](#driving-layoutgadget-only-applications)
for what's different underneath and `GETTEXT`/`DRAG`'s own honest
limit against such a name.

## Tier-2 semantic locators

For an application with no manifest, `CLICK`/`TYPE`/`GETTEXT` also
accept a locator by **role and label text** in place of the bare
`GA_ID` — one or more of `ROLE=<name>`, `LABEL=<substring>`, and
`INDEX=<n>`, at least one of `ROLE=`/`LABEL=` required:

```rexx
'CLICK GadTools ROLE=button LABEL=Connect'
'GETTEXT GadTools ROLE=string LABEL=Host'
```

`ROLE=` matches the same role name `TREE`'s own output prints for a
gadget (`button`, `string`, `checkbox`, `slider`, ... — case doesn't
matter). `LABEL=` is a **case-sensitive** substring match against the
gadget's label, quoted the same way a window pattern is if it
contains a space (`LABEL="Connect Now"`) — same convention window/
screen patterns already use, not a separate rule to learn. `INDEX=`
(0-based, default the first match) picks between several gadgets that
share a role and/or label, e.g. `ROLE=button INDEX=1` for the second
button in a window.

This is honest and useful, but fragile against relabelling in a way
`@name` isn't — prefer a manifest when the target application ships
one. No match (nothing with that role/label, or `INDEX=` past the
last match) is `RC=5`, the same class an unmatched `GA_ID` already
uses.

## Wait/expectation primitives

A naive "click, then immediately check" ordering can race — the app
hasn't necessarily processed the click's event yet by the time your
next command runs. `WAITFOR` and `CLICK`'s own `EXPECT=` close that
race by polling **server-side**, in the same request, instead of the
script polling over and over from the host:

```rexx
'WAITFOR WINDOW=Preferences TIMEOUT=15'
'CLICK GadTools 1 EXPECT=WINDOW=Async TIMEOUT=15'
'CLICK GadTools 1 EXPECT=NOWINDOW'
```

`WAITFOR [SCREEN=<substring>] WINDOW=<pattern> [TIMEOUT=<n>]` waits
for a window matching `<pattern>` to appear; `NOWINDOW=<pattern>`
waits for none to match. `TIMEOUT` (seconds) defaults to 10.

`CLICK` additionally accepts a trailing `EXPECT=WINDOW=<pattern>` or
bare `EXPECT=NOWINDOW` (no argument) — the click still always happens
either way. The two `NOWINDOW` forms mean subtly different things,
deliberately: `CLICK ... EXPECT=NOWINDOW` means "the window *this
click itself* just acted on has closed" — precise, because the server
already knows exactly which window that was. `WAITFOR NOWINDOW=<x>`
has no click to anchor to, so it means "no window currently matches
`<x>`" — a fresh search each time, which in principle a different,
similarly-titled window could satisfy without the original ever
having closed. Use `CLICK ... EXPECT=NOWINDOW` when you want "the
thing I just clicked away is gone," and `WAITFOR NOWINDOW=<pattern>`
when there's no click to anchor to at all.

A condition that never becomes true in time is `RC=15` — a distinct
RC from "nothing matched at all" (`RC=5`) or "the action's own
input.device injection failed" (`RC=20`), since none of those mean the
same thing to a test author deciding how to react.

`WAITFOR` also has a third condition, for waiting on a gadget's
*text* rather than a window:

```rexx
'CLICK GadTools 4'
'WAITFOR GadTools 2 TEXT="cancel clicked" TIMEOUT=15'
'WAITFOR @host_field TEXT="hello wire" TIMEOUT=15'
```

`WAITFOR [SCREEN=<s>] <window-pattern> (<gadget-id> | ROLE=<r>
[LABEL=<l>] [INDEX=<n>]) TEXT=<value> [TIMEOUT=<n>]` (or `WAITFOR
@<name> TEXT=<value>`) polls the same gadget-text `GETTEXT` would read
until it exactly equals `<value>` — not a substring match, since a
partial match could be satisfied by an intermediate state on the way
to the final one (e.g. "loading..." matching before "loaded" does).
This condition is `WAITFOR`-only, not available on `CLICK`'s
`EXPECT=`, because the gadget whose text changes as a result of a
click is often a *different* gadget than the one clicked.

`WAITFOR` also has a fourth condition, for detecting an Intuition
Requester (GitHub issue #52's original "cheap first step" — a way to
know a modal requester appeared):

```rexx
'CLICK GadTools 7'
'WAITFOR REQUESTER TIMEOUT=10'
```

`WAITFOR [SCREEN=<s>] REQUESTER [TIMEOUT=<n>]` polls until ANY
currently-open window (optionally narrowed by `SCREEN=`) shows a
genuine Intuition Requester, or `TIMEOUT` (default 10s) elapses. No
pattern argument — a requester generally has no title of its own to
match against, so "any requester, anywhere" (or on a matching screen)
is the right granularity for a detection-only primitive.

**Window-attached Requesters only, by design:** a system-wide
Requester with no owning window (a disk-swap prompt, a DOS error, a
Guru) is a genuinely harder detection problem — `BuildSysRequest()`'s
own autodoc says these *can* come back as a standalone window, but
reliably telling that apart from an ordinary app window needs more
research than this pass covers. Confirmed live against a real,
`AutoRequest()`-triggered requester (`fixtures/gadtools-app`,
`tests/copperline/run.sh`'s `run_requester_check`): detection does
NOT rely on `window->FirstRequest` alone (checked, but `AutoRequest()`/
`BuildSysRequest()`/`EasyRequest()` never populate it when given a
real owning window on this project's target OS/ROM — they open a
genuinely separate window instead, confirmed against
`BuildSysRequest()`'s own autodoc text: "a new window is opened in the
same screen as the one containing your window") — the actual signal
is that separate window sharing its owning window's exact title text,
which no ordinary well-behaved app window does on its own. Full
technical story in `WaitForRequesterPresent()`'s own comment,
`server/src/amipilotserver/main.c`.

**Acting on a window-owned requester needs no new verb** (issue #52
follow-up): since it's a genuinely separate `struct Window` sharing
its owner's exact title, ordinary `CLICK <window-pattern> <gadget-id>`
already reaches it. `BuildSysRequest()`'s own autodoc documents a
fixed, app-independent `GadgetID` convention: the positive/"Yes"
gadget is always `GadgetID` `TRUE` (1), the negative/"No" gadget is
always `FALSE` (0) — so `CLICK <same-pattern> 1` reliably dismisses
ANY window-owned system requester with the positive choice:

```rexx
'CLICK GadTools 7'
'WAITFOR REQUESTER TIMEOUT=10'
'CLICK GadTools 1'
```

Confirmed live (`tests/copperline/requester-test.py`'s
`REQUESTER-YES-CLICKED`/`REQUESTER-DISMISSED` checks) — a real dismiss,
verified by `WAITFOR REQUESTER` correctly timing out again afterward.
One real limit remains: the system-wide (no owning window) case — a
real disk-swap/DOS-error/Guru requester — is still detection-only,
since it opens with no known title to pattern-match against at all.

**`GETTEXT` cannot read a Requester's body text — confirmed as a
permanent limit, not left open.** `struct Requester->ReqText` (a
`struct IntuiText *`) is where `BuildSysRequest()`'s own autodoc says
the body text ends up — but that field lives on a `struct Requester`
this project confirmed (2026-08-09) genuinely does not exist anywhere
reachable here: dumping `FirstRequest`/`ReqCount` for every open
window while a real `AutoRequest()` was up showed both NULL/0 on the
owning window (blocked inside `AutoRequest()` at the time) AND the
requester's own separate window. The text is rendered directly at
open time with no structural field retaining it — `GETTEXT` has
nothing to query, the same shape as other confirmed structural-reading
limits (a `PLACETEXT_IN` button's baked-in label).

**Scope today:** `WINDOW=`/`NOWINDOW=`/`TEXT=`/`REQUESTER` conditions
are understood, and only `CLICK` composes with `EXPECT=` (`WINDOW=`/
`NOWINDOW=` only, not `TEXT=`/`REQUESTER`) — `TYPE`/`DRAG`/`MENUPICK`
callers, and anyone wanting a `TEXT=`/`REQUESTER` wait, use a separate
`WAITFOR` call instead.

## Driving MUI applications

MUI (Magic User Interface) is a third-party AmigaOS GUI toolkit whose
internals are opaque to `TREE`/`CLICK`/`TYPE`/`GETTEXT` — the same limit
a `window.class` window's `layout.gadget` children already have (see
[Locator Tiers and Limits](Locator-Tiers-and-Limits.md)), just for a
different reason. Every MUI application carries an automatic ARexx port
of its own, though, so `MUIREXX <app-base> [TIMEOUT=<n>] <command...>`
drives it through *that* instead — a different mechanism entirely, sent
straight to the target application, not resolved by AmiPilotServer
against any window/gadget model:

```rexx
'MUIREXX MUIDEMO info title'
SAY 'Target is: 'RESULT
'MUIREXX MUIDEMO quit'
```

`<app-base>` is the target application's `MUIA_Application_Base` (its
own chosen ARexx port name, e.g. `"MUIDEMO"` for MUI's own demo app) —
`MUIREXX` tries it verbatim first, then `<app-base>.1`, since both are
real conventions in use. `RC=5` if neither is found.

**Be honest about what this reaches.** MUI's own built-in ARexx support
is a small, universal set — `quit`, `hide`, `show`, `activate`,
`deactivate`, `info <item>` (fixed metadata: title/author/version/base/
screen/copyright/description), and `help [file]` (lists every command
the target understands, confirmed live as the way to find out what's
really there rather than guessing) — confirmed against AmigaOS 3.2's
own `MUI:Demos/MUI-Demo`. There is **no generic "read or set this
widget's value" command** the way a numeric `GA_ID` or a tier-2
`ROLE=`/`LABEL=`/`INDEX=` locator reaches a classic gadget — MUI-Demo
itself registers zero commands beyond that universal set. Anything
richer is entirely up to the target application having added its own
commands; `MUIREXX` passes whatever you send through unchanged rather
than inventing capability a given app doesn't have. `quit` is the one
command every MUI app answers, and is genuinely useful on its own —
composing a script's teardown the same "exit via the app's own
affordances" way this project's other verbs already do.

The target's own reply code (an arbitrary, app-defined value this
bridge relays rather than reinterprets) surfaces as `RC=10` with the
code prefixed onto `RESULT` when nonzero; `RC=15` if the target never
replied within `TIMEOUT` (default 10 seconds); `RC=20` only if
`AmiPilotServer` itself couldn't allocate the ARexx message (its own
resource problem, not the target's).

## Driving layout.gadget-only applications

A `window.class` window attaches only its single top-level layout
object to `window->FirstGadget` — its own button/string/checkbox
children aren't individually walkable, and classic OS 3.x has no
public API to enumerate them (see [Locator Tiers and
Limits](Locator-Tiers-and-Limits.md)). A plain `GADGET` manifest entry
can't name such a gadget; a `WHEREGADGET` entry can, if the
application itself cooperates.

An application implementing this (issue #49) exposes a small,
optional ARexx port answering `WHERE <name>` with that gadget's own
live geometry — it already holds the object pointer for its own event
dispatch, so it just reads `GetAttr(GA_Left/GA_Top/GA_Width/
GA_Height)` and reports back. Its manifest declares the port's name
with `WHEREPORT`:

```
MANIFEST 2
APP MyApp
WHEREPORT MYAPP.WHERE
WINDOW main "My App"
WHEREGADGET connect_button main
```

Once that manifest is loaded, `CLICK @connect_button`/
`TYPE @connect_button ...` work exactly like they would for a plain
`GADGET` name — AmiPilot queries the port for the gadget's current
geometry, then clicks it with a genuine `input.device` event. **Only
discovery is cooperative; the click is real input**, unlike `MUIREXX`
above, where the target's own port does the acting too. No coordinate
ever appears in the script — it's resolved live, at action time, so
relayout and font changes can't break anything.

`WHERE @<name> [TIMEOUT=<n>]` is the standalone diagnostic form —
useful for confirming a third-party port answers correctly, or for
asserting on geometry directly:

```rexx
'WHERE @connect_button'
SAY 'RESULT is "x y w h": 'RESULT
```

`GETTEXT`/`DRAG` have no path through a `WHEREPORT` — a `WHEREGADGET`
name given to either is `RC=10` with an explicit "geometry only"
message, an honest stated limit rather than a silent fallback.
`RC=5` if the declared `WHEREPORT` doesn't exist (checked by its
*exact* declared name — no `.1`-slot fallback the way `MUIREXX`'s
MUI-specific convention gets); `RC=10` if the port itself reports the
name unknown, or its reply doesn't parse as exactly four integers;
`RC=15` if it never replies within `TIMEOUT` (default 10 seconds).
Full contract for application authors: [`manifest/SPEC.md`'s "The
cooperative geometry port"
section](https://github.com/sidick/amipilot/blob/main/manifest/SPEC.md).

## Example

```rexx
/* rexx */
OPTIONS RESULTS
ADDRESS 'AMIPILOT.1'

'CLICK GadTools 2'
'TYPE GadTools 2 hello amipilot'
'GETTEXT GadTools 2'
SAY 'Host field now reads: 'RESULT

'CLICK GadTools 1'
'QUIT'
```

Run it with `rx myscript.rexx` (`rx` is the standard AmigaOS ARexx
launcher; `AmiPilotServer` needs a real resident `RexxMast` to talk to,
same as any other ARexx-scriptable application). `OPTIONS RESULTS` is
required — without it ARexx never asks the host for a `RESULT` string at
all.

## Return codes

Same convention this project's sibling tools use for their own ARexx
ports:

| `RC` | Meaning |
|------|---------|
| `0` | Success. |
| `5` | Warning — the window or gadget wasn't found. The command was well-formed; there was just nothing to act on. |
| `10` | Error — unknown command, or a required argument was missing. |
| `15` | Timeout — `WAITFOR`/`CLICK ... EXPECT=`'s own awaited condition (or `FSPUT`'s payload) never arrived within `TIMEOUT`. Distinct from `5` (nothing matched at all) and `20` (the action itself failed) — the command was accepted and, for `CLICK`, the action DID happen; the expected effect just didn't show up in time. |
| `20` | Failure — the action itself didn't deliver (the underlying `input.device` event injection failed). |

## What it needs open

`AmiPilotServer` always opens `intuition.library` (V37+; fails outright
without it), `input.device`, and `rexxsyslib.library` (fails outright
without it — there's no point running with no ARexx port).
`gadtools.library` and `keymap.library` are opened opportunistically:
without the former, the same button/checkbox discrimination gap
`AmiInspect` has applies; without the latter, `TYPE` fails outright
(there's no way to synthesize keypresses without it) but every other
command still works.

## Next steps

See [Locator Tiers and Limits](Locator-Tiers-and-Limits.md) for what a
`window-pattern`/`gadget-id` pair can and can't reach today (the same
structural limits `AmiInspect` documents apply here too — a gadget has
to be visible to the walker before `AmiPilotServer` can click or type
into it).
