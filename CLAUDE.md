# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

AmiPilot is an object-level GUI automation system for classic AmigaOS: an
on-Amiga automation server plus a host-side Python client that drive real
Amiga GUIs semantically (find a window/gadget by ID, label, or role; act
with genuinely synthesised input; assert on state) rather than by pixel
coordinates or screen scraping. The full design, locator-tier model, wire
protocol, and phase sequencing live in `docs/implementation-plan.md` —
read that before making architectural decisions; this file only covers
what's needed to build and navigate the code day to day.

**Current state:** v1.0 released (phase 1.0 complete — the first full
release). Since then, the cooperative geometry port is real too (issue
#49, not yet in a tagged release): the escape hatch for gadgets nested
inside a `window.class` window's `layout.gadget`, permanently invisible
to structural walking on classic AmigaOS 3.x. A manifest gains a
version-2 record pair, `WHEREPORT <port-name>` and `WHEREGADGET
<logical-name> <window-name>`, naming a small optional ARexx port the
*application itself* exposes, answering `WHERE <name>` with its own
live `GetAttr(GA_Left/GA_Top/GA_Width/GA_Height)` geometry;
`CLICK`/`TYPE @name` then act on that geometry with a genuine
`input.device` click (`AmipClickWindowRelative()`,
`server/src/action.c`) exactly as they would for a plain `GADGET` name
-- discovery is cooperative, but actuation stays real input, unlike
`MUIREXX`, where the target's own port does the acting too. The
standalone `WHERE` verb (`server/src/where.c`) is a diagnostic probe:
`GETTEXT`/`DRAG` have no path through a `WHEREPORT` at all, an honest
stated limit (RC 10, "geometry only") rather than a silent fallback.
See `manifest/SPEC.md`'s "The cooperative geometry port" section for
the full wire contract, including its "Clash guard" note on why
`WHEREPORT` resolution is exact-match only (no `MUIREXX`-style `.1`
fallback) and why applications should use a dedicated port name.
Verified end to end against `fixtures/classact-app`'s own new
`CAAPP.WHERE` port (`tests/copperline/run.sh`'s `run_where_check`,
`tests/copperline/where-test.py`) -- all three of that fixture's
gadgets, previously named by nothing at all (its manifest deliberately
named zero gadgets as the honest example of this exact limit), are now
addressed purely via `WHEREGADGET`; `CAApp.golden` is unchanged,
confirming the walker's own view of the window genuinely didn't change.
A real bug found building this, live (2026-08-09): a `RexxMsg`
constructed by hand via `CreateRexxMsg()`/`FillRexxMsg()`/`PutMsg()` --
the same recipe `MUIREXX`'s own `AmipMuiRexxSend()` (`server/src/
muirexx.c`) already used successfully against real MUI-Demo -- arrives
at the receiver with its node type left at `NT_MESSAGE`, not
`NT_REPLYMSG`; `rexxsyslib.library`'s own `IsRexxMsg()` reports such a
message as not a genuine `RexxMsg` at all despite every other field
being correct. Two attempted sender-side fixes (pre-marking
`ln_Type = NT_REPLYMSG` before sending; using a genuine
`CreateArgstring()`-backed `rm_Args[0]`) changed nothing. Root cause:
`IsRexxMsg()` checks `ln_Type == NT_REPLYMSG`, which `ReplyMsg()`
sets -- the right check for a *sender* inspecting its own reply, not
a *receiver* validating an incoming, not-yet-answered command, which
is legitimately still `NT_MESSAGE` regardless of how the sender is
built. The field that actually means "this is a command invocation"
is `RXCOMM` itself (already set correctly by every sender here) --
fixed by having `CAAPP.WHERE` validate incoming messages with
`rm_Action & RXCOMM` instead of `IsRexxMsg()`, see the doc comment on
`HandleWhereMessage()` in `fixtures/classact-app/src/main.c`. A
genuinely standard, ordinary ARexx port -- nothing dedicated-port-only
about this fix, any third-party implementer (e.g. AmiAuth) needs only
the same one-condition change.

`MENUPICK`'s pointer-based fallback for shortcut-less items is real
too now (issue #63, not yet in a tagged release): previously,
`MENUPICK` only worked for items with a real keyboard shortcut
(`AmipMenuPickByShortcut()`); an item with none was rejected outright.
`AmipMenuPickByPointer()` (`server/src/action.c`) now drives the
pointer path automatically for such items -- a genuine synthesized
RMB-down/move/move/RMB-up sequence, the same "real input.device
events, not a shortcut" principle `CLICK`/`DRAG` already follow, not a
forged `IDCMP_MENUPICK` message (considered and rejected: it would
bypass real IDCMP/activation/`WFLG_RMBTRAP`/`IDCMP_MENUVERIFY`
handling entirely, a false-positive risk this project's own design
principle exists to prevent). Two real, non-obvious things had to be
found live (2026-08-09) before this worked: (1) RMB-down alone only
switches the screen's own title bar into menu mode -- the pulldown
itself doesn't open until the pointer is actually moved onto the
target menu's own title text, confirmed only after a plain RMB-down
produced zero observable effect (no pulldown render, no fallback
`IDCMP_MOUSEBUTTONS`) even with the pointer already positioned inside
the window and a full second of `Delay()`; and (2) the RKRM documents
no formula at all for a submenu's own screen position (only "overlaps
its parent item's own select box somewhere") -- the real placement
(`parentLeft + parentWidth` for X, ignoring the sub-item's own
`LeftEdge` entirely; `parentTop + item->TopEdge` for Y) was measured
pixel-for-pixel against a real screenshot captured mid-pick under
Copperline (`amipilot.screenshot`'s own PNG conversion, cropped and
scanned for the popup's actual border pixels) after an initial guess
(adding `item->LeftEdge` on top of the parent's right edge) overshot
the box entirely. See `server/README.md`'s Menus section for the full
mechanism and `fixtures/gadtools-app`'s new shortcut-less "Toggle" and
"Sub NoShortcut" items, verified via `run_menu_check`'s
`MENUPICK-TOGGLE-POINTER`/`MENUPICK-SUBITEM-POINTER` checks.

`STRING_KIND`/`INTEGER_KIND` role classification is real too (issue
#64): both GadTools kinds create the same underlying `GTYP_STRGADGET`,
so nothing in the raw gadget structure told them apart -- a smaller
version of the `BUTTON_KIND`/`CHECKBOX_KIND` problem already solved by
`ClassifyBoolGadget()`'s `GT_GetGadgetAttrsA` kind-probe technique.
Applied the same way here: `GT_GetGadgetAttrsA`'s documented per-kind
tag table (gadtools.doc) lists `GTIN_Number` under `INTEGER_KIND` only,
not `STRING_KIND` -- asking a plain string gadget for it is a safe,
documented no-op (`numProcessed` stays 0), the discriminator itself
rather than a guessed heuristic. New `ClassifyStringGadget()`
(`intuition-model/src/walk.c`) reports `role=integer` for a real
`INTEGER_KIND` gadget and leaves `role=string` unchanged otherwise.
Verified against a new Count `INTEGER_KIND` gadget added to
`fixtures/gadtools-app`'s own window (it shares the Host `STRING_KIND`
gadget's underlying type, so the two now genuinely need the
discriminator to tell apart) -- confirmed live under Copperline via
`tests/copperline/run.sh`'s `run_golden_check`, whose `GTApp.golden`
now carries the new gadget's real, live-measured geometry and
`role=integer` line.

WB3.2-era BOOPSI/ReAction role classification is real too (issue #69):
`ClassifyByClassID()` (`intuition-model/src/walk.c`) previously
recognised only 10 of the classes documented in NDK 3.2's own
`gadgets/` header set, leaving the rest at `role=custom`. Twelve more
now get a real role -- `clicktab.gadget` (`role=page_tab_list`, the
class that motivated filing this issue: Hyperion's WB3.2-native tabbed-
panel widget), `colorwheel.gadget` (`role=color_wheel`),
`datebrowser.gadget` (`role=calendar`), `fuelgauge.gadget`
(`role=progress_bar`), `getcolor.gadget`/`getfile.gadget`/
`getfont.gadget`/`getscreenmode.gadget` (`role=color_chooser`/
`file_chooser`/`font_chooser`/`screenmode_chooser` -- AT-SPI-style
names for "a compound button that opens a system requester and shows
the result", which is genuinely what all four are per their own NDK
autodocs), `gradientslider.gadget` (`role=slider` -- reuses the
existing role rather than inventing a redundant one, since AT-SPI
itself has no separate "gradient slider" role either), `palette.gadget`
(`role=palette`), `sketchboard.gadget` (`role=canvas`),
`speedbar.gadget` (`role=toolbar`), and `texteditor.gadget`
(`role=text_editor`). Every class-ID string was confirmed against real
NDK 3.2 headers first (pragma/*_lib.h's own "<name>.gadget" comment, or
reaction_macros.h's convenience-Object macros for the two classes --
colorwheel, gradientslider -- that register a PUBLIC class name instead
of needing an explicit `XXX_GetClass()` call), not guessed, then
live-verified against a new dedicated fixture,
`fixtures/reaction-classes-app`, one instance of each class attached
DIRECTLY to a plain classic window rather than via `window.class`/
`layout.gadget` (which would make them exactly as unreachable as
issue #49's own confirmed limit already documents -- this fixture
exists specifically to sidestep that, not to demonstrate it). Two real
bugs found building this, live (2026-08-09): `colorwheel.gadget`'s
`NewObject()` silently returned NULL until `WHEEL_Screen` was supplied
-- its own autodoc does say this tag is required, but it's easy to
miss among a page of optional ones, and the failure mode (NULL, no
error text) gives no hint why; and `speedbar.gadget` registers its
class as literally `"speedbar"`, NOT `"speedbar.gadget"` like every
other class checked here -- confirmed only by reading the walker's own
`className` field back from a live object, since nothing in the NDK
materials flags this exception. `fixtures/reaction-classes-app`'s own
`ReactionClassesApp.golden` locks in the live-confirmed output for
regression protection, verified via `tests/copperline/run.sh`'s
`run_golden_check`. Five real classes from the same research pass were
deliberately left unclassified, not silently missed --
`space.gadget`/`virtual.gadget` (honest limits, `virtual.gadget`
sharing `layout.gadget`'s own unreachable-children problem),
`listview.gadget` (its own autodoc says `listbrowser.gadget`, already
mapped, is a strict upgrade), and `tabs.gadget`/`tapedeck.gadget`
(both ship as real library files on a stock WB3.2.3 install, confirmed
on disk, but neither has a documented NDK-supported construction path
in this project's own NDK 3.2 snapshot -- no `GetClass()` proto/pragma
header and no `reaction_macros.h` convenience macro exists for either)
-- see `userdocs/Locator-Tiers-and-Limits.md`'s own writeup for the
full reasoning behind each.

Issue #52's Requester gap is fully closed now (the detection half,
`WAITFOR REQUESTER`, already shipped in phase 0.5 -- see below;
acting and the system-wide case both landed 2026-08-09): investigating
live whether the original
design sketch's `REQ=` locator idea was actually needed turned up a
genuine surprise that made it unnecessary for the common case. A
window-owned `AutoRequest()`/`BuildSysRequest()`/`EasyRequest()`
requester doesn't attach invisibly to the owning window at all -- it
opens as a completely ordinary, SEPARATE `struct Window` (confirmed
live via `TREE`: while the requester is up, the SAME window pattern
resolves to a different window with different bounds and a real,
directly-walkable gadget list -- two `frbuttonclass` objects for
Yes/No, no trace of the app's own gadgets). That new window shares its
owner's EXACT title text (already the detection mechanism
`WaitForRequesterPresent()` relies on, see below), so ordinary `CLICK
<window-pattern> <gadget-id>` already reaches it -- zero new locator
syntax, zero wire changes. `BuildSysRequest()`'s own autodoc documents
a fixed, app-independent `GadgetID` convention (PosText's gadget is
always `GadgetID` `TRUE`/1, NegText's is always `FALSE`/0) --
confirmed by actually clicking `GadgetID` 1 against
`fixtures/gadtools-app`'s own `Ask` button and watching a real
`AutoRequest()` genuinely dismiss (`tests/copperline/
requester-test.py`'s `REQUESTER-YES-CLICKED`/`REQUESTER-DISMISSED`
checks, verified by a subsequent `WAITFOR REQUESTER` correctly timing
out again -- not a stale-state false pass).

The system-wide, no-owning-window case (a real disk-swap/DOS-error/
Guru requester, `BuildSysRequest(NULL, ...)`) -- the harder half of the
original issue -- is real too now, closing #52 fully. It turned out to
have its own real, documented signal: confirmed live that
`AutoRequest(NULL, ...)` (exactly what `dos.library` itself calls
internally for a genuine disk-swap "Please insert volume..." prompt,
per `AutoRequest()`'s own autodoc NOTES section, not a simulation)
produces a window titled EXACTLY `"System Request"` -- Intuition's own
documented fallback title for a titleless requester
(`EasyRequestArgs()`'s own autodoc: "if this is NULL... 'System
Request.'"), not a coincidence, since `AutoRequest()`/
`BuildSysRequest()` have no title parameter to override it with.
`WaitForRequesterPresent()` (`server/src/amipilotserver/main.c`) now
matches that exact title as a third detection branch. Structurally
identical to the window-owned case (same `frbuttonclass` Yes/No
gadgets, same fixed `GadgetID` convention), so `CLICK "System Request"
1` reaches it through the exact same mechanism already proven above --
no new action-engine code needed for THIS half either.
`fixtures/gadtools-app` gained a second button, "Ask System"
(`GID_ASK_SYSTEM`), exercising the real `AutoRequest(NULL, ...)` path;
`tests/copperline/requester-test.py`'s
`SYSTEM-REQUESTER-DETECTED`/`SYSTEM-REQUESTER-YES-CLICKED`/
`SYSTEM-REQUESTER-DISMISSED` checks confirm detection, action, and a
genuine dismiss end to end, the same rigor as the window-owned case.
English-locale specific (like every other window/screen title this
project already treats as Locale-sensitive) -- and a real third-party
app titling its own window exactly `"System Request"` would
false-positive here, an accepted, exceedingly unlikely real-world
collision, not a design flaw. See `server/README.md`'s own WAITFOR
REQUESTER section for the full mechanism.

Whether `GETTEXT` could read a requester's own body text was
investigated the same day and settled as a confirmed PERMANENT limit,
not a gap left for later: `struct Requester->ReqText` (a `struct
IntuiText *`, per `BuildSysRequest()`'s own autodoc) is where the body
text would live -- but a live dump of `FirstRequest`/`ReqCount` for
EVERY open window while a real `AutoRequest()` was up (a temporary
`AmiInspect` diagnostic, reverted after use) showed both NULL/0 on
BOTH the owning window (genuinely blocked inside `AutoRequest()` at
the time) and the requester's own separate window -- ruling out "just
checking the wrong window" as an alternative explanation. No `struct
Requester` exists anywhere reachable in this scenario on this
project's real target OS/ROM; the body text is rendered directly into
the requester window's own bitmap at open time, with no structural
field retaining it afterward for anything to read back. The same shape
as this project's other confirmed structural-reading limits (a
`PLACETEXT_IN` button's baked-in label, `layout.gadget`'s invisible
children) -- `GETTEXT` needs a live field to query, and there
genuinely isn't one here.

Phase 0.5 (reliability and reach into the wider ecosystem)
before it: `WAITFOR` (including its
`TEXT=` condition) and `CLICK`'s `EXPECT=` (wait/expectation
primitives, docs/implementation-plan.md's "Async by design" section)
-- see `server/README.md`'s own section. Quirk profiles are real too:
same manifest format/parser (`manifest/SPEC.md`), no new machinery,
just a documented convention for community-authored, third-party-app
manifests carrying known-oddity notes as comment lines -- see
`manifest/SPEC.md`'s own "Quirk profiles" section. The honest-limits
toolkit-to-tier table lives in
`userdocs/Locator-Tiers-and-Limits.md`. Golden-tree fixtures are real
too: `amipilot.golden`'s `assert_golden()`/`GoldenMismatch`, wired
into `amipilot dump --golden`/`Amipilot.assert_tree_matches()` and
into `tests/copperline/run.sh`'s `run_golden_check` against real,
checked-in golden files for both fixtures
(`fixtures/gadtools-app/GTApp.golden`,
`fixtures/classact-app/CAApp.golden`) -- see
`tests/copperline/README.md`'s "Golden-tree fixtures and Locale"
section for the real reproducibility caveat found while building
this (window/screen titles, and a real app's catalog-driven labels,
aren't Locale-invariant). The stock-app conformance set is real too:
`tests/copperline/stock-app-test.py` (wired into `run.sh`'s
`run_stock_app_check`) drives AmigaOS 3.2's own `SYS:Prefs/Time` --
launched over the wire, not a hand-written fixture -- end to end via
tier-2 `ROLE=`/`INDEX=` locators discovered purely from AmiInspect/
`amipilot dump` output; see its own header for what was actually
tried and confirmed live (its year field and "Save" button turned out
to be genuinely inert under this profile's `rtc: none` config -- an
honest finding, recorded as a quirk profile at
`tests/copperline/Time.manifest`, not silently worked around) and
`host/amipilot/client.py`'s `connect_with_retry()` docstring for a
real bug this work found and fixed along the way: the method left a
returned client's socket read timeout clamped to a leftover value
from its own retry loop (as little as 0.1s), silently breaking any
`WAITFOR`/`CLICK(expect=...)` whose `TIMEOUT=` exceeded it. A second
real bug from the same stock-app-conformance work, found and fixed
(issue #36, closed): `AmiInspect` genuinely hung (a true deadlock, not
a spin loop -- confirmed via GDB attached live to Copperline's `--gdb`
remote server, `TaskWait`/`tc_SigWait` archaeology, and an
instrumented walker build that pinned the exact gadget) walking a
different stock app's window (`SYS:Prefs/WBPattern`, one of its
custom-drawn Preview/Sketch boxes) -- its `GadgetType` bits claimed
`GTYP_CUSTOMGADGET` without a real BOOPSI `_Object` header behind
them, and `WalkGadgetList()` used to trust `OCLASS()`'s result
unconditionally. Fixed with a `TypeOfMem()` sanity check (see
`intuition-model/src/walk.c`'s own comment there) before dispatching
anything through it; `tests/copperline/run.sh`'s `run_wbpattern_check`
regression-tests this against the real app. The MUI-ARexx bridge tier
is real too, completing phase 0.5's scope: `MUIREXX <app-base>
[TIMEOUT=<n>] <command...>` (`server/src/muirexx.c`,
`Amipilot.mui_command()`) sends an ARexx command verbatim to a MUI
application's own port -- confirmed live against AmigaOS 3.2's own
MUI-Demo that MUI's built-in ARexx support is a small, universal
seven-command set (`quit`/`hide`/`show`/`activate`/`deactivate`/
`info`/`help`), not a generic widget-value accessor, so this bridge
is an honest passthrough rather than a CLICK/TYPE-shaped verb built on
a false promise -- see `server/README.md`'s own section and
`tests/copperline/run.sh`'s `run_mui_check` (skips cleanly when MUI
isn't installed on the configured Workbench volume, since it's a
third-party archive, not a standard Workbench component).
`intuition-model/` (the walker library) and `amiinspect/` (the Shell
command) are real, building, and verified on-target. `server/` is
real: the action engine (`server/src/action.c`, click/type/geometry/
menu-pick) and `AmiPilotServer` (`server/src/amipilotserver/`), a
commodity hosting both behind a genuine public ARexx port, the serial
wire transport (`server/src/serial.c`), and (0.4) TCP
(`server/src/tcp.c`, listen-mode only) — framing contract in
`server/WIRE.md`. TCP now has opt-in hardening: `TCPALLOW` (a source-
IP/CIDR allowlist, comma-separated single value — NOT `/M`, ReadArgs
only allows one repeatable keyword per template and `FSROOT` already
claims it) and `TCPPASSWORD` (gates the `AUTH` verb, defaults to the
public `"amipilot"` the host client sends automatically). Neither is
mandatory, and **neither makes this internet-safe** — no TLS, a
public default password, no rate-limiting; LAN/trusted-network use
only, see `server/README.md`'s TCP section. Phase 0.4 additions
beyond TCP: `LAUNCH` (start a test subject over the wire), the
allowlist-scoped file API (`FSLIST`/`FSSTAT`/`FSMKDIR`/`FSDELETE`/
`FSGET`, `server/src/fs.c`), menu walking + selection (`MENU`/
`MENUPICK`, `intuition-model`'s `AmipWalkMenuStrip()` — shortcut-based
and, since issue #63, genuine pointer-based selection too for items
with no shortcut), multi-screen support (`SCREENS`/`SCREEN=`, keyed off
`Screen->DefaultTitle`, not the live `Title` field), tier-2 semantic
locators (`ROLE=`/`LABEL=`/`INDEX=` on CLICK/TYPE/GETTEXT's classic
form, resolved via a fresh `AmipWalkWindow()` walk — proximity-to-
label matching deliberately not built, an honest limit not a gap),
and `DRAG` (`server/src/action.c`'s `AmipDragAt()`/
`AmipDragGadgetBy()`/`AmipDragGadgetToGadget()` — an offset form for
sliders/scrollers and a gadget-to-gadget form for drag-and-drop,
built on a single press/absolute-jump/release, not synthesized
continuous motion). See
`server/README.md` for the full verb set and what's verified for each.
Phase 1.0 (all of it now released as v1.0): `FSPUT <path>
<byte-count> [TIMEOUT=<n>]` (host-to-Amiga file writes,
`server/src/fs.c`'s `AmipFsPut()`) is real — the wire's first request
to carry a raw binary body, via a new length-prefixed request-payload
framing documented in `server/WIRE.md`'s "Request payloads" section
(symmetric to the response side's own framing). Deliberately
wire-only: there is no ARexx form at all, since `RexxMsg`/`ARG0()`
only ever carries string arguments — a real, permanent asymmetry
(stronger than `AUTH`'s own weaker one), not an oversight. Implemented
per-transport (`AmipSerialReadExact()`/`AmipTcpReadExact()`,
`server/src/serial.c`/`server/src/tcp.c`) since the shared, transport-
portable command parser (`AmipArexxParse()`) has no read primitive of
its own — see `server/README.md`'s File API section for the full
contract, including the drain-even-on-rejection connection-desync
guard. The other identified 1.0-blocking gap, real Workbench launch
with tooltype/project-argument support, is real too now: `WBLAUNCH
<icon-path> [TOOLTYPE=<key>=<value> ...] [ARG=<path> ...]`
(`server/src/wblaunch.c`) hand-builds a genuine `WBStartup`/`WBArg`
message and sends it to a real non-CLI `CreateNewProc()` process --
the same technique real launcher utilities (AmigaOS 45's own `WBLoad`;
the classic `WBRun`) use, not `workbench.library`'s V44+
`OpenWorkbenchObjectA()`, whose own autodoc BUGS section says
launching (not just opening drawers) was unsafe up to and including
V45.38 -- disqualifying against this project's V37 floor. One real
deviation from this feature's own original plan sketch, found doing
the research: tooltype overrides can't be merged "in memory... no
disk writes" as originally assumed -- a Workbench-started program
discovers its own tooltypes by reading its own icon file back off
disk, so there's no in-memory channel to hand it an override at all.
The real mechanism (what every real "tooltype override" launcher
actually does): merge into a scratch copy of the icon written to
`T:`, never the app's own real `.info`, and point the launch at that
instead -- cleaned up once the launched process exits and replies its
`WBStartup` message. Verified end to end against a real, Workbench-
startable fixture (`fixtures/wbapp`, its own icon stamped once by a
small helper reading the system's own default `WBTOOL` icon --
`fixtures/wbapp/src/makeicon.c`) via `tests/copperline/run.sh`'s
`run_wblaunch_check`: a bare launch, a `TOOLTYPE=` override that
changes one key while leaving another untouched (the real merge, not
a full replace), an `ARG=` project-file argument, and a bad-icon
rejection. `SCREENSHOT [SCREEN=<substring>] [WINDOW=<pattern>]`
(GitHub issues #41 and #44, `server/src/screenshot.c`) is real too:
raw, uncompressed bitmap capture -- planar OR Picasso96/RTG -- inspector
tooling only (not a `CLICK`/`TYPE`/`GETTEXT`-style locator mechanism).
The wire carries raw pixel bytes plus a small header, and ALL image-
format/colour-space work (IFF ILBM, PNG, P96 pixel-format decoding)
happens host-side (`host/amipilot/screenshot.py`, stdlib-only, no
Pillow), the same "wire stays simple, host does the rendering" split
TREE/`amipilot dump` already use. An earlier RTG guard
(`BitMap->Flags & BMF_STANDARD`) was found, via live testing against a
real, completely ordinary Copperline Workbench screen, to be simply
wrong -- that flag is never set on Intuition's own screen bitmaps at
all, so it rejected the normal case it was meant to allow. Removed and
replaced with the REAL, verified mechanism from Picasso96API.library's
own published SDK interface data (not redistributed in this repo --
see `server/include/p96_compat.h`'s own header comment for exactly
what was independently reproduced and why):
`p96GetBitMapAttr(bm, P96BMA_ISP96)`, documented safe to call on any
bitmap without locking it. `Picasso96API.library` is opened
OPTIONALLY at startup (same graceful-degradation pattern as
`GadToolsBase`/`KeymapBase`/`GfxBase`) -- absent, or the target screen
isn't genuinely P96-backed, and the classic planar path (issue #41)
runs completely unchanged; never required. A genuine P96 bitmap's
pixel memory is read via `p96LockBitMap()`'s own `RenderInfo` buffer,
held only for the raw memcpy, matching the SDK's own explicit
locking protocol; its native pixel format (CLUT or a truecolor/
hicolor RGB byte order -- YUV excluded, the SDK's own docs mark those
hardware-only) is sent raw over the wire and decoded host-side,
including the documented `PC`-suffix byte-swap pitfall (16-bit `PC`
formats are little-endian, non-`PC` big-endian). Verified end to end
against a real running `fixtures/gadtools-app` window via
`tests/copperline/run.sh`'s `run_screenshot_check` -- which, since
Copperline has no RTG emulation at all, is really a live confirmation
that the classic planar path (and P96Base's graceful absence) still
works correctly, NOT a confirmation of the P96-active capture path
itself. That path is now verified too, manually against real
Picasso96 3.6 + `uaegfx` under Amiberry (Copperline still has no RTG
emulation, so this isn't a `make test-target` regression check, just
a real, by-hand confirmation): a genuine P96 CLUT screen decoded back
an exact match of a known painted pen pattern, and a genuine P96
truecolor (`R5G6B5PC`) screen decoded back colour values matching the
expected 5/6/5-bit quantization exactly -- real hardware-adjacent
confirmation of both the SDK's own bitmap-locking contract and this
module's `PC`-suffix byte-swap handling, not just the planar/absent
path. See `server/README.md`'s own SCREENSHOT section for the full
detail; wiring this into an automated Amiberry-based check remains
real, tracked follow-up work, not done. The parsing/encoding logic
itself (exact byte layout for both capture shapes, IFF chunk shape,
PNG chunk CRCs, the P96 pixel-format decode table) has its own
dedicated host-side unit tests (`host/tests/test_screenshot.py`)
against synthetic captures. `WINDOWMOVE [SCREEN=<s>] <window-pattern>
<dx> <dy>` / `WINDOWSIZE [SCREEN=<s>] <window-pattern> <width>
<height>` (`server/src/action.c`'s `AmipWindowMoveBy()`/
`AmipWindowResizeTo()`) are real too: whole-window drag and resize,
reusing the exact same `AmipDragAt()` press/move/release primitive
`DRAG`'s gadget forms already use, just anchored on the window's own
title bar (`WFLG_DRAGBAR`) or sizing gadget (`WFLG_SIZEGADGET`)
instead of a gadget. Classic locator form only, no `@name` -- same
scope as TREE/MENU, since this acts on a whole window and there's no
verified separate "resolve a window-only logical name" path in the
manifest resolver. No new "get window position/size" verb was added
-- TREE's own response already carries a window's current `[left,top
WxH]`. Verified end to end via `tests/copperline/run.sh`'s
`run_windowmoveresize_check` against `fixtures/second-screen-app`
(the only fixture given a real sizing gadget, deliberately kept off
`gadtools-app`/`classact-app` since either would risk shifting their
own checked-in golden-tree fixture).
`manifest/` carries the manifest contract (`manifest/SPEC.md`, parsed
by `server/src/manifest.c` — no screen-awareness yet, `SCREEN=` only
applies to the classic locator form). `host/` is a real, installable
package: the wire client (`host/amipilot/wire.py`), text-format
parsers (`host/amipilot/model.py` for TREE, `menu.py` for MENU,
`screen.py` for SCREENS, `fs.py` for the file API), the object API
(`host/amipilot/client.py` — `Amipilot`, what test code actually
imports, including `wait_for_window()`/`wait_for_screen()` polling
helpers), `amipilot dump` (`host/amipilot/dump.py`, a console script
via `host/pyproject.toml`), and the pytest plugin
(`host/amipilot/pytest_plugin.py`, auto-registered via the `pytest11`
entry point) — its session-scoped `amipilot` fixture boots a
Copperline config and hands a test a connected client, which is phase
0.3's actual release gate ("a host pytest clicks a button and asserts
a label changed, deterministically, under the emulator") verified
live via `tests/copperline/pytest-example/`. All of it has real test
coverage (`make test-host` runs pytest — a superset that also collects
the stdlib-unittest files here — with `host/` editable-installed first
so the plugin's entry point is real, not force-loaded).
User-facing documentation lives in `userdocs/` (built
as a MkDocs site,
`mkdocs.yml`) and mirrored to AmigaGuide via `make guide`
(`tools/docs2guide.py`) — see `userdocs/Building-and-Testing.md`.

## Build commands

Requires Bebbo's m68k-amigaos GCC (`m68k-amigaos-gcc`) on `PATH`, or use
the container image:

```sh
make amiga          # build intuition-model + AmiInspect
make fixtures        # build the on-Amiga fixture apps (fixtures/*)
make docker          # run 'make amiga fixtures' inside ghcr.io/sidick/amiga-dev
make clean
```

CI verb contract (`sidick/amiga-workflows/build-test.yml`, invoked from
`.github/workflows/ci.yml`) — these Makefile targets exist because CI
calls them by name, not because they're the primary local entry points:

```sh
make build           # = amiga + fixtures
make test-host        # host-side unit tests (host/tests, stdlib unittest, no deps)
make test-target      # boots both fixtures under Copperline, asserts AmiInspect's output
make lint             # semgrep --config auto over intuition-model/ amiinspect/ server/ fixtures/
```

`make test-target` (`tests/copperline/run.sh`) is a real check when
`tests/copperline/copperline.local.toml` exists locally (see "On-target
testing" below); it skips cleanly, not a false pass, when that
machine-specific file is absent (e.g. in CI, which has no such
Workbench/ROM asset yet). It caught the `GTYP_CUSTOMGADGET` masking
crash below when deliberately reintroduced — proven, not just written.

Toolchain flags worth knowing before touching the Makefile: `-m68000
-msoft-float` (the real target floor, not a default — see "Minimum
requirements" below) and `-noixemul` (links against libnix, not
ixemul.library — see the `libnix` skill for its conventions if editing
startup/library-open code).

## Minimum requirements

AmigaOS 2.04 (V37) floor, plain 68000, no FPU, ~1MB RAM. Recommended/
CI-tested config is OS 3.1, 68020, 2MB chip + 8MB fast. Full rationale in
`docs/implementation-plan.md` under "Minimum requirements" — check it
before using any API newer than V37, or anything requiring an FPU.

## On-target testing

`make test-target` (`tests/copperline/run.sh`) is now a real automated
harness — a dozen-plus checks covering every verb, run live against a
real Copperline boot; see the "Build commands" section above. Two ways
to actually run code against real AmigaOS behavior interactively:

**Preferred: Copperline** (`brew install copperline`), a deterministic,
scriptable emulator — see `tests/copperline/README.md` for full setup.
Its `--control` JSON-RPC server (driven via `copperline-ctl`) gives
frame-accurate `run_until {seconds|stable_frames}` waits and an
`input.mouse_to {x, y}` that servos the pointer to an exact pixel by
watching sprite 0, instead of guessing relative mouse deltas and
screenshotting to check. For a scripted boot-run-verify cycle with *no*
GUI input at all, add commands to `S:User-Startup` on the mounted
Workbench volume (**back it up first, restore it after** — this mutates
a real, possibly-shared Workbench install) and read results straight off
the host filesystem via the `SRC:` hostfs mount — no screenshot parsing,
no window-focus fights. This is how the checkbox-classification fix and
the `GT_Underscore` fix below were verified.

**Fallback: Amiberry MCP tools** (`mcp__amiberry__*`) for interactive,
by-hand debugging (visually checking layout, poking around) against the
`amipilot.uae` config (Kickstart 3.2, A1200/AGA, Workbench 3.2.3). Slower
and fussier for anything scripted — expect to fight relative mouse deltas
and window z-order — so reach for Copperline first unless you specifically
need to watch it interactively.
1. `mcp__amiberry__launch_and_wait_for_ipc` with `config: "amipilot.uae"`.
2. Immediately call `mcp__amiberry__set_active_instance` with `instance:
   0` — without this, IPC calls silently fail with "Connection refused"
   even though the socket is live (a stray-connector-process quirk, not a
   real connection problem).
3. The config mounts this repo's working directory as `SRC:` and a
   Workbench hard drive as `DH0:` — freshly built binaries under `build/`
   are immediately visible as `SRC:build/...` with no copying step.
4. Open a Shell (Right-Amiga+E → type `NewShell` → Return), `run
   SRC:build/fixtures/GTApp` to launch a target, `SRC:build/AmiInspect
   WINDOW=<substring>` to inspect it. Redirect output to a file on `SRC:`
   and read it back rather than trusting a screenshot.
5. `mcp__amiberry__kill_amiberry` when done.

Both paths: always rebuild (`make amiga fixtures`) before booting — the
hostfs/SRC: mount only exposes what's already on disk, and `make clean`
silently leaves you testing against a binary that no longer exists.

Two known gaps already found this way, documented as TODO comments at
their exact site in `intuition-model/src/walk.c`, not silently patched
around: GadTools populates `gadget->GadgetText` for external-label
placements (`PLACETEXT_LEFT/RIGHT/ABOVE/BELOW`) on kinds like
`CHECKBOX_KIND`/`STRING_KIND`, but **`BUTTON_KIND` is a documented
exception** — confirmed against a second button added to
`fixtures/gadtools-app` (2026-08-07) that `PLACETEXT_RIGHT` bakes a
button's label into its rendered imagery exactly like `PLACETEXT_IN`
does, leaving `gadget->GadgetText` NULL either way; a button's label
is invisible to this tier under every `PLACETEXT_*` value tried, not
just `PLACETEXT_IN` as originally thought (address a button by
`GA_ID` or a `ROLE=`/`INDEX=` locator, not `LABEL=`); and
`BUTTON_KIND`/`CHECKBOX_KIND` both produce a plain `GTYP_BOOLGADGET`,
requiring a `GT_GetGadgetAttrsA` kind-probe (see `ClassifyBoolGadget`)
to tell them apart rather than a single flag check.

A third, more serious one found the same way (2026-08-07): a real,
OS-shipped stock application's `GadgetType` bits can claim
`GTYP_CUSTOMGADGET` (correctly matching the masked check the
`GTYP_CUSTOMGADGET`/`GTYP_BOOLGADGET` bitmask fix above already
requires) on a gadget that does **not** actually carry a genuine
BOOPSI `_Object` header — confirmed against AmigaOS 3.2's own
`SYS:Prefs/WBPattern` (its custom-drawn Preview/Sketch boxes).
`OCLASS()` there returns an implausible pointer, and dispatching
`GetAttr()`/`DoMethod()` through it unconditionally (as the walker
used to) wedges the entire machine — root-caused via GDB attached
live to Copperline's `--gdb` remote server (see the closed
investigation on GitHub issue #36 for the full methodology, including
two real amiga-gcc/vlink toolchain gotchas hit getting debug symbols
working at all). Fixed in `WalkGadgetList()` with a `TypeOfMem()`
sanity check on `OCLASS()`'s result before trusting it for anything —
the documented, honest way to confirm a pointer refers to allocated
system memory at all, not a guess at "plausible" address ranges.
Degrades to `role=custom` with no class/label, the same graceful path
an unrecognised class already took. `tests/copperline/run.sh`'s
`run_wbpattern_check` regression-tests this directly against the real
stock app, not just a hand-written fixture.

## Architecture

**`intuition-model/`** — the reusable Intuition walker library, with no
dependency on the server. Every walk starts with a brief `LockIBase()`
hold to confirm its target is still genuinely linked into Intuition's
own live screen/window lists, copying out a private model
(`AmipWindowModel` → linked list of `AmipGadgetModel`); nothing hands
out live Intuition pointers, and nothing patches anything (no
`SetFunction` anywhere in this codebase — see "Design principles" in
the implementation plan). **The lock does NOT stay held for the whole
walk** — `LockIBase()`'s own autodoc is explicit that no Intuition,
graphics, layers, or dos call is permitted while holding it, and the
per-gadget work (`GetAttr()`/`OCLASS()` dispatch, `CopyString()`'s own
`AllocVec()`) needs exactly those, so it happens after release. This
narrows, but doesn't eliminate, the gap between the liveness check and
the walk finishing — a window/gadget closing in that instant is a
known, accepted risk (same shape as `AmipIsWindowOpen()`'s own
documented limit for the action engine, below), not a guarantee this
library claims to provide. `AmipRole` is an AT-SPI-style
classification independent of which toolkit produced the gadget
(GadTools, BOOPSI/ReAction, MUI). Role classification covers plain
GadTools kinds (`ClassifyGadget`/`ClassifyBoolGadget`) and BOOPSI/
ReAction classes via `OCLASS()` (`ClassifyByClassID`) — a documented NDK
mechanism for reading a live BOOPSI object's class name from outside,
not a hack. **Confirmed limit:** a `window.class` window attaches only
its single top-level `layout.gadget` to `window->FirstGadget` — the
layout's own button/string/checkbox children aren't individually
walkable there, and there's no public API to enumerate them on classic
OS 3.x (see the implementation plan's "Honest limits" section and the
comment at `ClassifyByClassID` in `walk.c`). Don't try to "fix" this
with a reverse-engineered private struct — it's a stated, permanent
constraint, not an oversight.

Library-base convention: functions that need `gadtools.library` (the
`CHECKBOX_KIND` discriminator) consume the global `GadToolsBase` via
`proto/gadtools.h`'s `extern` declaration but never open or close it —
the calling program (currently `amiinspect/src/main.c`) owns that
lifecycle, same pattern as `IntuitionBase`. If `GadToolsBase` is `NULL`
(library not opened, or genuinely unavailable), classification degrades
gracefully rather than failing.

**Always initialize library-base globals** (`struct Library *FooBase =
NULL;`, never `struct Library *FooBase;`). An uninitialized base is a
COMMON symbol, which doesn't stop the linker from pulling libnix's own
same-named archive member — and that member carries a pre-`main()`
auto-open constructor that derives the library name from the base name
and aborts the whole program if the open fails (for `WindowBase` it
tries the nonexistent `"window.library"`, printing "window.library
failed to load" and exiting with rc 20 before any of your code runs).
This cost a full debugging session on 2026-08-05; see the header comment
in `fixtures/classact-app/src/main.c`. Two related ReAction findings
from the same session: BOOPSI gadget objects need `-lamiga`
(`DoMethod`/`NewObject` varargs marshaling), and `string.gadget`/
`checkbox.gadget` don't register public class names — use
`STRING_GetClass()`/`CHECKBOX_GetClass()`, not
`NewObject(NULL, "string.gadget", ...)` (which works for
`button.gadget` but returns NULL for these).

**`amiinspect/`** — standalone Shell command (`ReadArgs` template
`WINDOW/K`), the platform's first UIA-Inspect-equivalent. Finds a window
by title substring (or defaults to the active window), walks it via
`intuition-model`, prints the gadget tree. No host or server session
required — this is deliberately usable standing at the machine itself.
This is also *the* development/verification tool for everything else in
the walker: when in doubt about what a gadget structure actually looks
like, run `AmiInspect` against it rather than guessing from headers.

**`fixtures/`** — hand-written test apps used to exercise the walker
against real gadgets instead of only a window's built-in system gadgets.
`gadtools-app/` is a plain GadTools app (button + string + checkbox, real
`GA_ID`s) built directly against system libraries — it is the *target*
being inspected, not a consumer of `intuition-model`, so it doesn't link
against the library. When adding a fixture, remember `GT_Underscore` in
every `CreateGadget` tag list, or the `_` shortcut markers in labels
render as literal underscores instead of underlined shortcuts.

**`server/`, `host/`, `manifest/`, `tests/`** — see "Current state"
above for what's real in each. Phase 0.4 scope is now complete: TCP
transport + hardening (`TCPALLOW`/`TCPPASSWORD`), string entry, menus,
launch, the file API, tier-2 semantic locators (`ROLE=`/`LABEL=`/
`INDEX=`), and drag (`DRAG`, both the offset and gadget-to-gadget
forms) have all landed on main. Read the corresponding `README.md`,
`server/WIRE.md`, and the implementation plan's "Phases" section
before starting work in any of these.

## House conventions (this repo and its siblings)

- Version lives in `version.mk` (`VERSION`/`REVISION`, Amiga major.minor).
- License is BSD 2-Clause; new source files don't need a header (LICENSE
  file covers the repo), but workflow/CI files carry an SPDX header — see
  `.github/workflows/ci.yml` for the pattern.
- CI is a thin wrapper around a shared reusable workflow
  (`sidick/amiga-workflows/build-test.yml@v1`) that calls the Makefile's
  five-verb contract. Don't rename `build`/`test-host`/`test-target`/
  `lint`/`dist` — CI depends on those exact names.
- Real functions, not guessed heuristics: when the correct behavior isn't
  obvious from the header alone (as with the checkbox/button
  discrimination), find the documented contract (autodocs, RKRM) rather
  than assuming a plausible-looking flag check is right — then verify
  against a real fixture in the emulator, don't trust compilation alone.
- Releases are tag-driven: `.github/workflows/release.yml` fires on any
  pushed `v*` tag, checks the tag matches `version.mk`/`amipilot.readme`
  (`scripts/verify-version.sh`), then delegates to
  `sidick/amiga-workflows/aminet-release.yml` — builds `make dist`,
  creates the GitHub release with the `.lha`/`.readme` attached, then
  waits on the `aminet` environment's required reviewer before actually
  uploading to Aminet. Same shape as sibling repos sana2loop/AmiAuth. A
  release PR should still bump `version.mk` and `amipilot.readme`'s
  `Version:` field through normal review before the tag is pushed — the
  workflow verifies that match, it doesn't do the bump itself.
