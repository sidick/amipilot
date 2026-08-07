/*
 * second-screen-app -- minimal GadTools application that opens its
 * OWN custom screen (OpenScreenTagList(), not LockPubScreen()), used
 * as an AmiPilot conformance fixture for multi-screen support (phase
 * 0.4). Deliberately as close to gadtools-app's own minimal shape as
 * possible (one button, GA_ID=1, quits on close-gadget or the button
 * being pressed) -- the only thing this fixture exists to vary is
 * "which screen is the window on."
 *
 * Window titled "GadTools Fixture 2" -- deliberately overlaps the
 * substrings "GadTools" and "Fixture" with gadtools-app's own
 * "AmiPilot GadTools Fixture" (without touching that title, which
 * several existing on-target checks assert on verbatim) so a loose
 * pattern match can be shown colliding across two screens, then
 * resolved with SCREEN=. Screen titled "AmiPilot Second Screen" (its
 * DefaultTitle -- see action_engine.h's AmipFindWindow doc comment
 * for why SCREEN= and SCREENS both key off DefaultTitle, not the
 * live Title field).
 *
 * Opened via `OpenScreenTagList(NULL, tags)` with only SA_Title set:
 * the autodoc is explicit that passing NULL for both the NewScreen
 * pointer and omitting every other tag gets "a screen with defaults
 * in all fields, including display mode, depth, colors, dimension,
 * title, and so on" -- deliberately not hand-picking a DisplayID/
 * Width/Height/Depth, which would tie this fixture to a specific
 * chipset/config rather than whatever screen mode the host is
 * actually running.
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

#define GID_CONNECT 1

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

    screen = OpenScreenTags(NULL, SA_Title, (ULONG)"AmiPilot Second Screen", TAG_DONE);
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

    if (gad == NULL) {
        rc = RETURN_FAIL;
        goto cleanup;
    }

    window = OpenWindowTags(NULL,
                             WA_Left, 20, WA_Top, 20,
                             WA_Width, 160, WA_Height, 80,
                             WA_Title, (ULONG)"GadTools Fixture 2",
                             WA_Gadgets, (ULONG)glist,
                             WA_CloseGadget, TRUE,
                             WA_DragBar, TRUE,
                             WA_DepthGadget, TRUE,
                             WA_Activate, TRUE,
                             WA_SimpleRefresh, TRUE,
                             WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW,
                             WA_CustomScreen, (ULONG)screen,
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
        CloseScreen(screen);
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
