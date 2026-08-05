/*
 * AmiClickTest -- dev-only Shell command scoping the action engine
 * (server/src/action.c) against fixtures/gadtools-app. Not a fixture
 * itself, not the eventual server commodity: a throwaway-shaped, real
 * proof that AmipClickGadget() actually moves a real pointer and
 * delivers a real click through input.device, verified end-to-end under
 * Copperline. See docs/implementation-plan.md phase 0.2.
 *
 * Template: WINDOW/A,ID/N/A -- window title substring, gadget ID.
 * Finds the LIVE gadget by walking window->FirstGadget directly (not
 * through intuition-model's copied-out AmipWindowModel): a click target
 * must resolve against the current structure at action time, per the
 * plan's "Act with real input, not shortcuts" design principle -- a
 * stale copy could be clicking where a gadget used to be.
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

#define TEMPLATE "WINDOW/A,ID/N/A"

struct ClickTestArgs {
    STRPTR windowTitle;
    LONG *gadgetId;
};

static struct Window *FindWindow(CONST_STRPTR titleSubstring)
{
    struct Screen *screen = IntuitionBase->FirstScreen;
    struct Window *window;

    for (; screen != NULL; screen = screen->NextScreen) {
        for (window = screen->FirstWindow; window != NULL; window = window->NextWindow) {
            if (window->Title != NULL && strstr((const char *)window->Title, (const char *)titleSubstring) != NULL) {
                return window;
            }
        }
    }

    return NULL;
}

static struct Gadget *FindGadgetById(struct Window *window, ULONG id)
{
    struct Gadget *gadget = window->FirstGadget;

    for (; gadget != NULL; gadget = gadget->NextGadget) {
        if (gadget->GadgetID == id) {
            return gadget;
        }
    }

    return NULL;
}

/* Locate-then-act has a real gap between the two: `target` and `gadget`
 * are raw pointers with no generation counter or safe-invalidation on
 * AmigaOS, so if the window closed (or the gadget list changed) in the
 * meantime, acting on them is a dangling dereference. Re-walking the
 * live screen/window list under a brief LockIBase() hold immediately
 * before the click, checking pointer identity, doesn't close the gap
 * entirely (the window could still close in the instant after this
 * check returns and before AmipClickGadget's own DoIO() call), but it
 * does shrink it from "however long FindWindow to here took" to
 * "essentially zero" -- the real, general fix (a locate-and-act
 * verb that's atomic with respect to the target closing) is the
 * "action-scoped expectations" primitive phase 0.2 already plans to
 * build, not something this dev-only scoping tool should invent. */
static BOOL IsWindowStillOpen(struct Window *target)
{
    struct Screen *screen;
    struct Window *window;
    BOOL found = FALSE;

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

    target = FindWindow(args.windowTitle);
    if (target == NULL) {
        fprintf(stderr, "AmiClickTest: no matching window found\n");
        rc = RETURN_WARN;
    } else {
        gadget = FindGadgetById(target, (ULONG)*args.gadgetId);
        if (gadget == NULL) {
            fprintf(stderr, "AmiClickTest: no gadget with id=%ld in that window\n", (long)*args.gadgetId);
            rc = RETURN_WARN;
        } else {
            printf("AmiClickTest: clicking gadget id=%ld at window-relative [%d,%d %dx%d], window at [%d,%d]\n",
                   (long)*args.gadgetId, gadget->LeftEdge, gadget->TopEdge, gadget->Width, gadget->Height,
                   target->LeftEdge, target->TopEdge);

            if (!IsWindowStillOpen(target)) {
                fprintf(stderr, "AmiClickTest: window closed between locate and click, aborting\n");
                rc = RETURN_WARN;
            } else if (!AmipClickGadget(target, gadget)) {
                fprintf(stderr, "AmiClickTest: click failed\n");
                rc = RETURN_FAIL;
            } else {
                printf("AmiClickTest: click delivered\n");
            }
        }
    }

    AmipActionShutdown();
    FreeArgs(rdargs);
    CloseLibrary((struct Library *)IntuitionBase);
    return rc;
}
