#ifndef AMIPILOT_ACTION_ENGINE_H
#define AMIPILOT_ACTION_ENGINE_H

/*
 * action-engine: synthesises genuine input.device events (click, move) --
 * the "act" half of AmiPilot, alongside intuition-model's "read" half.
 *
 * Pointer motion uses IECLASS_NEWPOINTERPOS / IESUBCLASS_PIXEL (a struct
 * IEPointerPixel naming a screen and absolute pixel coordinates in it --
 * devices/inputevent.h, and the RKM's own Devices/Dev_examples/
 * Set_Mouse.c) -- the documented "put the pointer here" mechanism.
 * IECLASS_RAWMOUSE motion, by contrast, is relative-delta only and goes
 * through AmigaOS's normal pointer ballistics (tuned for a real mouse's
 * small per-frame deltas): a single large synthetic RAWMOUSE jump gets
 * heavily -- and asymmetrically between X and Y -- dampened, confirmed
 * empirically against fixtures/gadtools-app under Copperline
 * (2026-08-05): a click aimed at the Connect button landed on the
 * window's title bar instead, well short of the intended Y position,
 * even though IntuitionBase->MouseY read back as if the full delta had
 * applied. IEPointerPixel sidesteps ballistics entirely.
 *
 * Button transitions still use IECLASS_RAWMOUSE (there's no absolute
 * equivalent, and none is needed -- a button event doesn't move the
 * pointer), technique adapted from ../amirfb's proven input.device
 * injection code (src/amiga/rfb_server.c).
 *
 * Matches the implementation plan's "Act with real input, not shortcuts"
 * design principle: a click resolves the gadget's position at the moment
 * of the click, through the same input.device path a human's mouse
 * would use -- not a side door that skips the code under test.
 */

#include <exec/types.h>
#include <intuition/intuition.h>

typedef enum {
    AMIP_BUTTON_LEFT = 0,
    AMIP_BUTTON_MIDDLE,
    AMIP_BUTTON_RIGHT
} AmipMouseButton;

/* Opens input.device. Call once before any other function in this file.
 * Returns FALSE if input.device couldn't be opened (missing
 * intuition.library, or the device itself) -- every other call becomes a
 * safe no-op returning FALSE until AmipActionInit() succeeds. */
BOOL AmipActionInit(void);

/* Closes input.device. Safe to call even if AmipActionInit() failed or
 * was never called. */
void AmipActionShutdown(void);

/* Moves the pointer to absolute pixel coordinates (x, y) on screen, via
 * IECLASS_NEWPOINTERPOS/IESUBCLASS_PIXEL, then polls
 * IntuitionBase->MouseX/MouseY briefly to confirm Intuition's
 * input-handler task actually processed it (DoIO() completing only
 * confirms input.device queued the request). */
BOOL AmipMoveMouseTo(struct Screen *screen, WORD x, WORD y);

/* Moves to (x, y) on screen, then injects a real button-down followed by
 * a real button-up at that position -- two separate input.device
 * events, the same as a genuine click, not a single synthetic "clicked"
 * signal. */
BOOL AmipClickAt(struct Screen *screen, WORD x, WORD y, AmipMouseButton button);

/* Resolves gadget's current on-screen center from window's position plus
 * the gadget's own LeftEdge/TopEdge/Width/Height -- correct for classic/
 * GadTools gadgets and BOOPSI CUSTOMGADGET alike, the latter via
 * GetAttr(GA_Left/GA_Top/GA_Width/GA_Height), with GFLG_RELWIDTH/
 * RELHEIGHT/RELRIGHT/RELBOTTOM resolved against the window's own size
 * (see this function's definition in action.c). Pure geometry query --
 * doesn't bring the window/screen forward or touch input.device; callers
 * that act on the result (AmipClickGadget, AmipDragGadgetBy/
 * AmipDragGadgetToGadget below) do that themselves. FALSE (xOut/yOut
 * untouched) if window or gadget is NULL. */
