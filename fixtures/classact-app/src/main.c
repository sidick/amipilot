/*
 * classact-app -- minimal ReAction/BOOPSI application used as an AmiPilot
 * conformance fixture (docs/implementation-plan.md phase 0.1). Same three
 * controls as fixtures/gadtools-app (button + string + checkbox, real
 * GA_IDs) but built from window.class/layout.gadget/button.gadget/
 * string.gadget/checkbox.gadget instead of GadTools -- the BOOPSI/ReAction
 * path intuition-model's walker doesn't classify yet (see the
 * GTYP_CUSTOMGADGET case in walk.c).
 *
 * Quits on close-gadget or the button being pressed.
 *
 * CRITICAL (cost a long debugging session, 2026-08-05): every library
 * base below is explicitly initialized (= NULL). An uninitialized
 * `struct Library *WindowBase;` is a COMMON symbol, which does not stop
 * the linker from pulling libnix's own WindowBase archive member -- and
 * that member carries a pre-main auto-open constructor that derives the
 * library name from the base name ("window.library"), fails to open the
 * nonexistent library, and aborts the program with
 * "window.library failed to load" before main() ever runs. The = NULL
 * initializer makes each base a strong .data definition, which blocks
 * the archive pull. amiauth and AmiInspect both do this; the original
 * version of this file didn't, and failed exactly that way.
 */

#include <stdlib.h>
#include <string.h>

#include <intuition/classusr.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/checkbox.h>
#include <gadgets/string.h>
#include <classes/window.h>

#define __CLIB_PRAGMA_LIBCALL
#include <proto/alib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/window.h>
#include <proto/button.h>
#include <proto/checkbox.h>
#include <proto/string.h>

struct IntuitionBase *IntuitionBase = NULL;
struct Library *WindowBase = NULL;
struct Library *LayoutBase = NULL;
struct Library *ButtonBase = NULL;
struct Library *CheckBoxBase = NULL;
struct Library *StringBase = NULL;

#define GID_CONNECT  1
#define GID_HOST     2
#define GID_ENABLED  3

/* PLACETEXT_RIGHT's canonical home is <libraries/gadtools.h>, not any
 * ReAction header, despite CHECKBOX_TextPlace using the same constant --
 * defined directly here rather than pulling in a GadTools header this
 * app has no other reason to depend on. */
#define CHECKBOX_TEXT_RIGHT 0x0002

/* Raw dos.library Write() to Output() so failure paths are visible in a
 * Run-redirected log without depending on stdio buffering. */
static void Diag(const char *msg)
{
    Write(Output(), (APTR)msg, (LONG)strlen(msg));
}

static void CleanExit(Object *windowObject, int rc)
{
    if (windowObject != NULL) {
        DisposeObject(windowObject);
    }

    if (StringBase != NULL) {
        CloseLibrary(StringBase);
    }
    if (CheckBoxBase != NULL) {
        CloseLibrary(CheckBoxBase);
    }
    if (ButtonBase != NULL) {
        CloseLibrary(ButtonBase);
    }
    if (LayoutBase != NULL) {
        CloseLibrary(LayoutBase);
    }
    if (WindowBase != NULL) {
        CloseLibrary(WindowBase);
    }
    if (IntuitionBase != NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
    }

    exit(rc);
}

