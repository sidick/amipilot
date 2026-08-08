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
| `FSLIST` | `<path>` | Lists a directory's entries (name, file/dir, size, protection, date, comment). Only works inside a root granted at startup — see [File API](Wire-Protocol.md#file-api). |
| `FSSTAT` | `<path>` | The metadata for a single file or directory, without listing its contents. |
| `FSMKDIR` | `<path>` | Creates a directory. Its parent must already exist and be inside a granted root. |
| `FSDELETE` | `<path>` | Deletes a file or empty directory. |
| `FSGET` | `<path>` | Returns a file's full contents (`RESULT` is the raw bytes, capped at the server's own small internal buffer — a test-staging channel, not a file manager). `FSPUT` (the opposite direction) is **not** listed here: it's wire-only, not answerable over ARexx at all — see [File API](Wire-Protocol.md#file-api). |
| `SCREENS` | (none) | Lists every open screen: title, position, size, and whether it's frontmost. Title is each screen's own name (`DefaultTitle`), not the live title-bar text a window's `WA_ScreenTitle` can override. |
| `MENU` | `<window-pattern>` | Returns the matched window's full menu strip — every pulldown menu, its items, and (one level deep) their submenu items, with checkit/checked/enabled state and any keyboard shortcut, in the same text shape `AmiInspect` prints. |
| `MENUPICK` | `<window-pattern> <menu-num> <item-num> [<sub-num>]` | Selects a menu item via its keyboard shortcut (Right-Amiga + the shortcut character) — the numbers are the same 0-based chain positions `MENU`'s own output reports. `RC=20` if the item is disabled or has no keyboard shortcut (pointer-based selection for shortcut-less items isn't built yet). See [Wire Protocol](Wire-Protocol.md#menus) for the full contract. |
| `DRAG` | `<window-pattern> <locator> <dx> <dy>` or `<window-pattern> <locator> TO (<dest-gadget-id> \| @<dest-name>)` | A genuine press/move/release drag. The offset form (`<dx> <dy>`) moves the gadget's current center by a pixel delta — the natural shape for a slider/scroller. The `TO` form drags onto a second gadget's center instead, both resolved live, for drag-and-drop/reorder — the destination must be in the same window as the source. `<locator>` is the same numeric `GA_ID`, `ROLE=`/`LABEL=`/`INDEX=`, or `@name` form CLICK/TYPE/GETTEXT accept. |
| `WAITFOR` | `[SCREEN=<s>] WINDOW=<pattern> [TIMEOUT=<n>]` or `[SCREEN=<s>] NOWINDOW=<pattern> [TIMEOUT=<n>]` or `[SCREEN=<s>] <window-pattern> (<gadget-id> \| ROLE=<r> [LABEL=<l>] [INDEX=<n>]) TEXT=<value> [TIMEOUT=<n>]` or `@<name> TEXT=<value> [TIMEOUT=<n>]` | Polls (server-side, one round trip) until a window matching `<pattern>` appears, until none does, or until a gadget's text exactly equals `<value>`. `TIMEOUT` defaults to 10 seconds. `RC=15` if the condition never becomes true in time — a new RC distinct from "nothing matched" (`RC=5`) or "the action itself failed" (`RC=20`). See [Wait/expectation primitives](#waitexpectation-primitives). |
| `AUTH` | `<password>` | Authenticates a TCP connection (no effect on ARexx or serial.device, which have their own implicit trust boundaries). Until it succeeds, the TCP transport refuses every command except `VERSION`/`AUTH`/`QUIT` with `RC=10`. See [Securing TCP](Wire-Protocol.md#securing-tcp). |
| `MUIREXX` | `<app-base> [TIMEOUT=<n>] <command...>` | Sends `<command>` verbatim to a MUI application's own ARexx port. The MUI-ARexx bridge tier — see [Driving MUI applications](#driving-mui-applications). |
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

**Scope today:** `WINDOW=`/`NOWINDOW=`/`TEXT=` conditions are
understood, and only `CLICK` composes with `EXPECT=` (`WINDOW=`/
`NOWINDOW=` only, not `TEXT=`) — `TYPE`/`DRAG`/`MENUPICK` callers, and
anyone wanting a `TEXT=` wait, use a separate `WAITFOR` call instead.

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
