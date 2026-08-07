# server

The AmiPilot server commodity: hosts the `intuition-model` walker, the
action engine (input.device event synthesis), and the transports (ARexx
first, then serial.device, then TCP).

Lands in phase 0.2 onward -- see
[`docs/implementation-plan.md`](../docs/implementation-plan.md).

## Current state (phase 0.4, in progress)

- **The wire (phase 0.3, shipped in v0.3):** the same verb grammar over
  serial.device, framed per [`WIRE.md`](WIRE.md) (this directory) --
  LF-terminated request lines in, `RC <code> <byte-count>` + payload
  out, `VERSION` as the handshake. `src/serial.c` is the transport
  (standing async 1-byte read + SDCMD_QUERY chunk drain, xon/xoff
  disabled); one parser (`arexx_cmd.c`) and one dispatch
  (`HandleCommand` in `amipilotserver/main.c`) serve every transport,
  so ARexx RESULT strings and wire payloads are the same bytes. Enable
  with `AmiPilotServer SERIAL` (`SERDEVICE`/`SERUNIT`/`BAUD` to taste).
  Verified end-to-end by `make test-target`'s wire check: the real host
  client (`host/amipilot/wire.py`) connects through Copperline's
  `--serial tcp` bridge, handshakes, TYPEs into the fixture's string
  gadget, reads the value back, clicks Connect, and confirms the
  window died -- the phase 0.3 loop minus pytest, host-driven.
