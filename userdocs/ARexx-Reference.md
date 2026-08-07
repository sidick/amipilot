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
| `CLICK` | `<window-pattern> <gadget-id>` | Clicks the gadget with that `GA_ID` — a genuine `input.device` click, not a shortcut. |
| `TYPE` | `<window-pattern> <gadget-id> <text...>` | Clicks the gadget (to focus it), then types `text` into it via real `IECLASS_RAWKEY` events, human-paced. Everything after the gadget ID is taken verbatim as the text — no quoting needed unless the text itself starts with `"`. |
| `GETTEXT` | `<window-pattern> <gadget-id>` | Returns that gadget's current text: a string/integer gadget's live value if it has one, otherwise its label. |
| `MANIFEST` | `<file-path>` | Loads an application's manifest (see below). Replaces any previously loaded one. `RESULT` reports what loaded (`loaded GTApp: 1 windows, 3 gadgets`); a rejected manifest returns `RC=10` with the reason (including line number) in `RESULT`. |
| `VERSION` | (none) | Returns the server version, wire-protocol number, and the stable/experimental verb lists (multi-line, same payload the [wire handshake](Wire-Protocol.md) uses) — for feature-testing from a script. |
| `LAUNCH` | `[STACK=<n>] <command-line...>` | Starts `command-line` as an AmigaDOS process (asynchronous — the commodity keeps servicing the port while it runs). `STACK` sets the new process's stack in bytes (default 4000, AmigaDOS's own `CreateNewProc()` default — most Intuition/ReAction GUI apps need more). `RC=0` means the process itself could be created, **not** that the command was found or ran successfully; assert on the expected effect (a window appearing) instead. See [Wire Protocol](Wire-Protocol.md#launch) for the full contract and its honest limits. |
| `FSLIST` | `<path>` | Lists a directory's entries (name, file/dir, size, protection, date, comment). Only works inside a root granted at startup — see [File API](Wire-Protocol.md#file-api). |
| `FSSTAT` | `<path>` | The metadata for a single file or directory, without listing its contents. |
| `FSMKDIR` | `<path>` | Creates a directory. Its parent must already exist and be inside a granted root. |
| `FSDELETE` | `<path>` | Deletes a file or empty directory. |
| `FSGET` | `<path>` | Returns a file's full contents (`RESULT` is the raw bytes, capped at the server's own small internal buffer — a test-staging channel, not a file manager). There is no `FSPUT`: writing files host-to-Amiga needs a wire feature that doesn't exist yet. |
| `SCREENS` | (none) | Lists every open screen: title, position, size, and whether it's frontmost. Title is each screen's own name (`DefaultTitle`), not the live title-bar text a window's `WA_ScreenTitle` can override. |
| `MENU` | `<window-pattern>` | Returns the matched window's full menu strip — every pulldown menu, its items, and (one level deep) their submenu items, with checkit/checked/enabled state and any keyboard shortcut, in the same text shape `AmiInspect` prints. |
| `MENUPICK` | `<window-pattern> <menu-num> <item-num> [<sub-num>]` | Selects a menu item via its keyboard shortcut (Right-Amiga + the shortcut character) — the numbers are the same 0-based chain positions `MENU`'s own output reports. `RC=20` if the item is disabled or has no keyboard shortcut (pointer-based selection for shortcut-less items isn't built yet). See [Wire Protocol](Wire-Protocol.md#menus) for the full contract. |
| `AUTH` | `<password>` | Authenticates a TCP connection (no effect on ARexx or serial.device, which have their own implicit trust boundaries). Until it succeeds, the TCP transport refuses every command except `VERSION`/`AUTH`/`QUIT` with `RC=10`. See [Securing TCP](Wire-Protocol.md#securing-tcp). |
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
