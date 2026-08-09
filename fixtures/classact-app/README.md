# classact-app

Hand-written ClassAct/ReAction test application -- phase 0.1's primary
`AmiInspect` fixture, later the server's click-and-assert conformance
target. Built from `window.class`/`layout.gadget`/`button.gadget`/
`string.gadget`/`checkbox.gadget` (not GadTools, unlike
`fixtures/gadtools-app`) -- its three gadgets are all `layout.gadget`
children, the project's own documented "Confirmed limit": permanently
invisible to structural walking on classic AmigaOS 3.x.

Also implements the `CAAPP.WHERE` ARexx port (issue #49, `manifest/
SPEC.md`'s "The cooperative geometry port") -- the cooperative escape
hatch for exactly that limit. `CAApp.manifest` (format version 2)
addresses all three gadgets via `WHEREGADGET` records resolved through
this port, rather than the plain `GADGET` records it could never use.
See `src/main.c`'s own header comment and `tests/copperline/
where-test.py` for the end-to-end confirmation.
