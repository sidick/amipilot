/* action.c -- see action_engine.h for the design rationale. */

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/keymap.h>
#include <string.h>

#include "action_engine.h"

extern struct IntuitionBase *IntuitionBase;
/* Caller-owned, same convention as intuition-model's GadToolsBase: the
 * program using AmipTypeString() opens/closes keymap.library. */
extern struct Library *KeymapBase;

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

/* --- keyboard: AmipTypeString -------------------------------------------
 *
 * Technique from ../amirfb's proven keyboard injection: each printable
 * character goes through keymap.library's MapANSI(), which inverts it
 * into up to AMIP_MAX_PAIRS_PER_CHAR (rawkey, qualifier) pairs under the
 * ACTIVE keymap (dead-key prefixes included) -- self-adapting where a
 * fixed US-layout table would silently type the wrong characters.
 *
 * Qualifier handling, confirmed the hard way in amirfb: the modifier's
 * own rawkey press is NOT enough. Every injected event -- including the
 * main key's -- must carry the currently-held modifiers in ie_Qualifier,
 * because that field is how the OS knows Shift was down at the moment of
 * THIS keypress (console/keymap translation reads it); Shift-down then
 * 'e'-down with ie_Qualifier=0 types 'e', not 'E'. */

#define AMIP_MAX_PAIRS_PER_CHAR 3   /* MapANSI: 2 dead-key prefixes + final */

/* Keymap-independent rawkeys (devices/keymap.h layout, same values
 * amirfb verified against the NDK headers). */
#define AMIP_RAWKEY_RETURN 0x44
#define AMIP_RAWKEY_LSHIFT 0x60
#define AMIP_RAWKEY_RSHIFT 0x61
#define AMIP_RAWKEY_CTRL   0x63
#define AMIP_RAWKEY_LALT   0x64
#define AMIP_RAWKEY_RALT   0x65
#define AMIP_RAWKEY_LAMIGA 0x66
#define AMIP_RAWKEY_RAMIGA 0x67

/* Human-approximate pacing, in 1/50s ticks. ~2 ticks of key-down before
 * release and ~6 ticks between characters is roughly 6 characters/second
 * -- brisk-but-human typing. Deliberately not machine-speed: the input
 * regime every shipped Amiga program was actually tested against is a
 * human at a keyboard, and Simon asked for this explicitly. */
#define AMIP_TYPE_HOLD_TICKS  2
#define AMIP_TYPE_INTER_TICKS 6

static const struct {
    UWORD bit;
    UBYTE rawkey;
} g_qualifierKeys[] = {
    { IEQUALIFIER_LSHIFT,   AMIP_RAWKEY_LSHIFT },
    { IEQUALIFIER_RSHIFT,   AMIP_RAWKEY_RSHIFT },
    { IEQUALIFIER_CONTROL,  AMIP_RAWKEY_CTRL   },
    { IEQUALIFIER_LALT,     AMIP_RAWKEY_LALT   },
    { IEQUALIFIER_RALT,     AMIP_RAWKEY_RALT   },
    { IEQUALIFIER_LCOMMAND, AMIP_RAWKEY_LAMIGA },
    { IEQUALIFIER_RCOMMAND, AMIP_RAWKEY_RAMIGA },
};
#define AMIP_NUM_QUALIFIER_KEYS \
    (sizeof(g_qualifierKeys) / sizeof(g_qualifierKeys[0]))

static BOOL SendRawKey(UBYTE code, BOOL up, UWORD heldQualifiers)
{
    struct InputEvent ie;

    memset(&ie, 0, sizeof(ie));
    ie.ie_Class     = IECLASS_RAWKEY;
    ie.ie_Code      = up ? (UWORD)(code | IECODE_UP_PREFIX) : code;
    ie.ie_Qualifier = heldQualifiers;

    return WriteInputEvent(&ie);
}

