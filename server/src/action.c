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

struct MenuItem *AmipFindMenuItemWithParents(struct Window *window,
                                             LONG menuNum, LONG itemNum, LONG subNum,
                                             struct Menu **menuOut,
                                             struct MenuItem **parentItemOut)
{
    struct Menu *menu;
    struct MenuItem *item;
    LONG i;

    if (menuOut != NULL) *menuOut = NULL;
    if (parentItemOut != NULL) *parentItemOut = NULL;

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
    if (menuOut != NULL) *menuOut = menu;

    item = menu->FirstItem;
    for (i = 0; i < itemNum && item != NULL; i++) {
        item = item->NextItem;
    }
    if (item == NULL || subNum < 0) {
        return item;
    }
    if (parentItemOut != NULL) *parentItemOut = item;

    item = item->SubItem;
    for (i = 0; i < subNum && item != NULL; i++) {
        item = item->NextItem;
    }
    return item;
}

struct MenuItem *AmipFindMenuItem(struct Window *window, LONG menuNum, LONG itemNum, LONG subNum)
{
    return AmipFindMenuItemWithParents(window, menuNum, itemNum, subNum, NULL, NULL);
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

BOOL AmipClickWindowRelative(struct Window *window, WORD x, WORD y, WORD w, WORD h)
{
    if (window == NULL) {
        return FALSE;
    }

    BringWindowForward(window);

    /* Same window-relative-including-borders convention as
     * AmipGadgetCenter() above (see its comment) -- the geometry here
     * comes from a WHERE port's own GetAttr(GA_Left/GA_Top/GA_Width/
     * GA_Height) reply (manifest/SPEC.md's "The cooperative geometry
     * port" section) rather than a live struct Gadget*, but the
     * conversion to a screen-relative click point is identical: add
     * the window's own LeftEdge/TopEdge, nothing else. */
    return AmipClickAt(window->WScreen,
                        (WORD)(window->LeftEdge + x + w / 2),
                        (WORD)(window->TopEdge + y + h / 2),
                        AMIP_BUTTON_LEFT);
}

/* Resolves a menu item's current screen-absolute box, read directly off
 * the live struct Menu/MenuItem fields -- never cached, since menu
 * layout depends on the user's own screen/menu font (issue #63).
 *
 * Top-level item (parentItem == NULL): Menu->LeftEdge is documented
 * screen-absolute (the screen's own LeftEdge plus its left border,
 * already folded in -- RKRM Menu Data Structures). MenuItem->LeftEdge/
 * Width/Height for a top-level item are relative to Menu->LeftEdge.
 * MenuItem->TopEdge is documented as relative to "the topmost position
 * Intuition allows", resolved here against the screen's own live,
 * font-derived BarHeight field (intuition/screens.h: "Bar sizes for
 * this Screen... BarHeight is one less than the actual menu bar
 * height" -- genuinely adapts to the user's screen font, unlike any
 * fixed pixel constant).
 *
 * Sub-item (parentItem != NULL, the resolved TOP-LEVEL item it hangs
 * off of): RKRM gives no formula for this, only that the sub-item box
 * "overlaps the parent item's own select box somewhere" -- Intuition
 * places it itself to avoid clipping off-screen. STARTING HYPOTHESIS,
 * NOT YET CONFIRMED ON TARGET: anchor at the parent's own resolved
 * absolute box (its right edge, its own top), then add the sub-item's
 * own LeftEdge/TopEdge as an offset from that anchor -- the same "read
 * the live fields relative to a known reference point" shape as the
 * top-level case. Verify live under Copperline against a real submenu
 * item (tests/copperline/menu-test.py's MENUPICK-SUBITEM-POINTER
 * check) before trusting this -- this project's own house convention
 * is real functions verified against a real fixture, not guessed
 * heuristics (CLAUDE.md). Update this comment with whatever's actually
 * confirmed. */
static BOOL ResolveMenuItemBox(struct Window *window, struct Menu *menu,
                                struct MenuItem *parentItem, struct MenuItem *item,
                                WORD *leftOut, WORD *topOut, WORD *widthOut, WORD *heightOut)
{
    struct Screen *screen;

    if (window == NULL || menu == NULL || item == NULL) {
        return FALSE;
    }
    screen = window->WScreen;
    if (screen == NULL) {
        return FALSE;
    }

    if (parentItem == NULL) {
        *leftOut   = (WORD)(menu->LeftEdge + item->LeftEdge);
        *topOut    = (WORD)(screen->TopEdge + screen->BarHeight + 1 + item->TopEdge);
        *widthOut  = item->Width;
        *heightOut = item->Height;
    } else {
        WORD parentLeft, parentTop, parentWidth, parentHeight;

        if (!ResolveMenuItemBox(window, menu, NULL, parentItem,
                                &parentLeft, &parentTop, &parentWidth, &parentHeight)) {
            return FALSE;
        }
        /* Confirmed live (2026-08-09), via a real screenshot captured
         * mid-pick and measured pixel-for-pixel against
         * fixtures/gadtools-app's real "More" submenu: the submenu's
         * own box left-anchors flush against the parent item's right
         * edge (parentLeft + parentWidth) -- item->LeftEdge plays NO
         * part in X placement for a sub-item and must NOT be added on
         * top of that (doing so overshot the box entirely, landing
         * clicks well past its right edge). item->TopEdge, by
         * contrast, genuinely is each sub-item's own stacking offset
         * from the submenu box's top (== the parent item's own
         * resolved top) -- confirmed matching the visually measured
         * row for a second-position sub-item. */
        *leftOut   = (WORD)(parentLeft + parentWidth);
        *topOut    = (WORD)(parentTop + item->TopEdge);
        *widthOut  = item->Width;
        *heightOut = item->Height;
    }

    return (*widthOut > 0 && *heightOut > 0) ? TRUE : FALSE;
}

/* ~500ms: time for the menu strip (or one-level submenu) to actually
 * render and for Intuition's own input-handler task to catch up with
 * the synthesized pointer position, before the next move/release
 * depends on it -- confirmed live (2026-08-09) sufficient under
 * Copperline for both the top-level pulldown and a one-level submenu
 * to open reliably; same "empirically tuned, not guessed" precedent
 * as AmipDragAt's own Delay(3) below. */
#define AMIP_MENU_OPEN_TICKS    25
#define AMIP_MENU_SUBOPEN_TICKS 25

AmipMenuPickResult AmipMenuPickByPointer(struct Window *window, struct Menu *menu,
                                         struct MenuItem *topItem, struct MenuItem *subItem)
{
    struct MenuItem *target = (subItem != NULL) ? subItem : topItem;
    WORD left, top, width, height, x, y;

