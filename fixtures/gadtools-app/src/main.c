/*
 * gadtools-app -- minimal GadTools application used as an AmiPilot
 * conformance fixture (docs/implementation-plan.md phase 0.1). Opens a
 * window with two buttons, a string gadget, and a checkbox, each with a
 * stable GA_ID, so AmiInspect/the walker has real content to classify
 * instead of only a window's built-in system gadgets. The two buttons
 * (Connect, Cancel) share AMIP_ROLE_BUTTON and exist specifically so
 * tier-2 ROLE=/INDEX= locators (phase 0.4) have a real same-role pair
 * to disambiguate by INDEX= -- both are PLACETEXT_IN (label baked into
 * the button's own rendered imagery, confirmed via AmiInspect NOT to
 * populate gadget->GadgetText -- see the "Confirmed limit" in
 * intuition-model/src/walk.c and CLAUDE.md), so LABEL= locator testing
 * uses the Host/Enabled gadgets below instead, whose labels genuinely
 * are visible to the walker (PLACETEXT_LEFT/RIGHT).
 *
 * Quits on close-gadget or the Connect button being pressed. Cancel
 * writes an observable marker into the Host string gadget (same
 * technique the menu items below already use) so a CLICK against it --
 * addressed either by GA_ID or by a ROLE=/INDEX= locator -- is
 * verifiable via the existing GETTEXT path without a new assertion
 * mechanism.
 *
 * Also carries a horizontal SLIDER_KIND gadget (Volume, range 0..100,
 * starts at 0) -- AmiPilot's first DRAG target (phase 0.4). Its
 * IDCMP_GADGETUP handler writes "slider=<level>" into the Host string
 * gadget using the level GadTools itself reports in the event's own
 * Code field (its documented contract for SLIDER_KIND/PROP_KIND, no
 * separate GT_GetGadgetAttrs() round trip needed) -- the same
 * observable-marker technique as Cancel/the menu items above, so a
 * DRAG's effect is verifiable through the already-proven GETTEXT path
 * and genuinely confirms the drag reached GadTools' real slider-
 * tracking code, not just that input.device events were injected.
 *
 * A third button (Open Req) opens a second, plain window (no gadgets
 * or IDCMP of its own, purely decorative -- see the "No WA_IDCMP"
 * comment at its own OpenWindowTags call) after a deliberate ~2s
 * Delay() in its own IDCMP_GADGETUP handler. This exists specifically
 * to give AmiPilot's WAITFOR/CLICK EXPECT= (phase 0.5) a REAL race to
 * close, not a hypothetical: AmiPilotServer's CLICK returns RC 0 as
 * soon as the input.device event is injected, well before this
 * handler even runs, and this fixture doesn't start opening the
 * window until a full two seconds after receiving the GADGETUP event -- an
 * immediate post-click check reliably misses it, exactly the scenario
 * docs/implementation-plan.md's own success criteria call for.
 *
 * Also carries a menu strip (phase 0.4 MENU/MENUPICK conformance):
 * one "Project" menu with an "About" item (shortcut A, sets the Host
 * string gadget's text so a MENUPICK-by-shortcut round trip is
 * observable via the existing GETTEXT path), a "Toggle" checkmark
 * item (CHECKIT|MENUTOGGLE, starts checked -- exercises the walker's
 * checkit/checked fields, and is also the fixture's only enabled,
 * shortcut-less leaf item, so it doubles as the MENUPICK
 * pointer-based-fallback target, issue #63 -- its own IDCMP_MENUPICK
 * marker write proves a real RMB-down/move/RMB-up round trip reached
 * Intuition's own menu tracking, not just that a shortcut keystroke
 * was struck), a permanently "Disabled" item (no shortcut,
 * ITEMENABLED off), a separator bar, and a "More" item with two
 * submenu entries: "Sub Item" (shortcut S, also sets the Host text --
 * exercises the one-level-deep submenu walk and its own shortcut
 * addressing, menu 0, item 4, sub 0) and "Sub NoShortcut" (no
 * shortcut, sub 1 -- the fixture's only shortcut-less sub-item,
 * proving MENUPICK's pointer-based fallback reaches a genuine
 * sub-item, not just a top-level one).
 *
 * Also carries a Count INTEGER_KIND gadget (issue #64) -- shares the
 * same underlying GTYP_STRGADGET as the Host STRING_KIND gadget
 * above, exercising the walker's GTIN_Number kind-probe discriminator
 * (intuition-model/src/walk.c's ClassifyStringGadget()) against a
 * real target.
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <stdio.h>

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *GadToolsBase;

#define GID_CONNECT  1
#define GID_HOST     2
#define GID_ENABLED  3
#define GID_CANCEL   4
#define GID_SLIDER   5
#define GID_OPENREQ  6
#define GID_ASK      7
#define GID_COUNT    8

#define MENUNUM_PROJECT   0
#define ITEMNUM_ABOUT     0
#define ITEMNUM_TOGGLE    1
#define ITEMNUM_MORE      4
#define SUBNUM_SUBITEM    0
#define SUBNUM_SUBNOSHORTCUT 1

static struct NewMenu g_newMenu[] = {
    { NM_TITLE, (STRPTR)"Project",  NULL,        0,                        0, NULL },
    { NM_ITEM,  (STRPTR)"About",    (STRPTR)"A", 0,                        0, NULL },
    { NM_ITEM,  (STRPTR)"Toggle",   NULL,        CHECKIT | MENUTOGGLE | CHECKED, 0, NULL },
    { NM_ITEM,  (STRPTR)"Disabled", NULL,        NM_ITEMDISABLED,          0, NULL },
    { NM_ITEM,  NM_BARLABEL,        NULL,        0,                        0, NULL },
    { NM_ITEM,  (STRPTR)"More",     NULL,        0,                        0, NULL },
    { NM_SUB,   (STRPTR)"Sub Item", (STRPTR)"S", 0,                        0, NULL },
    { NM_SUB,   (STRPTR)"Sub NoShortcut", NULL,  0,                        0, NULL },
    { NM_END,   NULL,               NULL,        0,                        0, NULL },
};

int main(void)
{
    struct Screen *screen = NULL;
    struct Window *window = NULL;
    struct Window *reqWindow = NULL;
    struct Gadget *glist = NULL;
    struct Gadget *gad;
    struct Gadget *hostGad = NULL;
    struct Menu *menuStrip = NULL;
    struct NewGadget ng;
    APTR visualInfo = NULL;
    BOOL done = FALSE;
    int rc = RETURN_OK;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 37);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 37);
    GadToolsBase = OpenLibrary((CONST_STRPTR)"gadtools.library", 37);

    if (IntuitionBase == NULL || GfxBase == NULL || GadToolsBase == NULL) {
        rc = RETURN_FAIL;
        goto cleanup;
    }

    screen = LockPubScreen(NULL);
    if (screen == NULL) {
        rc = RETURN_FAIL;
        goto cleanup;
    }

    visualInfo = GetVisualInfo(screen, TAG_DONE);
    if (visualInfo == NULL) {
        rc = RETURN_FAIL;
        goto cleanup;
    }

    gad = CreateContext(&glist);

    ng.ng_LeftEdge = 20;
    ng.ng_TopEdge = 24;
    ng.ng_Width = 100;
    ng.ng_Height = 14;
    ng.ng_GadgetText = (UBYTE *)"_Connect";
    ng.ng_TextAttr = screen->Font;
    ng.ng_GadgetID = GID_CONNECT;
    ng.ng_Flags = PLACETEXT_IN;
    ng.ng_VisualInfo = visualInfo;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, GT_Underscore, '_', TAG_DONE);

    ng.ng_TopEdge += 24;
    ng.ng_GadgetText = (UBYTE *)"_Host:";
    ng.ng_GadgetID = GID_HOST;
    ng.ng_Flags = PLACETEXT_LEFT;
    gad = CreateGadget(STRING_KIND, gad, &ng,
                        GTST_String, (ULONG)"",
                        GTST_MaxChars, 64,
                        GT_Underscore, '_',
                        TAG_DONE);
    hostGad = gad;

    ng.ng_TopEdge += 24;
    ng.ng_GadgetText = (UBYTE *)"_Enabled";
    ng.ng_GadgetID = GID_ENABLED;
    ng.ng_Flags = PLACETEXT_RIGHT;
    gad = CreateGadget(CHECKBOX_KIND, gad, &ng,
                        GTCB_Checked, FALSE,
                        GT_Underscore, '_',
                        TAG_DONE);

    ng.ng_TopEdge += 24;
    ng.ng_GadgetText = (UBYTE *)"Ca_ncel";
    ng.ng_GadgetID = GID_CANCEL;
    /* PLACETEXT_IN, same as Connect above -- tried PLACETEXT_RIGHT
     * here first (like the Enabled checkbox below, whose label DOES
     * populate GadgetText), expecting a real external label the
     * walker could see for LABEL= locator testing. Confirmed via
     * AmiInspect (ground truth, no server/wire involved) that
     * BUTTON_KIND leaves gadget->GadgetText unpopulated regardless of
     * PLACETEXT_IN vs PLACETEXT_RIGHT -- a real, newly-confirmed
     * refinement of the existing "Confirmed limit" in
     * intuition-model/src/walk.c and CLAUDE.md, not a bug in this
     * fixture or the walker: see those files for the corrected claim.
     * Reverted to PLACETEXT_IN (matching Connect) since PLACETEXT_RIGHT
     * bought nothing here; LABEL= locator testing uses the Host/
     * Enabled gadgets instead, whose labels are genuinely visible. */
    ng.ng_Flags = PLACETEXT_IN;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, GT_Underscore, '_', TAG_DONE);

    /* Horizontal slider (SLIDER_KIND, a propgclass gadget under the
     * hood) -- AmiPilot's first drag target (phase 0.4 DRAG). Range
     * 0..100, starts at 0. GA_RelVerify is REQUIRED for GadTools to
     * ever send IDCMP_GADGETUP for a slider at all -- confirmed via
     * gadtools.doc's own CreateGadgetA autodoc ("GA_RelVerify (BOOL) -
     * If you want to hear each slider IDCMP_GADGETUP event (defaults
     * to FALSE)"), and confirmed the hard way: a first attempt at this
     * fixture omitted it and no IDCMP_GADGETUP ever arrived for the
     * slider (verified live via Amiberry -- the Host field never
     * updated after a real DRAG), not a walker/action-engine bug. The
     * handler below writes the live level (the event's own Code field,
     * GadTools' documented contract for a proportional gadget) into
     * the Host string gadget, the same observable-marker technique the
     * menu items and Cancel button above already use -- proves a drag
     * genuinely reached GadTools' real slider-tracking code, not just
     * that input.device events were injected. */
    ng.ng_TopEdge += 24;
    ng.ng_GadgetText = (UBYTE *)"Vo_lume";
    ng.ng_GadgetID = GID_SLIDER;
    ng.ng_Flags = PLACETEXT_ABOVE;
    gad = CreateGadget(SLIDER_KIND, gad, &ng,
                        GTSL_Min, 0,
                        GTSL_Max, 100,
                        GTSL_Level, 0,
                        GTSL_MaxLevelLen, 3,
                        GA_RelVerify, TRUE,
                        GT_Underscore, '_',
                        TAG_DONE);

    /* See this file's own header comment -- the deliberate ~2s Delay()
     * in this button's own IDCMP_GADGETUP handler (below) is what
     * gives WAITFOR/CLICK EXPECT= (phase 0.5) a real race to close. */
    ng.ng_TopEdge += 24;
    ng.ng_GadgetText = (UBYTE *)"_Open Req";
    ng.ng_GadgetID = GID_OPENREQ;
    ng.ng_Flags = PLACETEXT_IN;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, GT_Underscore, '_', TAG_DONE);

    /* AmiPilot's WAITFOR REQUESTER on-target check (issue #52's
     * detection-only slice) needs a REAL genuine Intuition Requester
     * to detect, not a second plain window (GID_OPENREQ above is
     * exactly that, kept as-is for its own existing WAITFOR/EXPECT=
     * race-condition check). AutoRequest() below is a real, blocking
     * modal Requester attached to THIS window (window->FirstRequest
     * becomes non-NULL for its whole duration) -- the actual thing
     * WAITFOR REQUESTER is meant to detect. */
    ng.ng_TopEdge += 24;
    ng.ng_GadgetText = (UBYTE *)"_Ask";
    ng.ng_GadgetID = GID_ASK;
    ng.ng_Flags = PLACETEXT_IN;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, GT_Underscore, '_', TAG_DONE);

    /* INTEGER_KIND -- issue #64's target: both this and the Host
     * STRING_KIND gadget above create the same underlying
     * GTYP_STRGADGET, nothing in the raw struct Gadget tells them
     * apart post-creation (confirmed here the same way BUTTON_KIND/
     * CHECKBOX_KIND's shared GTYP_BOOLGADGET was: real fixture,
     * real AmiInspect/TREE output). intuition-model/src/walk.c's
     * ClassifyStringGadget() discriminates via GT_GetGadgetAttrsA's
     * documented GTIN_Number probe (gadtools.doc: listed under
     * INTEGER_KIND only, not STRING_KIND). */
    ng.ng_TopEdge += 24;
    ng.ng_GadgetText = (UBYTE *)"Co_unt";
    ng.ng_GadgetID = GID_COUNT;
    ng.ng_Flags = PLACETEXT_LEFT;
    gad = CreateGadget(INTEGER_KIND, gad, &ng,
                        GTIN_Number, 0,
                        GTIN_MaxChars, 6,
                        GT_Underscore, '_',
                        TAG_DONE);

    if (gad == NULL) {
        rc = RETURN_FAIL;
        goto cleanup;
    }

    menuStrip = CreateMenus(g_newMenu, GTMN_FrontPen, 0, TAG_DONE);
    if (menuStrip == NULL) {
        rc = RETURN_FAIL;
        goto cleanup;
    }
    if (!LayoutMenus(menuStrip, visualInfo, GTMN_NewLookMenus, TRUE, TAG_DONE)) {
        rc = RETURN_FAIL;
        goto cleanup;
    }

    window = OpenWindowTags(NULL,
                             WA_Left, 40, WA_Top, 40,
                             WA_Width, 220, WA_Height, 238,
                             WA_Title, (ULONG)"AmiPilot GadTools Fixture",
                             WA_Gadgets, (ULONG)glist,
                             WA_CloseGadget, TRUE,
                             WA_DragBar, TRUE,
                             WA_DepthGadget, TRUE,
                             WA_Activate, TRUE,
                             WA_SimpleRefresh, TRUE,
                             WA_NewLookMenus, TRUE,
                             WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW
                                       | IDCMP_MOUSEBUTTONS | IDCMP_MENUPICK,
                             WA_PubScreen, (ULONG)screen,
                             TAG_DONE);

    if (window == NULL) {
        rc = RETURN_FAIL;
        goto cleanup;
    }

    if (SetMenuStrip(window, menuStrip) == 0) {
        rc = RETURN_FAIL;
        goto cleanup;
    }

    GT_RefreshWindow(window, NULL);

    while (!done) {
        struct IntuiMessage *msg;

        WaitPort(window->UserPort);

        while ((msg = GT_GetIMsg(window->UserPort)) != NULL) {
            switch (msg->Class) {
                case IDCMP_CLOSEWINDOW:
                    done = TRUE;
                    break;
                case IDCMP_REFRESHWINDOW:
                    GT_BeginRefresh(window);
                    GT_EndRefresh(window, TRUE);
                    break;
                case IDCMP_GADGETUP:
                    if (((struct Gadget *)msg->IAddress)->GadgetID == GID_CONNECT) {
                        done = TRUE;
                    } else if (((struct Gadget *)msg->IAddress)->GadgetID == GID_CANCEL) {
                        GT_SetGadgetAttrs(hostGad, window, NULL,
                                          GTST_String, (ULONG)"cancel clicked", TAG_DONE);
                    } else if (((struct Gadget *)msg->IAddress)->GadgetID == GID_SLIDER) {
                        /* GadTools' own documented contract for
                         * SLIDER_KIND/PROP_KIND: IDCMP_GADGETUP's Code
                         * field IS the new GTSL_Level -- no separate
                         * GT_GetGadgetAttrs() round trip needed. */
                        char levelText[16];
                        sprintf(levelText, "slider=%d", (int)msg->Code);
                        GT_SetGadgetAttrs(hostGad, window, NULL,
                                          GTST_String, (ULONG)levelText, TAG_DONE);
                    } else if (((struct Gadget *)msg->IAddress)->GadgetID == GID_OPENREQ) {
                        /* Deliberate delay BEFORE opening the second
                         * window -- see this file's own header
                         * comment for why. Blocking this app's own
                         * event loop for ~2s is the point: it's what
                         * makes the race real and measurable from the
                         * outside, not a hypothetical. 2s (not ~1s)
                         * so a 1-second EXPECT=/WAITFOR TIMEOUT=
                         * (whole seconds only, per the wire grammar)
                         * reliably exercises the RC_TIMEOUT path too,
                         * not just the "it eventually appears" path --
                         * confirmed live: a 1s window was too close to
                         * a ~1s delay's own poll-tick jitter to
                         * reliably time out either way. */
                        Delay(100);
                        if (reqWindow == NULL) {
                            /* No WA_IDCMP: this window generates no
                             * messages of its own (IDCMPFlags default
                             * to 0) -- deliberately, to avoid the
                             * real complexity of sharing an IDCMP port
                             * between two windows (there's no
                             * OpenWindowTags-time tag for that in
                             * classic Intuition; it needs opening with
                             * no IDCMP, then manually assigning
                             * window->UserPort and calling
                             * ModifyIDCMP() -- more machinery than
                             * this fixture needs). Its close gadget is
                             * purely decorative as a result; automated
                             * verification uses TREE/WAITFOR to see it
                             * appear, not manual interaction with it.
                             * Cleaned up at app quit either way (see
                             * cleanup: below). */
                            reqWindow = OpenWindowTags(NULL,
                                WA_Left, 60, WA_Top, 60,
                                WA_Width, 160, WA_Height, 50,
                                WA_Title, (ULONG)"Async Dialog",
                                WA_CloseGadget, TRUE,
                                WA_DragBar, TRUE,
                                WA_Activate, TRUE,
                                WA_PubScreen, (ULONG)screen,
                                TAG_DONE);
                        }
                    } else if (((struct Gadget *)msg->IAddress)->GadgetID == GID_ASK) {
                        /* A REAL, blocking modal Requester (RKRM/NDK
                         * AutoRequest() -- window->FirstRequest is
                         * genuinely non-NULL for its whole duration),
                         * not a second window -- the actual thing
                         * WAITFOR REQUESTER (issue #52's detection-only
                         * slice) is meant to detect. AutoRequest()
                         * itself runs its own internal message loop on
                         * window->UserPort until answered; calling it
                         * from inside an IDCMP_GADGETUP handler like
                         * this is the RKRM's own documented usage
                         * pattern, not a hack. PosFlags/NegFlags both 0
                         * -- IDCMP_GADGETUP is always an implicit
                         * dismiss condition, no extra flags needed for
                         * a plain Yes/No. */
                        struct IntuiText body = { 0, 1, JAM2, 4, 4, NULL,
                                                   (UBYTE *)"Are you sure?", NULL };
                        struct IntuiText posText = { 0, 1, JAM2, 4, 4, NULL,
                                                      (UBYTE *)"Yes", NULL };
                        struct IntuiText negText = { 0, 1, JAM2, 4, 4, NULL,
                                                      (UBYTE *)"No", NULL };
                        AutoRequest(window, &body, &posText, &negText, 0, 0, 220, 60);
                    }
                    break;
                /* Diagnostic: IDCMP_MOUSEBUTTONS only arrives for clicks
                 * Intuition did NOT deliver to a gadget, and its
                 * MouseX/MouseY are window-relative -- ground truth for
                 * where a synthetic click actually landed when it misses
                 * (added while debugging the action engine's input
                 * injection; harmless to keep for future runs). */
                case IDCMP_MOUSEBUTTONS:
                    printf("GTApp: MOUSEBUTTONS code=0x%04x qual=0x%04x at window-relative [%d,%d]\n",
                           msg->Code, msg->Qualifier, msg->MouseX, msg->MouseY);
                    fflush(stdout); /* still running when the log is read */
                    break;
                /* Proves a MENUPICK-by-shortcut round trip actually
                 * reached the app through Intuition's own menu
                 * resolution (server/src/action.c strikes Right-Amiga
                 * + the shortcut char; it never synthesizes
                 * IDCMP_MENUPICK directly) -- writes an observable,
                 * distinct marker into the Host string gadget per
                 * item, readable back via the already-verified GETTEXT
                 * path rather than needing a new assertion mechanism. */
                case IDCMP_MENUPICK:
                    if (MENUNUM(msg->Code) == MENUNUM_PROJECT
                        && ITEMNUM(msg->Code) == ITEMNUM_ABOUT
                        && SUBNUM(msg->Code) == NOSUB) {
                        GT_SetGadgetAttrs(hostGad, window, NULL,
                                          GTST_String, (ULONG)"about picked", TAG_DONE);
                    } else if (MENUNUM(msg->Code) == MENUNUM_PROJECT
                               && ITEMNUM(msg->Code) == ITEMNUM_TOGGLE
                               && SUBNUM(msg->Code) == NOSUB) {
                        /* Enabled, shortcut-less -- the fixture's only
                         * candidate for proving MENUPICK's pointer-based
                         * fallback (issue #63) genuinely reaches Intuition's
                         * own IDCMP_MENUPICK, not just that a shortcut
                         * keystroke was struck. */
                        GT_SetGadgetAttrs(hostGad, window, NULL,
                                          GTST_String, (ULONG)"toggle picked", TAG_DONE);
                    } else if (MENUNUM(msg->Code) == MENUNUM_PROJECT
                               && ITEMNUM(msg->Code) == ITEMNUM_MORE
                               && SUBNUM(msg->Code) == SUBNUM_SUBITEM) {
                        GT_SetGadgetAttrs(hostGad, window, NULL,
                                          GTST_String, (ULONG)"subitem picked", TAG_DONE);
                    } else if (MENUNUM(msg->Code) == MENUNUM_PROJECT
                               && ITEMNUM(msg->Code) == ITEMNUM_MORE
                               && SUBNUM(msg->Code) == SUBNUM_SUBNOSHORTCUT) {
                        /* Enabled, shortcut-less, one level deep -- the
                         * fixture's only candidate for proving
                         * MENUPICK's pointer-based fallback (issue #63)
                         * reaches a genuine sub-item, not just a
                         * top-level one (Toggle, above). */
                        GT_SetGadgetAttrs(hostGad, window, NULL,
                                          GTST_String, (ULONG)"subitem noshortcut picked", TAG_DONE);
                    }
                    break;
                default:
                    break;
            }
            GT_ReplyIMsg(msg);
        }
    }

cleanup:
    /* reqWindow shares window's own UserPort (WA_UserPort above) --
     * must close before window itself, which owns that port. */
    if (reqWindow != NULL) {
        CloseWindow(reqWindow);
    }
    if (window != NULL) {
        ClearMenuStrip(window);
        CloseWindow(window);
    }
    if (menuStrip != NULL) {
        FreeMenus(menuStrip);
    }
    if (glist != NULL) {
        FreeGadgets(glist);
    }
    if (visualInfo != NULL) {
        FreeVisualInfo(visualInfo);
    }
    if (screen != NULL) {
        UnlockPubScreen(NULL, screen);
    }
    if (GadToolsBase != NULL) {
        CloseLibrary(GadToolsBase);
    }
    if (GfxBase != NULL) {
        CloseLibrary((struct Library *)GfxBase);
    }
    if (IntuitionBase != NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
    }

    return rc;
}