/* One full keystroke: press the needed modifier keys, press+release the
 * main key (with the modifiers reflected in its ie_Qualifier), release
 * the modifiers again. Synchronous and self-contained per keystroke --
 * unlike amirfb there's no concurrent client holding keys across calls,
 * so no refcounting is needed. */
static BOOL StrikeKey(UBYTE code, UWORD qualifier)
{
    UWORD held = 0;
    size_t i;
    BOOL ok = TRUE;

    for (i = 0; i < AMIP_NUM_QUALIFIER_KEYS; i++) {
        if (qualifier & g_qualifierKeys[i].bit) {
            held |= g_qualifierKeys[i].bit;
            if (!SendRawKey(g_qualifierKeys[i].rawkey, FALSE, held)) {
                ok = FALSE;
            }
        }
    }

    if (ok) {
        ok = SendRawKey(code, FALSE, held);
        Delay(AMIP_TYPE_HOLD_TICKS);
        if (!SendRawKey(code, TRUE, held)) {
            ok = FALSE;
        }
    }

    for (i = AMIP_NUM_QUALIFIER_KEYS; i-- > 0;) {
        if (qualifier & g_qualifierKeys[i].bit) {
            held &= (UWORD)~g_qualifierKeys[i].bit;
            if (!SendRawKey(g_qualifierKeys[i].rawkey, TRUE, held)) {
                ok = FALSE;
            }
        }
    }

    return ok;
}

BOOL AmipTypeString(CONST_STRPTR text)
{
    const UBYTE *ch;

    if (text == NULL || g_inputReq == NULL || KeymapBase == NULL) {
        return FALSE;
    }

    for (ch = (const UBYTE *)text; *ch != '\0'; ch++) {
        if (*ch == '\n') {
            if (!StrikeKey(AMIP_RAWKEY_RETURN, 0)) {
                return FALSE;
            }
        } else {
            UBYTE pairs[AMIP_MAX_PAIRS_PER_CHAR * 2];
            LONG n, p;

            n = MapANSI((CONST_STRPTR)ch, 1, (STRPTR)pairs, sizeof(pairs) / 2, NULL);
            if (n <= 0) {
                /* Not generatable under the active keymap -- fail loudly
                 * rather than silently typing a truncated string. */
                return FALSE;
            }
            if (n > AMIP_MAX_PAIRS_PER_CHAR) {
                n = AMIP_MAX_PAIRS_PER_CHAR;
            }
            for (p = 0; p < n; p++) {
                if (!StrikeKey(pairs[p * 2], pairs[p * 2 + 1])) {
                    return FALSE;
                }
            }
        }
        Delay(AMIP_TYPE_INTER_TICKS);
    }

    return TRUE;
}

struct MenuItem *AmipFindMenuItem(struct Window *window, LONG menuNum, LONG itemNum, LONG subNum)
{
    struct Menu *menu;
    struct MenuItem *item;
    LONG i;

    if (window == NULL || menuNum < 0 || itemNum < 0) {
        return NULL;
    }

    menu = window->MenuStrip;
    for (i = 0; i < menuNum && menu != NULL; i++) {
        menu = menu->NextMenu;
    }
    if (menu == NULL) {
        return NULL;
    }

    item = menu->FirstItem;
    for (i = 0; i < itemNum && item != NULL; i++) {
        item = item->NextItem;
    }
    if (item == NULL || subNum < 0) {
        return item;
    }

    item = item->SubItem;
    for (i = 0; i < subNum && item != NULL; i++) {
        item = item->NextItem;
    }
    return item;
}