    if (window == NULL || menu == NULL || topItem == NULL || target == NULL) {
        return AMIP_MENUPICK_GEOMETRY_FAILED;
    }
    if (!(target->Flags & ITEMENABLED)) {
        return AMIP_MENUPICK_DISABLED;
    }
    /* A window that traps the right mouse button opts entirely out of
     * Intuition's own menu-button handling -- no synthesized RMB-down
     * can ever open its menu strip, a real permanent limit for this
     * window, not a transient injection failure. */
    if (window->Flags & WFLG_RMBTRAP) {
        return AMIP_MENUPICK_RMB_TRAPPED;
    }

    /* Same "bring the target forward first" rationale as
     * AmipClickGadget -- input only reaches whatever's frontmost. */
    BringWindowForward(window);

    if (!SendRawMouseButton(IEQUALIFIER_RBUTTON, IECODE_RBUTTON)) {
        return AMIP_MENUPICK_INJECT_FAILED;
    }
    /* RMB-down alone only switches the screen's own title bar into
     * menu mode (showing menu titles instead of the window title) --
     * confirmed live (2026-08-09) that the pulldown itself does NOT
     * open until the pointer is actually moved over the target menu's
     * own title text in that bar. Move there first: Menu->LeftEdge is
     * documented screen-absolute (see ResolveMenuItemBox's own doc
     * comment); the title row is the screen's own live BarHeight, not
     * a guessed pixel constant. */
    if (!AmipMoveMouseTo(window->WScreen,
                         (WORD)(menu->LeftEdge + 8),
                         (WORD)(window->WScreen->TopEdge + window->WScreen->BarHeight / 2))) {
        SendRawMouseButton(0, (UWORD)(IECODE_RBUTTON | IECODE_UP_PREFIX));
        return AMIP_MENUPICK_INJECT_FAILED;
    }
    Delay(AMIP_MENU_OPEN_TICKS);

    if (!ResolveMenuItemBox(window, menu, NULL, topItem, &left, &top, &width, &height)) {
        SendRawMouseButton(0, (UWORD)(IECODE_RBUTTON | IECODE_UP_PREFIX));
        return AMIP_MENUPICK_GEOMETRY_FAILED;
    }
    x = (WORD)(left + width / 2);
    y = (WORD)(top + height / 2);
    if (!AmipMoveMouseTo(window->WScreen, x, y)) {
        SendRawMouseButton(0, (UWORD)(IECODE_RBUTTON | IECODE_UP_PREFIX));
        return AMIP_MENUPICK_INJECT_FAILED;
    }
    /* Highlights topItem and, if it has sub-items, auto-opens the
     * one-level submenu -- purely Intuition-internal reactions to
     * pointer position while RMB is held; no separate event exists to
     * synthesize for either. */
    Delay(AMIP_MENU_OPEN_TICKS);

