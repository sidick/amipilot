# Troubleshooting and FAQ

## `AmiInspect` fails immediately with "requires intuition.library V37"

`intuition.library` V37 (AmigaOS 2.04) is `AmiInspect`'s hard floor — see
[Installation](Installation.md). There's no way around this short of an
OS upgrade; nothing about `AmiInspect` itself can lower it, since the
BOOPSI/commodities-era Intuition it walks simply doesn't exist before
V37.

## A gadget's `label` is blank when I know the application set one

Two specific, documented cases genuinely have no readable label at this
tier — see [Locator Tiers and Limits](Locator-Tiers-and-Limits.md):

- A GadTools `BUTTON_KIND` gadget using `PLACETEXT_IN` (the common case)
  bakes its text into rendered imagery instead of the field `AmiInspect`
  reads.
- A `window.class`/`layout.gadget` window's individual gadgets aren't
  reachable at all yet — only the top-level layout object is, so you
  won't see their labels (or anything else about them) in the tree.

If neither of those applies and you're still seeing an unexpected blank
label, that's worth filing as an issue.

## A checkbox reports as `role=button`

This means `gadtools.library` wasn't open when `AmiInspect` ran (it opens
it itself, opportunistically, so this would mean the library is
genuinely unavailable on your system) — without it, `AmiInspect` can't
tell a GadTools `BUTTON_KIND` from a `CHECKBOX_KIND`, since they're
structurally identical. See
[Locator Tiers and Limits](Locator-Tiers-and-Limits.md).

## `AmiInspect WINDOW=...` finds nothing, but the window is clearly open

- `WINDOW=` matches a **substring** of the title, case-sensitively. Check
  capitalization and spelling.
