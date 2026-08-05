/* action.c -- see action_engine.h for the design rationale. */

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <string.h>

#include "action_engine.h"

extern struct IntuitionBase *IntuitionBase;

static struct MsgPort *g_inputPort;
static struct IOStdReq *g_inputReq;

BOOL AmipActionInit(void)
{
    if (IntuitionBase == NULL) {
        return FALSE;
    }

    g_inputPort = CreateMsgPort();
    if (g_inputPort == NULL) {
        return FALSE;
    }

    g_inputReq = (struct IOStdReq *)CreateIORequest(g_inputPort, sizeof(struct IOStdReq));
    if (g_inputReq == NULL) {
        DeleteMsgPort(g_inputPort);
        g_inputPort = NULL;
        return FALSE;
    }

    if (OpenDevice((CONST_STRPTR)"input.device", 0, (struct IORequest *)g_inputReq, 0) != 0) {
        DeleteIORequest((struct IORequest *)g_inputReq);
        DeleteMsgPort(g_inputPort);
        g_inputReq = NULL;
        g_inputPort = NULL;
        return FALSE;
    }

    return TRUE;
}

void AmipActionShutdown(void)
{
    if (g_inputReq != NULL) {
        CloseDevice((struct IORequest *)g_inputReq);
        DeleteIORequest((struct IORequest *)g_inputReq);
        g_inputReq = NULL;
    }
    if (g_inputPort != NULL) {
        DeleteMsgPort(g_inputPort);
        g_inputPort = NULL;
    }
}

/* Shared IND_WRITEEVENT submission. ie_TimeStamp is deliberately left
 * zeroed: the input.device autodoc is explicit that IND_WRITEEVENT
 * itself sets it (V36+), and this project's floor is V37. */
static BOOL WriteInputEvent(struct InputEvent *ie)
{
    if (g_inputReq == NULL) {
        return FALSE;
    }

    g_inputReq->io_Command = IND_WRITEEVENT;
    g_inputReq->io_Length  = sizeof(*ie);
    g_inputReq->io_Data    = (APTR)ie;
    DoIO((struct IORequest *)g_inputReq);

    return g_inputReq->io_Error == 0;
}

/* A synthetic button transition, as IECLASS_RAWMOUSE with zero motion.
 *
 * IEQUALIFIER_RELATIVEMOUSE is load-bearing: the gameport handler sets
 * it on every real mouse event, marking ie_X/ie_Y as deltas. Without it
 * a synthetic RAWMOUSE event's zeroed position fields read as absolute
 * (0,0) -- every click landed at the screen's top-left corner (the
 * title bar) no matter where the pointer visibly was, which cost most
 * of a day's debugging (2026-08-05) before the fixture's
 * IDCMP_MOUSEBUTTONS diagnostic finally showed clicks arriving with
 * this qualifier reaching the right window-relative coordinates.
 *
 * ie_Qualifier also reflects the FULL current button state as of this
 * event, not just the ie_Code transition -- same convention as
 * ../amirfb's proven injection (on_pointer()): a down event's qualifier
 * carries that button's bit, an up event's does not. */
static BOOL SendRawMouseButton(UWORD buttonQualifier, UWORD code)
{
    struct InputEvent ie;

    memset(&ie, 0, sizeof(ie));
    ie.ie_Class     = IECLASS_RAWMOUSE;
    ie.ie_Qualifier = (UWORD)(buttonQualifier | IEQUALIFIER_RELATIVEMOUSE);
    ie.ie_Code      = code;

    return WriteInputEvent(&ie);
}

/* Absolute pointer positioning via IECLASS_NEWPOINTERPOS /
 * IESUBCLASS_PIXEL (struct IEPointerPixel naming a screen + pixel
 * coordinates in it), exactly as the RKM's documented
 * Devices/Dev_examples/Set_Mouse.c does. IECLASS_RAWMOUSE motion is
 * relative-delta only; there is no absolute RAWMOUSE form.
 *
 * Verified in isolation via the AmiSetMouse diagnostic (2026-08-05,
 * Copperline, 640x256 hires screen): requesting pixel [100,200] reads
 * back IntuitionBase->MouseX/MouseY = [100,400] -- MouseY is in
 * double-resolution units on a hires screen (exactly 2x the pixel Y),
 * NOT a positioning error. Trust the documented pixel-coordinate
 * request; don't poll MouseY expecting it to equal the pixel Y. */