    if (subItem != NULL) {
        if (!ResolveMenuItemBox(window, menu, topItem, subItem, &left, &top, &width, &height)) {
            SendRawMouseButton(0, (UWORD)(IECODE_RBUTTON | IECODE_UP_PREFIX));
            return AMIP_MENUPICK_GEOMETRY_FAILED;
        }
        x = (WORD)(left + width / 2);
        y = (WORD)(top + height / 2);
        if (!AmipMoveMouseTo(window->WScreen, x, y)) {
            SendRawMouseButton(0, (UWORD)(IECODE_RBUTTON | IECODE_UP_PREFIX));
            return AMIP_MENUPICK_INJECT_FAILED;
        }
        Delay(AMIP_MENU_SUBOPEN_TICKS);
    }

    /* Release over the target -- this is literally what Intuition
     * turns into IDCMP_MENUPICK (only the most-subordinate item under
     * the pointer is selectable, per RKRM). Best-effort even on
     * failure paths above already released; this final one is the
     * real commit. */
    if (!SendRawMouseButton(0, (UWORD)(IECODE_RBUTTON | IECODE_UP_PREFIX))) {
        return AMIP_MENUPICK_INJECT_FAILED;
    }
    return AMIP_MENUPICK_OK;
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

/* Window system gadgets (drag bar, sizing gadget, close/depth/zoom) are
 * REAL struct Gadget entries Intuition links into the window's own
 * FirstGadget chain at OpenWindow() time, flagged GTYP_SYSGADGET with a
 * GTYP_SYSTYPEMASK sub-type (GTYP_WDRAGGING, GTYP_SIZING, ...) --
 * documented in intuition/intuition.h, not a private/undocumented
 * struct. Finding the real gadget and reusing AmipGadgetCenter()'s
 * already-correct REL-flag-aware geometry resolution is the honest way
 * to get its exact clickable center -- confirmed necessary live: an
 * earlier version of AmipWindowResizeTo() computed the sizing gadget's
 * anchor by hand from Window->BorderRight/BorderBottom (inset by half
 * the border thickness), which LOOKED right pixel-for-pixel against a
 * live screenshot of fixtures/second-screen-app's own sizing gadget,
 * yet the resize never actually happened -- Intuition's own real
 * gadget rectangle for that gadget, once located this way, uses
 * GFLG_RELRIGHT/GFLG_RELBOTTOM-relative dimensions AmipGadgetCenter()
 * already has to resolve correctly for every other gadget kind, and
 * skipping that resolution was the actual bug, not the border math
 * itself. */
struct Gadget *AmipFindSystemGadget(struct Window *window, UWORD sysType)
{
    struct Gadget *g;

    for (g = window->FirstGadget; g != NULL; g = g->NextGadget) {
        if ((g->GadgetType & GTYP_SYSGADGET) &&
            (g->GadgetType & GTYP_SYSTYPEMASK) == sysType) {
            return g;
        }
    }
    return NULL;
}

BOOL AmipWindowMoveBy(struct Window *window, WORD dx, WORD dy)
{
    struct Gadget *dragGadget;
    WORD anchorX, anchorY;

    if (window == NULL || !(window->Flags & WFLG_DRAGBAR)) {
        return FALSE;
    }

    BringWindowForward(window);

    dragGadget = AmipFindSystemGadget(window, GTYP_WDRAGGING);
    if (dragGadget == NULL || !AmipGadgetCenter(window, dragGadget, &anchorX, &anchorY)) {
        /* Honest fallback if the real system gadget somehow isn't in
         * the chain despite WFLG_DRAGBAR being set (shouldn't happen)
         * -- horizontal center of the title bar, vertically centered
         * in the window's own top border strip. */
        anchorX = window->LeftEdge + window->Width / 2;
        anchorY = window->TopEdge + window->BorderTop / 2;
    }
    return AmipDragAt(window->WScreen, anchorX, anchorY,
                       (WORD)(anchorX + dx), (WORD)(anchorY + dy));
}

BOOL AmipWindowResizeTo(struct Window *window, WORD targetWidth, WORD targetHeight)
{
    struct Gadget *sizeGadget;
    WORD anchorX, anchorY, dx, dy;

    if (window == NULL || !(window->Flags & WFLG_SIZEGADGET)) {
        return FALSE;
    }

    BringWindowForward(window);

    sizeGadget = AmipFindSystemGadget(window, GTYP_SIZING);
    if (sizeGadget == NULL || !AmipGadgetCenter(window, sizeGadget, &anchorX, &anchorY)) {
        /* Honest fallback, same reasoning as AmipWindowMoveBy() above. */
        anchorX = window->LeftEdge + window->Width - 1 - window->BorderRight / 2;
        anchorY = window->TopEdge + window->Height - 1 - window->BorderBottom / 2;
    }
    dx = (WORD)(targetWidth - window->Width);
    dy = (WORD)(targetHeight - window->Height);
    return AmipDragAt(window->WScreen, anchorX, anchorY,
                       (WORD)(anchorX + dx), (WORD)(anchorY + dy));
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