AmipMenuPickResult AmipMenuPickByShortcut(struct Window *window, struct MenuItem *item)
{
    UBYTE ch;
    UBYTE pairs[AMIP_MAX_PAIRS_PER_CHAR * 2];
    LONG n;

    if (window == NULL || item == NULL) {
        return AMIP_MENUPICK_NO_SHORTCUT;
    }
    if (!(item->Flags & ITEMENABLED)) {
        return AMIP_MENUPICK_DISABLED;
    }
    if (!(item->Flags & COMMSEQ) || item->Command == 0) {
        return AMIP_MENUPICK_NO_SHORTCUT;
    }
    if (KeymapBase == NULL) {
        return AMIP_MENUPICK_INJECT_FAILED;
    }

    /* Same "bring the target forward first" rationale as AmipClickGadget
     * -- a keystroke only reaches the window that's actually active. */
    ScreenToFront(window->WScreen);
    WindowToFront(window);
    ActivateWindow(window);

    /* MenuItem->Command is a plain ASCII char (intuition.h), inverted
     * into its rawkey + qualifier under the LIVE keymap the same way
     * AmipTypeString does per character -- self-adapting to non-US
     * layouts rather than assuming a fixed US rawkey table. */
    ch = (UBYTE)item->Command;
    n = MapANSI((CONST_STRPTR)&ch, 1, (STRPTR)pairs, sizeof(pairs) / 2, NULL);
    if (n != 1) {
        /* A shortcut char needing a dead-key sequence (or otherwise not
         * a single keystroke under the active keymap) isn't the case
         * classic Amiga menu shortcuts are meant to produce -- fail
         * loudly rather than guess which pair of the sequence is "the"
         * keystroke. */
        return AMIP_MENUPICK_INJECT_FAILED;
    }

    /* Right-Amiga is the conventional menu-shortcut modifier (the same
     * key the Amiga-key glyph next to a menu's shortcut letter refers
     * to) -- OR it into the keymap-resolved qualifier rather than
     * replacing it, so a shortcut that itself needs Shift under the
     * active keymap still gets it. Intuition resolves the combination
     * against window's live MenuStrip on its own; this doesn't
     * synthesize IDCMP_MENUPICK directly. */
    if (!StrikeKey(pairs[0], (UWORD)(pairs[1] | IEQUALIFIER_RCOMMAND))) {
        return AMIP_MENUPICK_INJECT_FAILED;
    }
    return AMIP_MENUPICK_OK;
}

/* GFLG_RELWIDTH/RELHEIGHT/RELRIGHT/RELBOTTOM (intuition.h): a classic,
 * pre-BOOPSI Intuition convention where the corresponding Width/Height/
 * LeftEdge/TopEdge is stored as a NEGATIVE offset from the window's own
 * dimension, not an absolute value. Confirmed against
 * fixtures/classact-app: its root layout.gadget (the only child a
 * window.class window attaches to window->FirstGadget -- see CLAUDE.md's
 * "Confirmed limit") answers GA_Width/GA_Height via GetAttr with exactly
 * this negative-relative encoding (GFLG_RELWIDTH|GFLG_RELHEIGHT both
 * set), not a broken value. Same resolution as intuition-model/walk.c's
 * ResolveGadgetGeometry -- duplicated rather than shared since server/
 * doesn't link intuition-model. */
static void ResolveGadgetGeometry(struct Window *window, UWORD flags,
                                   WORD *left, WORD *top, WORD *width, WORD *height)
{
    if (flags & GFLG_RELWIDTH) {
        *width = (WORD)(window->Width + *width);
    }
    if (flags & GFLG_RELHEIGHT) {
        *height = (WORD)(window->Height + *height);
    }
    if (flags & GFLG_RELRIGHT) {
        *left = (WORD)(window->Width + *left);
    }
    if (flags & GFLG_RELBOTTOM) {
        *top = (WORD)(window->Height + *top);
    }
}

