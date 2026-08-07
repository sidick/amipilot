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
 * Also carries a menu strip (phase 0.4 MENU/MENUPICK conformance):
 * one "Project" menu with an "About" item (shortcut A, sets the Host
 * string gadget's text so a MENUPICK-by-shortcut round trip is
 * observable via the existing GETTEXT path), a "Toggle" checkmark
 * item (CHECKIT|MENUTOGGLE, starts checked -- exercises the walker's
 * checkit/checked fields), a permanently "Disabled" item (no
 * shortcut, ITEMENABLED off), a separator bar, and a "More" item with
 * one submenu entry "Sub Item" (shortcut S, also sets the Host text)
 * -- exercises the one-level-deep submenu walk and its own shortcut
 * addressing (menu 0, item 4, sub 0).
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
#include <stdio.h>

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *GadToolsBase;

#define GID_CONNECT  1
#define GID_HOST     2
#define GID_ENABLED  3
#define GID_CANCEL   4

#define MENUNUM_PROJECT   0
#define ITEMNUM_ABOUT     0
#define ITEMNUM_MORE      4
#define SUBNUM_SUBITEM    0

static struct NewMenu g_newMenu[] = {
    { NM_TITLE, (STRPTR)"Project",  NULL,        0,                        0, NULL },
    { NM_ITEM,  (STRPTR)"About",    (STRPTR)"A", 0,                        0, NULL },
    { NM_ITEM,  (STRPTR)"Toggle",   (STRPTR)"T", CHECKIT | MENUTOGGLE | CHECKED, 0, NULL },
    { NM_ITEM,  (STRPTR)"Disabled", NULL,        NM_ITEMDISABLED,          0, NULL },
    { NM_ITEM,  NM_BARLABEL,        NULL,        0,                        0, NULL },
    { NM_ITEM,  (STRPTR)"More",     NULL,        0,                        0, NULL },
    { NM_SUB,   (STRPTR)"Sub Item", (STRPTR)"S", 0,                        0, NULL },
    { NM_END,   NULL,               NULL,        0,                        0, NULL },
};

int main(void)
{
    struct Screen *screen = NULL;
    struct Window *window = NULL;
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
                             WA_Width, 220, WA_Height, 154,
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
                               && ITEMNUM(msg->Code) == ITEMNUM_MORE
                               && SUBNUM(msg->Code) == SUBNUM_SUBITEM) {
                        GT_SetGadgetAttrs(hostGad, window, NULL,
                                          GTST_String, (ULONG)"subitem picked", TAG_DONE);
                    }
                    break;
                default:
                    break;
            }
            GT_ReplyIMsg(msg);
        }
    }

cleanup:
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