BOOL AmipGadgetCenter(struct Window *window, struct Gadget *gadget, WORD *xOut, WORD *yOut);

/* Resolves gadget's current on-screen center (AmipGadgetCenter()) and
 * clicks it (AmipClickAt() with AMIP_BUTTON_LEFT), after bringing the
 * target screen/window forward. */
BOOL AmipClickGadget(struct Window *window, struct Gadget *gadget);

/* --- drag: press, move, release ------------------------------------ */

/* The raw two-point drag primitive: moves to (x1, y1), presses the left
 * button, moves to (x2, y2), releases -- two real input.device button
 * events plus two absolute IEPointerPixel jumps, the same events a
 * human's press-drag-release would produce, just not synthesized as a
 * continuous stream of intermediate motion events (a single jump
 * between the two endpoints, not many small ones). This is a known,
 * accepted limitation for v1: Intuition's own built-in gadget/prop
 * dragging (GadTools SLIDER_KIND/PROP_KIND scrollers, window drag bars)
 * tracks the pointer via ordinary mouse-move events regardless of how
 * many arrive, so a single jump between press and release is sufficient
 * for those -- but an application with its OWN pointer-motion-sensitive
 * drag handling (tracking per-frame deltas rather than just where the
 * button was released) could in principle behave differently under a
 * single jump than under continuous motion. Narrows to "the endpoints
 * are always genuine input.device events reaching the real event path,"
 * not "indistinguishable from a human dragging the mouse across every
 * intervening pixel." If the button-down succeeds but the move to
 * (x2, y2) fails, the button is still released (best-effort, avoids
 * leaving Intuition thinking a button is stuck down) before returning
 * FALSE. */
BOOL AmipDragAt(struct Screen *screen, WORD x1, WORD y1, WORD x2, WORD y2);

/* Drags gadget from its current center by a pixel offset (dx, dy) --
 * the natural shape for adjusting a slider/scroller (GadTools
 * SLIDER_KIND/PROP_KIND), which is a delta operation, not a locator-to-
 * locator one. Brings the window/screen forward first, same as
 * AmipClickGadget. */
BOOL AmipDragGadgetBy(struct Window *window, struct Gadget *gadget, WORD dx, WORD dy);

/* Drags srcGadget's current center onto destGadget's current center,
 * both resolved live at action time -- zero coordinates in the caller's
 * script, for drag-and-drop/reorder cases (e.g. dragging one listview
 * item onto another) where the destination is itself a gadget, not an
 * offset. Both gadgets are assumed to belong to `window` (dest resolved
 * against the same window as src -- see arexx_cmd.h's DRAG doc
 * comment). Brings the window/screen forward first, same as
 * AmipClickGadget. */
BOOL AmipDragGadgetToGadget(struct Window *window, struct Gadget *srcGadget, struct Gadget *destGadget);

/* Moves a WHOLE WINDOW by a pixel offset (dx, dy) -- a real drag of
 * its own title bar (WFLG_DRAGBAR), built on the exact same AmipDragAt()
 * primitive the gadget-drag functions above use, not a new mechanism.
 * The anchor point is the horizontal CENTER of the title bar, vertically
 * centered in the window's own BorderTop strip -- deliberately not an
 * attempt to locate the close/depth/zoom system gadgets' exact pixel
 * extents and dodge them precisely; for any window wider than roughly
 * 120px (effectively all real windows) the center point falls well
 * clear of them, an honest, documented heuristic, not a guessed one
 * masquerading as precise. Returns FALSE if `window` has no drag bar
 * at all (WFLG_DRAGBAR unset) without attempting anything. Brings the
 * window/screen forward first, same as AmipClickGadget. Query a
 * window's CURRENT position via TREE (its own `[left,top WxH]` header
 * line, `Window.left`/`Window.top` host-side) -- no separate "get
 * position" verb exists since TREE already carries it. */
BOOL AmipWindowMoveBy(struct Window *window, WORD dx, WORD dy);