static void ProcessEvents(Object *windowObject)
{
    ULONG windowSignal;
    ULONG result;
    ULONG code;
    BOOL done = FALSE;

    GetAttr(WINDOW_SigMask, windowObject, &windowSignal);

    while (!done) {
        Wait(windowSignal);

        while ((result = DoMethod(windowObject, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            switch (result & WMHI_CLASSMASK) {
                case WMHI_CLOSEWINDOW:
                    done = TRUE;
                    break;
                case WMHI_GADGETUP:
                    if ((result & WMHI_GADGETMASK) == GID_CONNECT) {
                        done = TRUE;
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

int main(void)
{
    struct Window *intuiWindow = NULL;
    Object *windowObject = NULL;
    Object *mainLayout = NULL;
    Object *connectButton;
    Object *hostString;
    Object *enabledCheckbox;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 37);
    WindowBase = OpenLibrary((CONST_STRPTR)"window.class", 44);
    LayoutBase = OpenLibrary((CONST_STRPTR)"gadgets/layout.gadget", 44);
    ButtonBase = OpenLibrary((CONST_STRPTR)"gadgets/button.gadget", 44);
    CheckBoxBase = OpenLibrary((CONST_STRPTR)"gadgets/checkbox.gadget", 44);
    StringBase = OpenLibrary((CONST_STRPTR)"gadgets/string.gadget", 44);

    Diag(IntuitionBase != NULL ? "caapp: intuition.library ok\n" : "caapp: intuition.library FAILED\n");
    Diag(WindowBase != NULL ? "caapp: window.class ok\n" : "caapp: window.class FAILED\n");
    Diag(LayoutBase != NULL ? "caapp: layout.gadget ok\n" : "caapp: layout.gadget FAILED\n");
    Diag(ButtonBase != NULL ? "caapp: button.gadget ok\n" : "caapp: button.gadget FAILED\n");
    Diag(CheckBoxBase != NULL ? "caapp: checkbox.gadget ok\n" : "caapp: checkbox.gadget FAILED\n");
    Diag(StringBase != NULL ? "caapp: string.gadget ok\n" : "caapp: string.gadget FAILED\n");

    if (IntuitionBase == NULL || WindowBase == NULL || LayoutBase == NULL
        || ButtonBase == NULL || CheckBoxBase == NULL || StringBase == NULL) {
        CleanExit(NULL, RETURN_FAIL);
    }

    connectButton = NewObject(NULL, (CONST_STRPTR)"button.gadget",
                               GA_ID, GID_CONNECT,
                               GA_Text, (ULONG)"Connect",
                               GA_RelVerify, TRUE,
                               TAG_DONE);

    /* STRING_GetClass()/CHECKBOX_GetClass(), not NewObject(NULL, "name"):
     * unlike button.gadget, these classes don't register a public class
     * name, so lookup-by-name returns NULL (confirmed empirically under
     * WB 3.2.3 -- button ok, string/checkbox FAILED via the name form). */
    hostString = NewObject(STRING_GetClass(), NULL,
                            GA_ID, GID_HOST,
                            GA_Text, (ULONG)"Host:",
                            STRINGA_TextVal, (ULONG)"",
                            STRINGA_MaxChars, 64,
                            TAG_DONE);

    enabledCheckbox = NewObject(CHECKBOX_GetClass(), NULL,
                                 GA_ID, GID_ENABLED,
                                 GA_Text, (ULONG)"Enabled",
                                 CHECKBOX_Checked, FALSE,
                                 CHECKBOX_TextPlace, CHECKBOX_TEXT_RIGHT,
                                 TAG_DONE);

    Diag(connectButton != NULL ? "caapp: button obj ok\n" : "caapp: button obj FAILED\n");
    Diag(hostString != NULL ? "caapp: string obj ok\n" : "caapp: string obj FAILED\n");
    Diag(enabledCheckbox != NULL ? "caapp: checkbox obj ok\n" : "caapp: checkbox obj FAILED\n");

    if (connectButton == NULL || hostString == NULL || enabledCheckbox == NULL) {
        CleanExit(NULL, RETURN_FAIL);
    }

    mainLayout = NewObject(LAYOUT_GetClass(), NULL,
                            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                            LAYOUT_SpaceInner, TRUE,
                            LAYOUT_SpaceOuter, TRUE,
                            LAYOUT_AddChild, (ULONG)connectButton,
                            LAYOUT_AddChild, (ULONG)hostString,
                            LAYOUT_AddChild, (ULONG)enabledCheckbox,
                            TAG_DONE);

    if (mainLayout == NULL) {
        Diag("caapp: layout obj FAILED\n");
        CleanExit(NULL, RETURN_FAIL);
    }

    windowObject = NewObject(WINDOW_GetClass(), NULL,
                              WINDOW_Position, WPOS_CENTERSCREEN,
                              WA_Activate, TRUE,
                              WA_Title, (ULONG)"AmiPilot ClassAct Fixture",
                              WA_DragBar, TRUE,
                              WA_CloseGadget, TRUE,
                              WA_DepthGadget, TRUE,
                              WA_InnerWidth, 220,
                              WA_InnerHeight, 130,
                              WA_IDCMP, IDCMP_CLOSEWINDOW,
                              WINDOW_Layout, (ULONG)mainLayout,
                              TAG_DONE);

    if (windowObject == NULL) {
        Diag("caapp: window obj FAILED\n");
        CleanExit(NULL, RETURN_FAIL);
    }

    intuiWindow = (struct Window *)DoMethod(windowObject, WM_OPEN, NULL);
    if (intuiWindow == NULL) {
        Diag("caapp: WM_OPEN FAILED\n");
        CleanExit(windowObject, RETURN_FAIL);
    }

    Diag("caapp: window open, entering event loop\n");

    ProcessEvents(windowObject);

    DoMethod(windowObject, WM_CLOSE);
    CleanExit(windowObject, RETURN_OK);
    return RETURN_OK; /* unreachable, CleanExit() calls exit() */
}
