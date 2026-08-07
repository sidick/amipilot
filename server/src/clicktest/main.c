/*
 * AmiClickTest -- dev-only Shell command scoping the action engine
 * (server/src/action.c) against fixtures/gadtools-app. Not a fixture
 * itself, not the eventual server commodity: a throwaway-shaped, real
 * proof that AmipClickGadget() actually moves a real pointer and
 * delivers a real click through input.device, verified end-to-end under
 * Copperline. See docs/implementation-plan.md phase 0.2.
 *
 * Template: WINDOW/A,ID/N/A,TEXT/K -- window title substring, gadget ID,
 * and optionally text to type (AmipTypeString) after the click lands --
 * click a string gadget then TEXT fills it, the same way a human would.
 * Locator lookups (AmipFindWindow/AmipFindGadgetById/AmipIsWindowOpen)
 * live in action_engine.h now -- see its "locators" section for why they
 * walk live structure rather than intuition-model's copied-out model.
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <libraries/dos.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <stdio.h>
#include <string.h>

#include "action_engine.h"

struct IntuitionBase *IntuitionBase = NULL;
/* Consumed by AmipTypeString() (MapANSI); explicitly initialized -- an
 * uninitialized library-base global is a COMMON symbol and pulls in
 * libnix's auto-open constructor, see CLAUDE.md. */
struct Library *KeymapBase = NULL;

#define TEMPLATE "WINDOW/A,ID/N/A,TEXT/K"

struct ClickTestArgs {
    STRPTR windowTitle;
    LONG *gadgetId;
    STRPTR text;
};

int main(void)
{
    struct RDArgs *rdargs;
    struct ClickTestArgs args;
    struct Window *target;
    struct Gadget *gadget;
    int rc = RETURN_OK;

    memset(&args, 0, sizeof(args));

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 37);
    if (IntuitionBase == NULL) {
        fprintf(stderr, "AmiClickTest: requires intuition.library V37 or newer\n");
        return RETURN_FAIL;
    }

    /* Optional -- only TEXT typing needs it; AmipTypeString() reports
     * failure itself if it's absent. */
    KeymapBase = OpenLibrary((CONST_STRPTR)"keymap.library", 37);

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, (LONG *)&args, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"AmiClickTest");
        CloseLibrary((struct Library *)IntuitionBase);
        return RETURN_FAIL;
    }

    if (!AmipActionInit()) {
        fprintf(stderr, "AmiClickTest: AmipActionInit failed (no input.device?)\n");
        FreeArgs(rdargs);
        CloseLibrary((struct Library *)IntuitionBase);
        return RETURN_FAIL;
    }

    target = AmipFindWindow(NULL, (CONST_STRPTR)args.windowTitle);
    if (target == NULL) {
        fprintf(stderr, "AmiClickTest: no matching window found\n");
        rc = RETURN_WARN;
    } else {
        gadget = AmipFindGadgetById(target, (ULONG)*args.gadgetId);
        if (gadget == NULL) {
            fprintf(stderr, "AmiClickTest: no gadget with id=%ld in that window\n", (long)*args.gadgetId);
            rc = RETURN_WARN;
        } else {
            printf("AmiClickTest: clicking gadget id=%ld at window-relative [%d,%d %dx%d], window at [%d,%d]\n",
                   (long)*args.gadgetId, gadget->LeftEdge, gadget->TopEdge, gadget->Width, gadget->Height,
                   target->LeftEdge, target->TopEdge);

            if (!AmipIsWindowOpen(target)) {
                fprintf(stderr, "AmiClickTest: window closed between locate and click, aborting\n");
                rc = RETURN_WARN;
            } else if (!AmipClickGadget(target, gadget)) {
                fprintf(stderr, "AmiClickTest: click failed\n");
                rc = RETURN_FAIL;
            } else {
                printf("AmiClickTest: click delivered\n");
                if (args.text != NULL) {
                    if (!AmipTypeString((CONST_STRPTR)args.text)) {
                        fprintf(stderr, "AmiClickTest: typing failed\n");
                        rc = RETURN_FAIL;
                    } else {
                        printf("AmiClickTest: typed \"%s\"\n", (const char *)args.text);
                    }
                }
            }
        }
    }

    AmipActionShutdown();
    FreeArgs(rdargs);
    if (KeymapBase != NULL) {
        CloseLibrary(KeymapBase);
    }
    CloseLibrary((struct Library *)IntuitionBase);
    return rc;
}