/* Resizes a WHOLE WINDOW to an ABSOLUTE target (targetWidth,
 * targetHeight) -- a real drag of its own sizing gadget
 * (WFLG_SIZEGADGET) from its current bottom-right corner to wherever
 * that corner needs to land to reach the target size, built on the
 * same AmipDragAt() primitive. Returns FALSE if `window` has no
 * sizing gadget at all (WFLG_SIZEGADGET unset) without attempting
 * anything. Does NOT pre-check the target against the window's own
 * MinWidth/MinHeight/MaxWidth/MaxHeight (real, live fields on `struct
 * Window` -- readable directly if a caller wants to check first):
 * Intuition's own sizing logic clamps the drag exactly as it would a
 * genuine user drag, so the honest way to confirm the actual
 * resulting size is a follow-up TREE call, same "verify the real
 * outcome, don't assume the request was granted exactly" precedent
 * DRAG's own gadget forms already set. Brings the window/screen
 * forward first, same as AmipClickGadget. */
BOOL AmipWindowResizeTo(struct Window *window, WORD targetWidth, WORD targetHeight);

/* Types text as genuine IECLASS_RAWKEY press/release events into
 * whatever currently has keyboard focus (click/activate the target
 * first). Each printable character is inverted into its rawkey +
 * qualifier combination under the LIVE keymap via keymap.library's
 * MapANSI() -- self-adapting to non-US layouts, same technique as
 * ../amirfb's proven keyboard injection. '\n' maps to Return
 * (keymap-independent rawkey 0x44) directly.
 *
 * Pacing approximates human typing (~a few characters per second, a
 * real press duration on every key) rather than machine-speed
 * back-to-back events -- both so the target program's event loop sees
 * a realistic input stream, and because that's the input regime all
 * shipped software was actually tested against.
 *
 * Consumes the global KeymapBase (proto/keymap.h extern) the same way
 * the walker consumes GadToolsBase: the calling program owns opening/
 * closing keymap.library. Returns FALSE (typing nothing) if KeymapBase
 * is NULL, a character can't be generated under the active keymap, or
 * event injection fails. */
BOOL AmipTypeString(CONST_STRPTR text);

/* --- locators: resolving live structure at action time -----------------
 *
 * Deliberately walk window->FirstGadget / IntuitionBase->FirstScreen
 * directly rather than through intuition-model's copied-out
 * AmipWindowModel: an action target must resolve against the CURRENT
 * structure at the moment of the action, per the implementation plan's
 * "Act with real input, not shortcuts" design principle -- a stale copy
 * could click where a gadget used to be. Originally scoped to
 * server/src/clicktest/main.c; promoted here once the ARexx commodity
 * needed the exact same lookups, to avoid a second copy drifting.
 */

/* First window whose title contains titleSubstring, restricted to
 * screens whose DefaultTitle contains screenSubstring -- NOT the live
 * Title field, which tracks whichever window is currently active on
 * that screen (intuition.doc's SetWindowTitles: "the screen title
 * appears ... whenever this window is the active one") and so isn't a
 * stable screen identity; DefaultTitle is the app's own name for the
 * screen, set once at open time. screenSubstring NULL or "" means no
 * screen filter -- searches every screen, front-to-back, same as
 * before this parameter existed. NULL if no window matches. */
struct Window *AmipFindWindow(CONST_STRPTR screenSubstring, CONST_STRPTR titleSubstring);

/* First screen whose DefaultTitle contains screenSubstring, or the
 * frontmost/default screen (IntuitionBase->FirstScreen) if
 * screenSubstring is NULL or "" -- same substring-match and NULL-means-
 * "no filter" convention as AmipFindWindow's own screenSubstring.
 * NULL only if screenSubstring was given and nothing matched, or no
 * screen is open at all. */
struct Screen *AmipFindScreen(CONST_STRPTR screenSubstring);