BOOL AmipGadgetCenter(struct Window *window, struct Gadget *gadget, WORD *xOut, WORD *yOut)
{
    WORD gadLeft, gadTop, gadWidth, gadHeight;

    if (window == NULL || gadget == NULL || xOut == NULL || yOut == NULL) {
        return FALSE;
    }

    /* BOOPSI CUSTOMGADGET position isn't necessarily mirrored into the
     * classic LeftEdge/TopEdge/Width/Height fields -- intuition-model's
     * walk.c already had to learn this the hard way for GA_Text/GA_ID
     * (confirmed against fixtures/classact-app: its layout.gadget entry
     * read back with a nonsensical negative width/height via the classic
     * fields). GA_Left/GA_Top/GA_Width/GA_Height (gadgetclass.h, all
     * LONG) are window-relative the same as the classic fields -- GA_Left
     * is documented as "relative to the left edge of the window" -- so
     * this doesn't change the coordinate convention below, only where the
     * numbers come from. Same GetAttr()-only-if-CUSTOMGADGET safety walk.c
     * relies on: calling it on a classic gadget would read garbage since
     * there's no real _Object/class header there. Falls back to the
     * classic fields per-attribute if a particular one isn't answered. */
    if ((gadget->GadgetType & GTYP_GTYPEMASK) == GTYP_CUSTOMGADGET) {
        ULONG left = 0, top = 0, width = 0, height = 0;

        gadLeft   = GetAttr(GA_Left, gadget, &left)     ? (WORD)left   : gadget->LeftEdge;
        gadTop    = GetAttr(GA_Top, gadget, &top)       ? (WORD)top    : gadget->TopEdge;
        gadWidth  = GetAttr(GA_Width, gadget, &width)   ? (WORD)width  : gadget->Width;
        gadHeight = GetAttr(GA_Height, gadget, &height) ? (WORD)height : gadget->Height;
    } else {
        gadLeft   = gadget->LeftEdge;
        gadTop    = gadget->TopEdge;
        gadWidth  = gadget->Width;
        gadHeight = gadget->Height;
    }

    ResolveGadgetGeometry(window, gadget->Flags, &gadLeft, &gadTop, &gadWidth, &gadHeight);

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
    *xOut = window->LeftEdge + gadLeft + gadWidth / 2;
    *yOut = window->TopEdge + gadTop + gadHeight / 2;
    return TRUE;
}

/* A click/drag has to actually reach the target screen/window to mean
 * anything -- input events only interact with whatever is frontmost.
 * docs/implementation-plan.md's architecture diagram already scopes
 * focus to the action engine, not the caller: bring both forward
 * unconditionally rather than leaving it to whoever calls this to
 * remember. Shared by AmipClickGadget and the drag entry points below. */
static void BringWindowForward(struct Window *window)
{
    ScreenToFront(window->WScreen);
    WindowToFront(window);
    ActivateWindow(window);
}

BOOL AmipClickGadget(struct Window *window, struct Gadget *gadget)
{
    WORD centerX, centerY;

    if (window == NULL || gadget == NULL) {
        return FALSE;
    }

    BringWindowForward(window);

    if (!AmipGadgetCenter(window, gadget, &centerX, &centerY)) {
        return FALSE;
    }
    return AmipClickAt(window->WScreen, centerX, centerY, AMIP_BUTTON_LEFT);
}

BOOL AmipDragAt(struct Screen *screen, WORD x1, WORD y1, WORD x2, WORD y2)
{
    if (!AmipMoveMouseTo(screen, x1, y1)) {
        return FALSE;
    }
    if (!SendRawMouseButton(IEQUALIFIER_LEFTBUTTON, IECODE_LBUTTON)) {
        return FALSE;
    }
    /* A real drag has non-zero press duration before motion starts. */
    Delay(3);

    if (!AmipMoveMouseTo(screen, x2, y2)) {
        /* Best-effort release even on failure -- don't leave Intuition
         * thinking the button is still held down. */
        SendRawMouseButton(0, (UWORD)(IECODE_LBUTTON | IECODE_UP_PREFIX));
        return FALSE;
    }
    Delay(3);
    return SendRawMouseButton(0, (UWORD)(IECODE_LBUTTON | IECODE_UP_PREFIX));
}

