# server

The AmiPilot server commodity: hosts the `intuition-model` walker, the
action engine (input.device event synthesis), and the transports (ARexx
first, then serial.device, then TCP).

Lands in phase 0.2 onward -- see
[`docs/implementation-plan.md`](../docs/implementation-plan.md).

## Current state (phase 0.2, in progress)

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

The commodity itself (ARexx port, event loop) is not started yet.