- It searches every window on every public screen — but not windows on a
  private/custom screen your Shell session can't see, and not requesters
  (which aren't windows).
- A window with no title at all (`(untitled)` in `AmiInspect`'s own
  output when it's the active window) can never match a `WINDOW=`
  substring search — omit `WINDOW=` to fall back to the active window
  instead.

## `Amipilot.connect()`/`connect_with_retry()` raises `WireError` or times out

- Check `AmiPilotServer` is actually running on the Amiga side and
  listening on the transport you're connecting to (`Run AmiPilotServer`
  for serial, `Run AmiPilotServer TCP` — see
  [Wire Protocol](Wire-Protocol.md#starting-it) for the exact startup
  arguments for each).
- Over TCP, `connect_with_retry()` (unlike plain `connect()`) will keep
  retrying until `deadline_seconds` — the right call in test setup code
  when the server might not have finished booting yet, since a bare
  `connect()` fails immediately on the first refused connection.
- `ProtocolMismatch` (a `WireError` subclass) means the server's
  `PROTOCOL` number doesn't match what this host package expects —
  usually an old `AmiPilotServer` binary talking to a newer host
  package or vice versa. Rebuild/reinstall one side to match.
- If `TCPPASSWORD` was set on the server, connecting without the
  matching `password=` argument leaves the connection stuck rejecting
  every command except `VERSION`/`AUTH`/`QUIT` with `RC 10` — see
  [Securing TCP](Wire-Protocol.md#securing-tcp).

## A host client call raises `NotFound`, `CommandError`, `Timeout`, or `ActionFailed` — what's the difference?

These map directly to the wire's own RC codes
(`host/amipilot/client.py`):

- **`NotFound`** (`RC 5`) — the command was well-formed, but nothing
  matched (no window/gadget/screen fit the pattern you gave). Check
  spelling and substring matching, same as `AmiInspect WINDOW=`'s own
  rules above.
- **`CommandError`** (`RC 10`) — bad syntax, an unknown verb, or a
  locator the parser couldn't make sense of. Usually a typo or a verb
  this server build doesn't have yet (check `client.handshake()` /
  the `VERSION` command's stable/experimental verb lists).
- **`Timeout`** (`RC 15`) — a `WAITFOR`/`click(expect=...)` condition
  never became true within its timeout. For `click(expect=...)`
  specifically, **the click itself already happened** — this means
  only the expected follow-on effect (a window appearing/closing)
  didn't show up in time, not that the click failed to deliver. Raise
  `timeout=`, or check the effect you're waiting for is really what
  that action triggers.
- **`ActionFailed`** (`RC 20`) — the action itself didn't deliver:
  input injection failed, or (for `window_move()`/`window_resize()`)
  the target window never had a drag bar/sizing gadget at all.

## `SCREENSHOT`/`client.screenshot()` fails, or a P96 capture looks wrong

- `CommandError` from a capture usually means the target screen has no
  bitmap at all, or (for a Picasso96/RTG screen) a pixel format this
  project doesn't support — currently the SDK's own hardware-only YUV
  formats. See [SCREENSHOT](Wire-Protocol.md#screenshot).
- A capture that would exceed the server's own size cap
  (`AMIP_SCREENSHOT_MAX_BYTES`, 512 KB) is rejected outright, not
  silently truncated — expect this on a large/high-colour P96 desktop.
- The genuine P96-active capture path (an RTG board actually being
  detected and read) is honestly unverified against real hardware in
  this project's own testing so far — Copperline has no RTG emulation
  to test against. If a real P96 capture looks wrong, that's a real
  gap worth filing as an issue, not assumed to already be solid.
- Serial transfers are slow for anything beyond a small/low-colour
  capture — see the transfer-time table in
  [SCREENSHOT](Wire-Protocol.md#screenshot). Prefer TCP if `SCREENSHOT`
  seems to hang.

## `WBLAUNCH`'s `TOOLTYPE=`/`ARG=` don't seem to take effect

- `WBLAUNCH`'s `RC 0` only means the process was created and the
  `WBStartup` message queued — **not** that the launched program
  finished starting or actually read the tooltype/argument. Assert on
  the program's own observable effect (a window appearing with the
  expected state) instead of trusting the return code alone. See
  [WBLAUNCH](Wire-Protocol.md#wblaunch).
- Tooltype overrides are merged into a scratch copy of the icon
  written to `T:`, never the application's own real `.info` — if the
  program re-reads its *own* icon path directly rather than trusting
  the `WBArg` it was launched with, it'll see the original tooltypes,
  not the override.

## `PICK`/`client.pick()`/`AmiInspect PICK` finds the wrong gadget, or none at all

- Confirm the pointer is actually over the window you expect at the
  moment you call `PICK` — it's a single point-in-time snapshot of
  wherever the live pointer happens to be right then, not a live
  subscription. Call it repeatedly (a poll loop, or `AmiInspect PICK`'s
  own built-in loop) rather than once.
- An empty `gadgets` list isn't a failure — it means the pointer is
  over that window's own chrome/background, which can genuinely
  include a real system gadget (`gadget_id == 0`,
  `class_name == "gadgetclass"`, for the drag bar/close/depth/size
  decorations) rather than "nothing at all". See
  [PICK](Wire-Protocol.md#pick).
- The live pointer position the server reads back internally needs a
  correction for a real, confirmed quirk (roughly 2x the real pixel
  Y) — handled transparently, verified on this project's own default
  Workbench screen configuration. Whether that correction holds
  unchanged on every other display mode (superhires, NTSC,
  productivity/RTG) is honestly **not verified** — if `PICK` seems
  systematically off vertically on an unusual screen mode, that's a
  real gap worth filing as an issue (`server/README.md`'s own PICK
  section has the full story), not assumed already solid.

## `MUIREXX` says a command isn't recognized, even though I copied it from MUI documentation

MUI's own built-in ARexx support is a small, fixed set of seven
commands (`quit`/`hide`/`show`/`activate`/`deactivate`/`info`/`help`)
— it's not a generic "read or set any gadget's value" mechanism.
`MUIREXX` is an honest passthrough to whatever the target application's
own ARexx port actually implements; if the app adds its own custom
commands beyond those seven, `MUIREXX` can send them too, but AmiPilot
has no way to know what they are ahead of time. See
[Driving MUI applications](ARexx-Reference.md#driving-mui-applications).

## Where do I report a bug or ask a question?

[github.com/sidick/amipilot/issues](https://github.com/sidick/amipilot/issues).