static BOOL SendPointerPixel(struct Screen *screen, WORD x, WORD y)
{
    struct InputEvent ie;
    struct IEPointerPixel pix;

    if (screen == NULL) {
        return FALSE;
    }

    pix.iepp_Screen     = screen;
    pix.iepp_Position.X = x;
    pix.iepp_Position.Y = y;

    memset(&ie, 0, sizeof(ie));
    ie.ie_Class        = IECLASS_NEWPOINTERPOS;
    ie.ie_SubClass     = IESUBCLASS_PIXEL;
    ie.ie_Code         = IECODE_NOBUTTON;
    ie.ie_EventAddress = (APTR)&pix;

    return WriteInputEvent(&ie);
}

BOOL AmipMoveMouseTo(struct Screen *screen, WORD x, WORD y)
{
    if (IntuitionBase == NULL || g_inputReq == NULL || screen == NULL) {
        return FALSE;
    }

    if (!SendPointerPixel(screen, x, y)) {
        return FALSE;
    }

    /* DoIO() completing only confirms input.device queued the event, not
     * that Intuition's input-handler task has consumed it -- give it a
     * tick before anything that depends on the new position. */
    Delay(1);
    return TRUE;
}

BOOL AmipClickAt(struct Screen *screen, WORD x, WORD y, AmipMouseButton button)
{
    UWORD downCode, buttonQualifier;

    if (!AmipMoveMouseTo(screen, x, y)) {
        return FALSE;
    }

    switch (button) {
        case AMIP_BUTTON_MIDDLE:
            downCode = IECODE_MBUTTON;
            buttonQualifier = IEQUALIFIER_MIDBUTTON;
            break;
        case AMIP_BUTTON_RIGHT:
            downCode = IECODE_RBUTTON;
            buttonQualifier = IEQUALIFIER_RBUTTON;
            break;
        case AMIP_BUTTON_LEFT:
        default:
            downCode = IECODE_LBUTTON;
            buttonQualifier = IEQUALIFIER_LEFTBUTTON;
            break;
    }

    if (!SendRawMouseButton(buttonQualifier, downCode)) {
        return FALSE;
    }
    /* A real click has non-zero press duration. */
    Delay(3);
    return SendRawMouseButton(0, (UWORD)(downCode | IECODE_UP_PREFIX));
}

/* TODO: BOOPSI CUSTOMGADGET position isn't necessarily mirrored into the
 * classic LeftEdge/TopEdge/Width/Height fields -- intuition-model's
 * walk.c already had to learn this the hard way for GA_Text/GA_ID
 * (confirmed against fixtures/classact-app: its layout.gadget entry read
 * back with a nonsensical negative width/height via the classic fields).
 * This function hasn't been verified against a BOOPSI gadget yet; only
 * trust it for classic/GadTools gadgets (GTYP_BOOLGADGET/STRGADGET/
 * PROPGADGET) until GA_Left/GA_Top/GA_Width/GA_Height are read via
 * GetAttr() for GTYP_CUSTOMGADGET the same way. */
BOOL AmipClickGadget(struct Window *window, struct Gadget *gadget)
{
    WORD centerX, centerY;

    if (window == NULL || gadget == NULL) {
        return FALSE;
    }

    /* A click has to actually reach the target screen/window to mean
     * anything -- input events only interact with whatever is frontmost.
     * docs/implementation-plan.md's architecture diagram already scopes
     * focus to the action engine, not the caller: bring both forward
     * unconditionally rather than leaving it to whoever calls
     * AmipClickGadget to remember. */
    ScreenToFront(window->WScreen);
    WindowToFront(window);
    ActivateWindow(window);

    /* Gadget LeftEdge/TopEdge are relative to the window's own top-left
     * corner INCLUDING the border/title-bar area -- do NOT also add
     * BorderLeft/BorderTop. Proven via the fixture's IDCMP_MOUSEBUTTONS
     * diagnostic (2026-08-05): with borders wrongly added, the click
     * reported back at window-relative [74,42] against a button spanning
     * [20..120, 24..38] -- exactly BorderLeft/BorderTop past the center,
     * 4px below the gadget.
     *
     * IEPointerPixel coordinates are screen-relative; window LeftEdge/
     * TopEdge are also screen-relative, so no screen offset needed. */
    centerX = window->LeftEdge + gadget->LeftEdge + gadget->Width / 2;
    centerY = window->TopEdge + gadget->TopEdge + gadget->Height / 2;

    return AmipClickAt(window->WScreen, centerX, centerY, AMIP_BUTTON_LEFT);
}
