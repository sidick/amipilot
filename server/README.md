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
  genuine matches and genuine escapes alike. `FSGET` returns the whole
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
