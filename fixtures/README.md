# fixtures

Conformance test applications used to exercise AmiPilot's locator tiers in
CI: a hand-written ClassAct/ReAction app (`classact-app/`) and a GadTools
app (`gadtools-app/`), each eventually carrying a manifest for tier-1
testing.

These are the phase 0.1 release-gate targets for `AmiInspect` (dumping
their gadget trees correctly) and later the click-and-assert gate for the
server. See [`docs/implementation-plan.md`](../docs/implementation-plan.md).
