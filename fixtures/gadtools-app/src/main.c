/*
 * gadtools-app -- minimal GadTools application used as an AmiPilot
 * conformance fixture (docs/implementation-plan.md phase 0.1). Opens a
 * window with a button, a string gadget, and a checkbox, each with a
 * stable GA_ID and a label, so AmiInspect/the walker has real content to
 * classify instead of only a window's built-in system gadgets.
 *
 * Quits on close-gadget or the button being pressed.
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

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *GadToolsBase;

#define GID_CONNECT  1
#define GID_HOST     2
#define GID_ENABLED  3

int main(void)
{
    struct Screen *screen = NULL;
    struct Window *window = NULL;
    struct Gadget *glist = NULL;
    struct Gadget *gad;
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
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);

    ng.ng_TopEdge += 24;
    ng.ng_GadgetText = (UBYTE *)"_Host:";
    ng.ng_GadgetID = GID_HOST;
    ng.ng_Flags = PLACETEXT_LEFT;
    gad = CreateGadget(STRING_KIND, gad, &ng,
                        GTST_String, (ULONG)"",
                        GTST_MaxChars, 64,
                        TAG_DONE);

    ng.ng_TopEdge += 24;
    ng.ng_GadgetText = (UBYTE *)"_Enabled";
    ng.ng_GadgetID = GID_ENABLED;
    ng.ng_Flags = PLACETEXT_RIGHT;
    gad = CreateGadget(CHECKBOX_KIND, gad, &ng,
                        GTCB_Checked, FALSE,
                        TAG_DONE);

    if (gad == NULL) {
        rc = RETURN_FAIL;
        goto cleanup;
    }

    window = OpenWindowTags(NULL,
                             WA_Left, 40, WA_Top, 40,
                             WA_Width, 220, WA_Height, 130,
                             WA_Title, (ULONG)"AmiPilot GadTools Fixture",
                             WA_Gadgets, (ULONG)glist,
                             WA_CloseGadget, TRUE,
                             WA_DragBar, TRUE,
                             WA_DepthGadget, TRUE,
                             WA_Activate, TRUE,
                             WA_SimpleRefresh, TRUE,
                             WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW,
                             WA_PubScreen, (ULONG)screen,
                             TAG_DONE);

    if (window == NULL) {
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
        CloseWindow(window);
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