- **TCP (phase 0.4, in progress):** the same wire over
  bsdsocket.library, listen-mode -- `src/tcp.c`, enabled with
  `AmiPilotServer TCP TCPPORT=n` (`SERIAL` and `TCP` are independent;
  either or both may be given at once). Readiness is driven by
  `WaitSelect()`, not the more obvious `SBTC_SIGEVENTMASK` +
  `SO_EVENTMASK` + `GetSocketEvents()` async-notification mechanism the
  headers also document -- that path reported success at every step
  (`SocketBaseTags()` returned 0, a plausible signal bit came back) but
  never actually delivered a signal when tested live against a real
  Copperline hostsocket bridge; `WaitSelect()` (the mechanism
  bsdsocktest's own conformance suite exercises directly) does. See
  `tcp.c`'s own header comment for the full story, including a second
  confirmed-live gotcha: a command sent before the connection is truly
  ready gets silently dropped, not buffered, so the client needs to
  resend on a *held* connection rather than reconnect (`host/amipilot/
  client.py`'s `Amipilot.connect_with_retry()` does this on the host
  side).

  **Verified end-to-end on two independent bsdsocket.library
  implementations**, not just one: a full `VERSION`/`TREE`/`TYPE`/
  `GETTEXT`/`CLICK`/`TREE`/`QUIT` round trip against Amiberry (whose
  bsdsocket.library forwards straight to the host OS's real socket
  API -- `amipilot.uae`'s `bsdsocket_emu=true`, no virtual networking
  involved at all, just `Run AmiPilotServer TCP TCPPORT=n` and connect
  to that port on the host directly), and the same round trip earlier
  against a real Copperline hostsocket bridge (bridged to a host
  `feth` pair). The two implementations are architecturally unrelated
  (Copperline's is a from-scratch smoltcp-based TCP/IP stack;
  Amiberry's is a thin syscall-forwarding shim), so agreement between
  them is real evidence the `WaitSelect()` design is correct, not an
  artifact of one emulator's specific behaviour. Not yet wired into
  `make test-target` as an automated check -- Amiberry isn't scripted
  headlessly the way Copperline is (see "On-target testing" in the
  top-level `CLAUDE.md`), and a Copperline-hostsocket version would
  need the same `feth` bridge setup `tests/copperline/README.md`
  documents, machine-specific like `copperline.local.toml`. Only
  listen-mode exists today (the server binds, the host connects in);
  a guest-initiated dial-out mode -- which would sidestep needing any
  bridge/NAT-forwarding setup at all, since the guest only needs
  outbound connectivity -- is proposed future work, see
  [amipilot#12](https://github.com/sidick/amipilot/issues/12).

- **Securing TCP (phase 0.4, in progress):** `AmipTcpOpen()` binds
  `INADDR_ANY` and accepts any source by default -- fine for a trusted
  LAN, real exposure on anything else given this server can run
  arbitrary shell commands (`LAUNCH`), read/write files inside a
  granted `FSROOT`, and inject GUI input. Two independent, opt-in,
  combinable knobs raise the bar:

  - **`TCPALLOW=<ip-or-cidr>[,<ip-or-cidr>...]`** -- a single `/K`
    value, comma-separated for multiple entries, e.g.
    `TCPALLOW=192.168.1.0/24,10.0.0.5`. **Not `/M`**, unlike `FSROOT`
    -- `ReadArgs()`'s template grammar allows at most one `/M` keyword
    per template, and `FSROOT` already claims that slot; a second one
    makes `ReadArgs()` fail with `ERROR_BAD_TEMPLATE` on *every*
    invocation of `AmiPilotServer`, not just when `TCPALLOW` is used
    -- caught live via Amiberry verification, not assumed. Addresses
    are parsed by a small hand-rolled dotted-quad parser
    (`ParseDottedQuad()`, `tcp.c`), deliberately **not** `inet_aton()`:
    that call is a Roadshow-era bsdsocket extension at a high LVO
    offset, absent from every real bsdsocket.library implementation,
    and calling it under Amiberry's `bsdsocket_emu` produced a genuine
    CPU trap (illegal instruction) rather than a clean failure --
    reproduced in isolation with a 6-line standalone test program, so
    this wasn't specific to this server's own code. `inet_addr()` (the
    classic, portable alternative) was considered and rejected too:
    its failure return (`INADDR_NONE`, `0xFFFFFFFF`) is indistinguishable
    from the valid address `255.255.255.255`. Parsing four decimal
    octets by hand needed no library call at all, sidestepping both
    problems. With no `TCPALLOW` granted, every source is accepted,
    unchanged from this transport's original behavior. A rejected
    peer is closed immediately -- before ever becoming the active
    client, no reply ever sent -- so a disallowed connection can't be
    used to fingerprint this service.
  - **`TCPPASSWORD=<value>`** gates the new `AUTH <password>` verb
    (`server/WIRE.md`) -- TCP only, not ARexx or serial.device, which
    keep their existing implicit trust boundaries (local machine,
    physical cable). Defaults to `"amipilot"` when omitted (see
    `AMIP_TCP_DEFAULT_PASSWORD`) -- the bundled host client
    (`Amipilot.connect()`/`connect_with_retry()`) sends this
    automatically, so TCP keeps working out of the box with zero
    config changes on either side. Neither `TCPALLOW` nor
    `TCPPASSWORD` is mandatory; `TCP TCPPORT=n` alone behaves exactly
    as before this work.

  **This is explicitly NOT real security, and neither knob makes TCP
  safe to expose on an open/internet-facing port -- LAN or a direct
  machine-to-machine link only.** The default password is public (it's
  in this repo); there is no TLS, so even a custom password crosses
  the wire in cleartext; and there is no rate-limiting or lockout on
  repeated `AUTH` guesses. `AmiPilotServer` prints a warning to this
  effect every time `TCP` is enabled, not just here. `TCPALLOW`/
  `TCPPASSWORD` raise the bar above "wide open to anyone," nothing
  more -- treat them as you would a router's default admin password.

  **Verification, manual (same honest-limits precedent TCP's own
  original verification already established just above -- no
  automated `make test-target` check exists for the real TCP
  transport at all, only for serial.device carried over Copperline's
  `--serial tcp` bridge):** confirmed live via Amiberry
  (`bsdsocket_emu=true`), driving the real wire protocol from a host
  Python socket against `TCP TCPPORT=n TCPALLOW=... TCPPASSWORD=...`.
  This verification pass is *why* the implementation looks the way it
  does above, not an afterthought run once the code compiled -- it
  caught three real bugs before merge, none of which showed up any
  other way:

  1. **`ReadArgs()` failed on every single invocation, TCP or not.**
     The first `TCPALLOW/K/M` draft gave the template two `/M`
     keywords (`FSROOT` already had the one AmigaDOS allows per
     template) -- confirmed live as `ERROR_BAD_TEMPLATE` on startup,
     not caught by the cross-compiler (a syntax-valid string literal,
     wrong at the AmigaDOS level). Fixed by making `TCPALLOW` a plain
     `/K` value with comma-separated entries instead (see above).
  2. **`inet_aton()` trapped the CPU outright.** It's a Roadshow-era
     bsdsocket extension at a high LVO offset, absent from other real
     implementations; calling it under Amiberry's `bsdsocket_emu`
     produced a genuine illegal-instruction exception, not a clean
     "unsupported" failure -- reproduced in an isolated 6-line test
     program to confirm it wasn't specific to this server's own code.
     Fixed by parsing dotted-quad addresses by hand instead
     (`ParseDottedQuad()`, `tcp.c`) -- no library call, so no
     availability risk.
  3. **Rejection/auth-failure reasons never reached the wire.** The
     new early-return paths in `HandleCommand()` set the payload text
     but not its length, so the TCP dispatch loop sent `RC 10` with a
     silently empty payload -- confirmed by driving the actual bytes
     over a socket and seeing `len=0` where a message was expected,
     something a design read-through wouldn't have caught. Fixed by
     setting `*resultLenOut` on those paths, same as the fall-through
     tail already does for every other verb.

  With those three fixed, confirmed working end to end: (a) an
  unauthenticated connection has every non-`VERSION`/`AUTH`/`QUIT`
  command rejected, *with* the correct reason text now arriving; (b)
  `AUTH amipilot` (the default) succeeds and unlocks normal operation;
  (c) a wrong password is rejected (with the correct reason text) and
  doesn't unlock anything; (d) `TCPALLOW` set to a range excluding the
  test machine causes the connection to be reset with no reply, while
  a range including it connects and authenticates normally. A fourth,
  smaller gap surfaced during (d) and was also fixed: the rejection
  log line had no `fflush()`, so it sat in stdio's buffer instead of
  reaching a redirected log file until the process eventually exited
  -- confirmed by the client-side reset/no-reply happening correctly
  while the log stayed empty until `AmiPilotServer` was killed.

- **Program launch (phase 0.4, in progress):** `LAUNCH [STACK=n]
  <command-line...>` starts an AmigaDOS process from a connected
  session -- so a test can connect first, confirm nothing is running
  yet, and start its own subject over the wire instead of pre-staging
  it via `S:User-Startup`. Implemented with `SystemTagList()`
  (`SYS_Asynch` so the launch doesn't block AmiPilotServer's own
  dispatch loop -- a persistent GUI app like a fixture never returns),
  with explicit `NIL:` input/output handles rather than defaulted ones:
  `SystemTagList()`'s own docs say an async launch closes the caller's
  `Input()`/`Output()` on completion "even if these were your Input()
  and Output()!", so leaving them unset would eventually close
  AmiPilotServer's own stdio. `STACK` sets `NP_StackSize` (bytes;
  AmigaDOS's own `CreateNewProc()` default is 4000 if omitted) -- most
  Intuition/ReAction GUI apps need more than that default. **Honest
  limit, not silently glossed over:** an async launch's own RC only
  reflects whether the shell process itself could be created (out of
  memory, no process slot) -- RC 0 does NOT mean the command was found
  or ran successfully, since the shell resolves the command name after
  `SystemTagList()` has already returned. There's no output capture
  yet either. Assert on the expected effect instead (e.g. polling
  `TREE` for the launched app's window), same as
  `tests/copperline/launch-test.py` does. Real command-found
  verification and exit-code retrieval need a `proc-wait` verb -- part
  of this phase's plan but not built here, not invented ahead of it.
  Verified end-to-end by `make test-target`'s LAUNCH check: connect to
  a bare server (no fixture pre-staged), confirm its window doesn't
  exist, `LAUNCH ... STACK=8192`, poll until the window appears, then
  a full `TYPE`/`GETTEXT`/`CLICK` round trip proves the launched
  process is genuinely functional with the non-default stack, not just
  that it didn't immediately crash.

- **File API (phase 0.4, in progress):** `FSLIST`/`FSSTAT`/`FSMKDIR`/
  `FSDELETE`/`FSGET`, each taking a single `<path>` argument (quoted
  the same way a window pattern is if it contains a space) --
  `src/fs.c`. Disabled entirely until the server is started with at
  least one `AmiPilotServer ... FSROOT=<path> [FSROOT=<path> ...]`
  grant (`FSROOT/K/M`, so multiple roots may be given); every verb
  refuses a path outside every granted root with `RC 10` naming the
  granted list, and there is no way to grant a root after startup.
  Containment is checked by **lock identity**
  (`Lock()`/`ParentDir()`/`SameLock()`, all V36), never by string
  prefix matching -- Amiga assigns mean two different path strings can
  name the same or a nested location, so string comparison would miss
  genuine matches and genuine escapes alike. **Known limitation
  (TOCTOU, accepted):** `FSDELETE`, `FSGET`, and `FSMKDIR` verify
  containment via a lock, then release it and re-resolve the same path
  *string* for the actual `DeleteFile()`/`Open()`/`CreateDir()` --
  AmigaDOS (this project's V37 floor) has no relative-to-lock form of
  any of those calls. An assign or path component repointed in the gap
  between the check and the real operation could steer the operation
  outside the granted root; this narrows nothing further than the
  check itself and is a real, standing gap, not silently assumed closed
  (see the doc comment on `ResolveExisting()` in `src/fs.c`, matching
  `AmipIsWindowOpen()`'s own "shrinks the gap, doesn't close it
  entirely" precedent). `FSGET` returns the whole
  file as raw bytes (may contain embedded NULs; the wire's
  length-prefixed framing carries them intact) and is capped at the
  server's own internal buffer (`AMIP_FS_BUF_SIZE`, 16KB) -- this is a
  test-staging channel for small fixtures/config/log files, not a file
  manager, and `RC 20` on anything larger. **`FSPUT` (host-to-Amiga
  writes) is deliberately not built here** -- the wire's request
  grammar is strictly single LF-terminated text lines today, and a
  binary request body needs its own protocol addition; a real, separate
  follow-up, not silently dropped scope.
  Verified end-to-end by `make test-target`'s file API check
  (`tests/copperline/fs-test.py`): seeds a granted `RAM:` directory
  with a file via `S:User-Startup` before the server starts (`FSROOT`
  is a startup-time `Lock()`, so the root must already exist), then
  over the wire lists it, stats and reads the seeded file back byte-
  for-byte, creates and deletes a subdirectory, and confirms `FSLIST
  SYS:` -- outside the granted root -- is rejected rather than served.

- **Menus (phase 0.4, in progress):** `MENU <window-pattern>` walks a
  window's live `struct Menu`/`struct MenuItem` chain
  (`intuition-model`'s `AmipWalkMenuStrip()`, `intuition-model/src/
  walk.c`) and returns every pulldown menu, its items, and (one level
  deep -- classic Intuition menus don't nest a second pull-right
  level) their submenu items, each with its checkit/checked/enabled
  state and keyboard shortcut if it has one. `MENUPICK
  <window-pattern> <menu-num> <item-num> [<sub-num>]` addresses an
  item by the same 0-based chain positions `MENU`'s own output
  reports (and what Intuition itself decodes an `IDCMP_MENUPICK`
  `Code` into via `MENUNUM()`/`ITEMNUM()`/`SUBNUM()`).

  **Selection is keyboard-shortcut only for now.** `AmipMenuPickByShortcut()`
  (`server/src/action.c`) activates the window, then strikes the
  item's `Command` byte (inverted through the live keymap via
  `MapANSI()`, same technique `AmipTypeString()` uses per character)
  with the right-Amiga qualifier held -- the same input.device path a
  human pressing Right-Amiga+key produces. Intuition resolves that
  combination against the window's own live menu strip on its own;
  this deliberately does not synthesize `IDCMP_MENUPICK` directly, so
  a successful `RC 0` is genuine evidence the pick reached the app
  through the real menu-shortcut path, not a shortcut around it.
  **Honest limit:** an item with no keyboard shortcut (`COMMSEQ`
  unset) can't be picked yet -- `RC 20` names this explicitly
  ("pointer-based menu selection isn't built yet") rather than
  silently failing or guessing a fallback. Pointer-based navigation
  (open the menu, move across items/submenus, release over the target
  -- `LayoutMenusA()` already precomputes every item's screen-absolute
  geometry before the menu is ever opened, so this is buildable) is
  real follow-up work, not invented here ahead of it. A disabled item
  (`ITEMENABLED` unset) is rejected client-side, before any keystroke
  is sent at all -- `RC 20`, distinct from the no-shortcut case.

  Verified end-to-end by `make test-target`'s MENU/MENUPICK check
  (`tests/copperline/menu-test.py`) against a menu strip added to
  `fixtures/gadtools-app` for this purpose: walks the menu and asserts
  every field the walker read live (including a `CHECKIT|MENUTOGGLE`
  item's starting `checked` state and a separator bar's blank,
  disabled entry), `MENUPICK`s a top-level item and a submenu item by
  their shortcuts and confirms each pick genuinely reached the
  fixture's own `IDCMP_MENUPICK` handler (which writes a distinct
  marker into its Host string gadget, read back via the
  already-verified `GETTEXT` path -- proof of real delivery through
  Intuition, not just that a keystroke was injected), and confirms the
  fixture's permanently-disabled item is rejected without ever sending
  a keystroke.

- **Tier-2 semantic locators (phase 0.4, in progress):** `CLICK`/`TYPE`/
  `GETTEXT`'s classic form accepts a `ROLE=<role>`/`LABEL=<substring>`/
  `INDEX=<n>` locator in place of the bare numeric `<gadget-id>` --
  `docs/implementation-plan.md`'s "Locator tiers" section, tier 2
  ("gadget by role + label text, or by position-in-set"). `ROLE=`
  matches `intuition-model`'s `AmipRoleName()` vocabulary
  (`"button"`, `"string"`, `"slider"`, etc., case-insensitively --
  `AmipRoleFromName()` is the reverse lookup); `LABEL=` is a
  case-**sensitive** substring match (`strstr`), the same convention
  window/screen patterns already use, not a new inconsistent
  behavior; `INDEX=` (0-based, default the first match) disambiguates
  when more than one gadget matches. At least one of `ROLE=`/`LABEL=`
  must be given -- a bare digit is always the original numeric form,
  unchanged. Resolved server-side (`ResolveTargetGadget()`,
  `amipilotserver/main.c`) against a fresh walk of the live window
  (the same `AmipWalkWindow()` call `TREE`/`GETTEXT` already make, so
  no new classification logic), never a cached or stale model. No
  match is `RC 5`, same class an unmatched numeric ID already uses.
  **Honest limit:** proximity-to-a-label matching (the plan's third
  tier-2 locator style) isn't built -- `ROLE=`/`LABEL=`/`INDEX=` is
  the complete locator vocabulary today, not a partial step toward a
  fuzzier heuristic.

  `fixtures/gadtools-app` carries a second `BUTTON_KIND` gadget
  (Connect, Cancel) specifically so `INDEX=` has a real same-role
  pair to disambiguate, not just a single unambiguous instance
  `ROLE=`/`LABEL=` alone could already find.

- **Drag (phase 0.4, in progress):** `DRAG <window-pattern> <locator>
  <dx> <dy>` -- a genuine press/move/release drag of the target
  gadget's current center by a pixel offset, the natural shape for
  adjusting a slider/scroller (GadTools `SLIDER_KIND`/`PROP_KIND`,
  which is inherently a delta operation). `DRAG <window-pattern>
  <locator> TO <dest-gadget-id>` (or `TO @<dest-name>`) is the
  gadget-to-gadget form instead: drags the source onto a second
  gadget's center, both resolved live at action time, zero
  coordinates in the caller's script -- for drag-and-drop/reorder
  cases (e.g. dragging one listview item onto another). The
  destination is always resolved against the SAME window as the
  source; a `TO @<dest-name>` whose own manifest entry names a
  different window is rejected explicitly (`RC 10`), not silently
  dragged into the wrong window. The source locator is exactly
  CLICK/TYPE/GETTEXT's (numeric `GA_ID`, tier-2 `ROLE=`/`LABEL=`/
  `INDEX=`, or `@name`) -- DRAG gets tier-2 source locators for free
  from the same parser path. No `ROLE=`/`LABEL=` form for the
  destination, keeping this verb's scope contained.

  `AmipDragAt()` (`server/src/action.c`) is the raw primitive: a real
  `IECLASS_RAWMOUSE` button-down, an absolute `IEPointerPixel` jump to
  the destination, a real button-up -- built on the same
  `AmipMoveMouseTo()`/button-injection code `AmipClickAt()` already
  uses (`AmipGadgetCenter()`, refactored out of `AmipClickGadget()`,
  is the shared geometry-resolution step both now call). **Honest
  limit:** the move between press and release is a single absolute
  jump, not synthesized continuous motion -- sufficient for
  Intuition's own built-in prop-gadget/window-drag tracking (which
  watches ordinary mouse-move events regardless of how many arrive),
  but an application with its OWN per-frame-delta-sensitive drag
  handling could in principle behave differently than under a real
  human drag across every intervening pixel. Narrows to "the
  endpoints are always genuine input.device events reaching the real
  event path," not full motion-stream fidelity.

  Verified live against a real `AmiPilotServer` (Amiberry, not just
  build/lint): a `SLIDER_KIND` gadget added to `fixtures/gadtools-app`
  for this purpose (range 0..100, starts at 0) whose `IDCMP_GADGETUP`
  handler writes the live level GadTools itself reports (the event's
  own `Code` field, its documented contract for `SLIDER_KIND`/
  `PROP_KIND`) into the Host string gadget -- the same observable-
  marker technique the Cancel button/menu items already use, proving
  a `DRAG` genuinely reached GadTools' real slider-tracking code, not
  just that input.device events were injected.

- **Screens (phase 0.4, in progress):** `SCREENS` (no arguments) lists
  every open screen -- title, position, size, and whether it's
  frontmost. Every window-targeting verb (`TREE`/`CLICK`/`TYPE`/
  `GETTEXT`/`MENU`/`MENUPICK`'s classic form, not `@name`) additionally
  accepts an optional leading `SCREEN=<substring>` token before the
  window pattern -- same `KEYWORD=value` idiom `LAUNCH`'s own
  `STACK=<n>` already established -- narrowing the window search
  (`AmipFindWindow()`, `server/src/action.c`) to screens matching that
  substring; omitted, the search covers every screen exactly as before
  this existed. This is for disambiguating two same-titled windows on
  different screens, not for making cross-screen targeting possible in
  the first place -- `AmipFindWindow()` already searched every screen
  before this feature existed, it just couldn't tell two matches on
  different screens apart. `TREE`/`MENU`'s window header line also
  gains a `screen="<title>"` field for the same reason, so a script
  can confirm which screen a match actually landed on.

  **Identity is `Screen->DefaultTitle`, deliberately not the live
  `Title` field** -- verified against `intuition.doc`, not assumed:
  `SetWindowTitles()`'s own doc says the screen's title-bar text
  "appears ... whenever this window is the active one", i.e. `Title`
  tracks whichever window is currently active on that screen (via that
  window's own `WA_ScreenTitle`), not a fixed name for the screen
  itself, and so isn't a safe thing to match or report as identity.
  `DefaultTitle` is the app's own name for the screen, set once at
  open time -- the classic `NewScreen.DefaultTitle` field and the V36+
  `SA_Title` tag are the *same* field (the autodoc says outright "[For
  V36: superseded by SA_Title]"), so there's no behavioral difference
  between a screen opened the old way and a tag-opened one here.

  **Honest limit, not silently glossed over:** `SCREEN=` and the new
  `screen="..."` field only apply to the classic `<window-pattern>`
  form -- the manifest contract and `@name` locators have no
  screen-awareness yet, since manifests don't record which screen a
  window belongs to. There's also no standalone "bring this screen to
  front" verb: it's unnecessary, since `CLICK`/`TYPE`/`MENUPICK`
  already call `ScreenToFront()`/`WindowToFront()`/`ActivateWindow()`
  on the target window's own screen as a side effect of acting (this
  predates the `SCREENS`/`SCREEN=` work -- see `AmipClickGadget()` and
  `AmipMenuPickByShortcut()`), and read-only verbs (`TREE`/`GETTEXT`/
  `MENU`) deliberately don't move anything forward.

  Also new: `Amipilot.wait_for_window()`/`wait_for_screen()`
  (`host/amipilot/client.py`) -- host-side polling helpers for "did my
  window/screen show up yet" (an asynchronously `launch()`ed program
  opening its own screen is the normal case, not an edge case), same
  idea `LAUNCH`'s own docstring already recommended by hand
  (`tests/copperline/launch-test.py`'s poll loop), now reusable rather
  than re-written per caller. A server-side blocking wait verb was
  considered and rejected: `AmiPilotServer`'s dispatch is
  single-threaded and synchronous (see `main.c`'s own comment on why
  `g_resultBuf`/`g_treeBuf` are safe as file-statics), so a verb that
  blocks server-side for a timeout would stall the *entire* server --
  ARexx port and wire both -- for that whole duration.

  Verified end-to-end by `make test-target`'s multi-screen check
  (`tests/copperline/screens-test.py`), against a new fixture
  (`fixtures/second-screen-app`) that opens its own custom screen: with
  `gadtools-app`'s window pushed to a now-background Workbench screen
  by the second screen opening on top of it, `SCREENS` correctly
  reports both screens and which is frontmost (reached via
  `wait_for_screen()`, exercising that helper live rather than only
  against a fake clock), a loose pattern matching both fixtures'
  windows is disambiguated by `SCREEN=`, and -- the check that actually
  retires the risk this feature exists to address -- `TYPE`/`GETTEXT`/
  `CLICK` against the window on the background screen still succeed,
  proving `ScreenToFront()` genuinely brings a non-frontmost screen
  forward correctly, not just in the single-screen case every prior
  on-target check has ever exercised.

## Phase 0.2 (shipped)

- `src/action.c` + `include/action_engine.h` -- the action engine's
  first real verbs: absolute pointer positioning (documented
  `IECLASS_NEWPOINTERPOS`/`IESUBCLASS_PIXEL` mechanism) and genuine
  button click synthesis through `input.device`. Verified end-to-end
  under Copperline by `make test-target`'s action-engine click check:
  the synthetic click really presses `fixtures/gadtools-app`'s Connect
  button. Read the comments in `action.c` before changing event fields
  -- `IEQUALIFIER_RELATIVEMOUSE` and the gadget-coordinate convention
  were both hard-won.
- `src/clicktest/` (`AmiClickTest`) and `src/setmouse/` (`AmiSetMouse`)
  -- dev-only Shell diagnostics, not deliverables: end-to-end click
  proof and isolated pointer-positioning verification respectively.
  Built by `make server`, deliberately not part of `make build`.
- **`src/amipilotserver/` (`AmiPilotServer`) -- the actual phase 0.2
  deliverable.** A commodity hosting the action engine and
  `intuition-model` behind a genuine public ARexx port
  (`"AMIPILOT.<n>"`), plus `src/arexx.c`/`arexx_cmd.c` (the RexxMsg glue
  and portable command parser, split the same way `../amiauth`'s
  `arexx.c`/`arexx_cmd.c` are). Verb set: `TREE`/`CLICK`/`TYPE`/
  `GETTEXT`/`MANIFEST`/`QUIT` -- a small, real subset of the plan's
  full v1 verb list (wire protocol, launch, fs, menu/drag verbs are
  0.3/0.4 scope). `src/manifest.c` (portable, like `arexx_cmd.c`)
  parses the manifest contract (`../manifest/SPEC.md`) behind the
  `MANIFEST` verb and the `@<logical-name>` locator form.
  Verified against a real resident `RexxMast` (`rx`, not a hand-rolled
  RexxMsg -- `rexxsyslib.library`'s `IsRexxMsg()` only validates
  messages whose `rm_TaskBlock` came from a live ARexx task) by
  `make test-target`'s ARexx-port check: `tests/copperline/arexx-test.rexx`
  types into `fixtures/gadtools-app`'s Host string gadget, reads the
  value back over the port, then clicks Connect and confirms the window
  is gone -- state changed, driven and observed entirely through ARexx,
  no host involved. This is the phase 0.2 release gate.

  Kept under `server` rather than `amiga`/`build` for now because phase
  0.2 isn't tagged yet, not because it's a throwaway tool like
  `AmiClickTest`/`AmiSetMouse` above.

  **Stack note, hard-won (2026-08-05):** the commodity's TREE/GETTEXT
  result buffers (`treeBuf`/`resultBuf`, ~4.5KB combined) are `static`,
  not stack-allocated. A Shell-launched process's default AmigaDOS stack
  is small; as locals they silently overflowed it, corrupting state such
  that the *first* ARexx command handled fine but the process took an
  illegal-instruction exception before the second could be processed --
  `rx` then reported a confusing "Host environment not found" for every
  later command, since the crashed task was never servicing the port
  again. Nothing about that symptom pointed at a stack overflow; don't
  reintroduce large stack-allocated locals in the per-message dispatch
  loop without moving them back to `static` (or `AllocVec`).