/* First gadget in window's own list (window->FirstGadget) with a
 * matching GadgetID. NULL if none matches -- note this walks the classic
 * chain, so a BOOPSI window.class window only exposes its single
 * top-level layout.gadget here, not that layout's individual children
 * (see CLAUDE.md's "Confirmed limit"). */
struct Gadget *AmipFindGadgetById(struct Window *window, ULONG id);

/* Finds `window`'s own system gadget (close/depth/drag-bar/size) of
 * the given GTYP_SYSTYPEMASK sub-type (GTYP_CLOSE, GTYP_WDEPTH,
 * GTYP_WDRAGGING, GTYP_SIZING -- intuition/intuition.h), by walking
 * window->FirstGadget for a GTYP_SYSGADGET entry whose masked type
 * matches. NULL if that window has no such system gadget. The ONLY
 * correct way to re-resolve a specific system gadget back to a live
 * struct Gadget* -- every system gadget reports GA_ID 0, so
 * AmipFindGadgetById(window, 0) is ambiguous between them and simply
 * returns whichever one is first in Intuition's own chain, not
 * necessarily the one a caller actually means (confirmed live: this
 * is exactly what made CLICK's own ROLE=/INDEX= locator silently act
 * on the wrong system gadget -- server/src/amipilotserver/main.c's
 * ResolveTargetGadget(), and AmipGadgetModel's own sysGadgetType
 * field, both fixed alongside this). */
struct Gadget *AmipFindSystemGadget(struct Window *window, UWORD sysType);

/* Re-walks the live screen/window list under a brief LockIBase() hold,
 * checking pointer identity against target -- for the gap between
 * locating a window and acting on it (the window could have closed in
 * between). Doesn't close the gap entirely (it could still close in the
 * instant after this check returns), but shrinks it to essentially zero.
 * The real, general fix is "action-scoped expectations" (locate-and-act
 * atomic with respect to the target closing), planned but not yet
 * built -- see docs/implementation-plan.md. */
BOOL AmipIsWindowOpen(struct Window *target);

/* Locates the MenuItem at (menuNum, itemNum, subNum) in window's live
 * MenuStrip, walking NextMenu/NextItem by 0-based position -- the same
 * addressing intuition-model's AmipWalkMenuStrip() stamps onto its
 * model and Intuition itself reports via IDCMP_MENUPICK's MENUNUM()/
 * ITEMNUM()/SUBNUM() macros. subNum < 0 means "a top-level item, not
 * a submenu entry" (AmipMenuItemModel's own convention). Returns NULL
 * if window has no menu strip or any index is out of range. */
struct MenuItem *AmipFindMenuItem(struct Window *window, LONG menuNum, LONG itemNum, LONG subNum);

typedef enum {
    AMIP_MENUPICK_OK = 0,
    AMIP_MENUPICK_DISABLED,     /* item->Flags lacks ITEMENABLED */
    AMIP_MENUPICK_NO_SHORTCUT,  /* no COMMSEQ/Command, or Command isn't a
                                  * single keystroke under the active
                                  * keymap -- pointer-based menu
                                  * navigation isn't built yet (see
                                  * server/README.md) */
    AMIP_MENUPICK_INJECT_FAILED /* keymap.library unavailable, or
                                  * input.device event injection failed */
} AmipMenuPickResult;

/* Selects `item` via its keyboard shortcut: activates window, then
 * strikes the Command byte with the right-Amiga qualifier held, the
 * same input.device path a human pressing Right-Amiga+<key> would
 * produce -- Intuition itself resolves that combination against the
 * window's live MenuStrip, so this doesn't need to (and doesn't)
 * synthesize IDCMP_MENUPICK directly. Does NOT open the menu or move
 * the pointer -- pointer-based selection (for items without a
 * shortcut) is planned but not built (docs/implementation-plan.md,
 * "menu-pick"). */
AmipMenuPickResult AmipMenuPickByShortcut(struct Window *window, struct MenuItem *item);

#endif /* AMIPILOT_ACTION_ENGINE_H */
