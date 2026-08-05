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
 * the gadget's own LeftEdge/TopEdge/Width/Height, then calls
 * AmipClickAt() with AMIP_BUTTON_LEFT. Correct for classic/GadTools
 * gadgets and BOOPSI CUSTOMGADGET alike -- the latter via
 * GetAttr(GA_Left/GA_Top/GA_Width/GA_Height), with GFLG_RELWIDTH/
 * RELHEIGHT/RELRIGHT/RELBOTTOM resolved against the window's own size
 * (see this function's definition in action.c). */
BOOL AmipClickGadget(struct Window *window, struct Gadget *gadget);

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

/* First window (any screen) whose title contains titleSubstring. NULL if
 * none matches. */
struct Window *AmipFindWindow(CONST_STRPTR titleSubstring);

/* First gadget in window's own list (window->FirstGadget) with a
 * matching GadgetID. NULL if none matches -- note this walks the classic
 * chain, so a BOOPSI window.class window only exposes its single
 * top-level layout.gadget here, not that layout's individual children
 * (see CLAUDE.md's "Confirmed limit"). */
struct Gadget *AmipFindGadgetById(struct Window *window, ULONG id);

/* Re-walks the live screen/window list under a brief LockIBase() hold,
 * checking pointer identity against target -- for the gap between
 * locating a window and acting on it (the window could have closed in
 * between). Doesn't close the gap entirely (it could still close in the
 * instant after this check returns), but shrinks it to essentially zero.
 * The real, general fix is "action-scoped expectations" (locate-and-act
 * atomic with respect to the target closing), planned but not yet
 * built -- see docs/implementation-plan.md. */
BOOL AmipIsWindowOpen(struct Window *target);

#endif /* AMIPILOT_ACTION_ENGINE_H */