BOOL AmipDragGadgetBy(struct Window *window, struct Gadget *gadget, WORD dx, WORD dy)
{
    WORD centerX, centerY;

    if (window == NULL || gadget == NULL) {
        return FALSE;
    }

    BringWindowForward(window);

    if (!AmipGadgetCenter(window, gadget, &centerX, &centerY)) {
        return FALSE;
    }
    return AmipDragAt(window->WScreen, centerX, centerY,
                       (WORD)(centerX + dx), (WORD)(centerY + dy));
}

BOOL AmipDragGadgetToGadget(struct Window *window, struct Gadget *srcGadget, struct Gadget *destGadget)
{
    WORD srcX, srcY, destX, destY;

    if (window == NULL || srcGadget == NULL || destGadget == NULL) {
        return FALSE;
    }

    BringWindowForward(window);

    if (!AmipGadgetCenter(window, srcGadget, &srcX, &srcY)) {
        return FALSE;
    }
    if (!AmipGadgetCenter(window, destGadget, &destX, &destY)) {
        return FALSE;
    }
    return AmipDragAt(window->WScreen, srcX, srcY, destX, destY);
}

/* See action_engine.h's "locators" section for the design rationale --
 * moved here from server/src/clicktest/main.c once the ARexx commodity
 * needed the exact same lookups. */

struct Window *AmipFindWindow(CONST_STRPTR screenSubstring, CONST_STRPTR titleSubstring)
{
    struct Screen *screen;
    struct Window *window;
    BOOL wantScreenFilter = (screenSubstring != NULL && screenSubstring[0] != '\0');

    if (IntuitionBase == NULL) {
        return NULL;
    }

    for (screen = IntuitionBase->FirstScreen; screen != NULL; screen = screen->NextScreen) {
        if (wantScreenFilter
            && (screen->DefaultTitle == NULL
                || strstr((const char *)screen->DefaultTitle, (const char *)screenSubstring) == NULL)) {
            continue;
        }
        for (window = screen->FirstWindow; window != NULL; window = window->NextWindow) {
            if (window->Title != NULL
                && strstr((const char *)window->Title, (const char *)titleSubstring) != NULL) {
                return window;
            }
        }
    }

    return NULL;
}

struct Screen *AmipFindScreen(CONST_STRPTR screenSubstring)
{
    struct Screen *screen;
    BOOL wantFilter = (screenSubstring != NULL && screenSubstring[0] != '\0');

    if (IntuitionBase == NULL) {
        return NULL;
    }
    if (!wantFilter) {
        return IntuitionBase->FirstScreen;
    }
    for (screen = IntuitionBase->FirstScreen; screen != NULL; screen = screen->NextScreen) {
        if (screen->DefaultTitle != NULL
            && strstr((const char *)screen->DefaultTitle, (const char *)screenSubstring) != NULL) {
            return screen;
        }
    }
    return NULL;
}

struct Gadget *AmipFindGadgetById(struct Window *window, ULONG id)
{
    struct Gadget *gadget;

    if (window == NULL) {
        return NULL;
    }

    for (gadget = window->FirstGadget; gadget != NULL; gadget = gadget->NextGadget) {
        if (gadget->GadgetID == id) {
            return gadget;
        }
    }

    return NULL;
}

BOOL AmipIsWindowOpen(struct Window *target)
{
    struct Screen *screen;
    struct Window *window;
    BOOL found = FALSE;

    if (IntuitionBase == NULL || target == NULL) {
        return FALSE;
    }

    LockIBase(0);
    for (screen = IntuitionBase->FirstScreen; screen != NULL && !found; screen = screen->NextScreen) {
        for (window = screen->FirstWindow; window != NULL; window = window->NextWindow) {
            if (window == target) {
                found = TRUE;
                break;
            }
        }
    }
    UnlockIBase(0);

    return found;
}
