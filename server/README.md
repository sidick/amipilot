# server

The AmiPilot server commodity: hosts the `intuition-model` walker, the
action engine (input.device event synthesis), and the transports (ARexx
first, then serial.device, then TCP).

Lands in phase 0.2 onward -- see
[`docs/implementation-plan.md`](../docs/implementation-plan.md).

## Current state (phase 0.3, in progress)

- **The wire (phase 0.3):** the same verb grammar over serial.device,
  framed per [`WIRE.md`](WIRE.md) (this directory) -- LF-terminated
  request lines in, `RC <code> <byte-count>` + payload out, `VERSION`
  as the handshake. `src/serial.c` is the transport (standing async
  1-byte read + SDCMD_QUERY chunk drain, xon/xoff disabled); one parser
  (`arexx_cmd.c`) and one dispatch (`HandleCommand` in
  `amipilotserver/main.c`) serve both transports, so ARexx RESULT
  strings and wire payloads are the same bytes. Enable with
  `AmiPilotServer SERIAL` (`SERDEVICE`/`SERUNIT`/`BAUD` to taste).
  Verified end-to-end by `make test-target`'s wire check: the real host
  client (`host/amipilot/wire.py`) connects through Copperline's
  `--serial tcp` bridge, handshakes, TYPEs into the fixture's string
  gadget, reads the value back, clicks Connect, and confirms the
  window died -- the phase 0.3 loop minus pytest, host-driven.

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
