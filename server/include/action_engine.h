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
 * AmipClickAt() with AMIP_BUTTON_LEFT. Only correct for classic/GadTools
 * gadgets so far (GTYP_BOOLGADGET/STRGADGET/PROPGADGET) -- BOOPSI
 * CUSTOMGADGET position needs GetAttr(GA_Left/GA_Top/...) the same way
 * intuition-model's walk.c learned GA_Text/GA_ID do, not yet implemented
 * here (see the TODO at this function's definition). */
BOOL AmipClickGadget(struct Window *window, struct Gadget *gadget);

#endif /* AMIPILOT_ACTION_ENGINE_H */
