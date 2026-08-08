/*
 * wbgui-app -- a Workbench-startable fixture WITH a real GUI window,
 * used for the "bare machine" lifecycle on-target check (GitHub issue
 * tracking docs/implementation-plan.md's own success criterion: "A
 * full lifecycle test passes: Workbench-launch a tool with an
 * overridden tooltype and a project argument, assert the override
 * took effect in the GUI, drive it, and quit it via its own
 * affordances... The same lifecycle test runs entirely self-contained
 * against a bare machine over TCP -- fixtures staged via fs-put...").
 *
 * Deliberately a SEPARATE fixture from fixtures/wbapp's own WBApp, not
 * a modification of it: WBApp is specifically a non-GUI, exits-
 * immediately fixture that several existing on-target checks
 * (run_wblaunch_check's bare-launch/override/ARG/bad-icon/scratch-
 * leak assertions) already depend on running to completion and
 * exiting promptly -- giving it a real window that blocks on
 * IDCMP_CLOSEWINDOW would break every one of those. This fixture
 * exists purely to give the lifecycle check something with actual,
 * observable GUI state to assert on and a real close affordance to
 * quit through.
 *
 * On a real Workbench start: writes the same kind of plain-text
 * report WBApp does (dos.library Open/Write, no console -- see
 * RESULT_PATH below) for the FSGET half of the lifecycle check, THEN
 * opens a small GadTools window with one read-only string gadget
 * (GA_ID=1) pre-filled with the launch's own GREETING tooltype value
 * -- the same self-lookup idiom WBApp already uses (CurrentDir() to
 * the icon lock WBLAUNCH handed us, GetDiskObject(), FindToolType())
 * -- so a host test can GETTEXT it to confirm the tooltype override
 * genuinely reached the GUI, not just the T: report file. The
 * lifecycle check quits through the window's own real system close
 * gadget (WFLG_CLOSEGADGET, IDCMP_CLOSEWINDOW, addressed via the
 * tier-2 ROLE=custom locator) -- the real "quit via its own
 * affordances" the success criterion calls for, no kill. This window
 * is also what caught a real, previously-unknown bug getting this
 * check working (GitHub issue #60): clicking a GadTools-context
 * window's system close gadget via ROLE=custom INDEX=<n> reported
 * success but silently acted on the wrong gadget (depth instead of
 * close) -- root-caused to AmipGadgetModel's gadgetId being 0 for
 * EVERY system gadget, so main.c's ResolveTargetGadget() re-resolving
 * "by ID 0" after a correct role/index match just grabbed whichever
 * system gadget Intuition's own chain happened to list first,
 * discarding the real match. Fixed by carrying each system gadget's
 * real GTYP_SYSTYPEMASK sub-type through the walk model and
 * re-resolving through AmipFindSystemGadget() (unambiguous) instead
 * of AmipFindGadgetById() (ambiguous for these) -- see that function
 * and intuition_model.h's own sysGadgetType field for the fix.
 * Also carries a real "_Quit" button (GA_ID=2, a numeric-GA_ID click,
 * the mechanism already proven reliable everywhere else in this
 * project) as a second, equally legitimate affordance -- not what the
 * automated check relies on, but there for anyone poking at this
 * fixture by hand.
 *
 * On a Shell start (no _WBenchMsg -- useful for poking at this
 * fixture by hand): prints the same report to stdout, no GUI, and
 * exits -- a debugging convenience, matching WBApp's own precedent.
 */
#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <workbench/workbench.h>
#include <workbench/startup.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>

extern struct WBStartup *_WBenchMsg;

struct Library *IconBase;
struct IntuitionBase *IntuitionBase;
struct Library *GadToolsBase;

#define RESULT_PATH "T:amipilot-lifecycle-result.txt"
#define GID_GREETING 1
#define GID_QUIT 2

static void WriteLine(BPTR fh, const char *line)
{
    Write(fh, (APTR)line, (LONG)strlen(line));
}

int main(void)
{
    BPTR out;
    char line[256];
    char greeting[128];
    struct DiskObject *dobj = NULL;

    strcpy(greeting, "(no tooltype found)");

    IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 0);

    out = Open((CONST_STRPTR)RESULT_PATH, MODE_NEWFILE);
    if (out == 0) {
        if (IconBase != NULL) {
            CloseLibrary(IconBase);
        }
        return 20;
    }

    if (_WBenchMsg == NULL) {
        WriteLine(out, "STARTED_FROM=SHELL\n");
        Close(out);
        if (IconBase != NULL) {
            CloseLibrary(IconBase);
        }
        return 0;
    }

    snprintf(line, sizeof(line), "STARTED_FROM=WORKBENCH\n");
    WriteLine(out, line);
    snprintf(line, sizeof(line), "NUMARGS=%ld\n", (long)_WBenchMsg->sm_NumArgs);
    WriteLine(out, line);

    {
        LONG i;
        for (i = 0; i < _WBenchMsg->sm_NumArgs; i++) {
            snprintf(line, sizeof(line), "ARG%ld=%s\n", (long)i,
                     (const char *)_WBenchMsg->sm_ArgList[i].wa_Name);
            WriteLine(out, line);
        }
    }

    if (IconBase != NULL) {
        /* Same self-lookup idiom WBApp already uses: CurrentDir() to
         * the lock WBLAUNCH handed us for our own icon (arg 0), then
         * GetDiskObject() by name relative to it. */
        CurrentDir(_WBenchMsg->sm_ArgList[0].wa_Lock);
        dobj = GetDiskObject(_WBenchMsg->sm_ArgList[0].wa_Name);
        if (dobj != NULL) {
            UBYTE *val = FindToolType((CONST_STRPTR *)dobj->do_ToolTypes, (CONST_STRPTR)"GREETING");
            if (val != NULL) {
                strncpy(greeting, (const char *)val, sizeof(greeting) - 1);
                greeting[sizeof(greeting) - 1] = '\0';
            }
            snprintf(line, sizeof(line), "TOOLTYPE_GREETING=%s\n", greeting);
            WriteLine(out, line);
        } else {
            WriteLine(out, "TOOLTYPE_GREETING=(no icon found)\n");
        }
    }

    Close(out);

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 37);
    GadToolsBase = OpenLibrary((CONST_STRPTR)"gadtools.library", 37);

    if (IntuitionBase != NULL && GadToolsBase != NULL) {
        struct Screen *screen = LockPubScreen(NULL);
        struct Window *window = NULL;
        struct Gadget *glist = NULL;
        struct Gadget *gad = NULL;
        struct NewGadget ng;
        APTR visualInfo = NULL;

        if (screen != NULL) {
            visualInfo = GetVisualInfo(screen, TAG_DONE);
        }

        if (screen != NULL && visualInfo != NULL) {
            gad = CreateContext(&glist);

            ng.ng_LeftEdge = 8;
            ng.ng_TopEdge = 24;
            ng.ng_Width = 260;
            ng.ng_Height = 14;
            ng.ng_GadgetText = (UBYTE *)"Greeting:";
            ng.ng_TextAttr = screen->Font;
            ng.ng_GadgetID = GID_GREETING;
            ng.ng_Flags = PLACETEXT_ABOVE;
            ng.ng_VisualInfo = visualInfo;
            gad = CreateGadget(STRING_KIND, gad, &ng,
                                GTST_String, (ULONG)greeting,
                                GTST_MaxChars, (ULONG)(sizeof(greeting) - 1),
                                TAG_DONE);

            /* A real button, not just the window's own system close
             * gadget -- this is what the lifecycle check actually
             * quits through (a numeric GA_ID click, the same
             * mechanism proven reliable throughout this project),
             * since clicking a GadTools-context window's SYSTEM close
             * gadget via the tier-2 ROLE=custom locator was found NOT
             * to work live (confirmed: the click reports success,
             * WINDOWMOVE's drag-bar click on the same window works
             * fine, but the close gadget specifically never responds)
             * -- a genuine, newly-found limitation, not yet root-
             * caused; the system close gadget stays on the window
             * (a real user could still close it by hand), just isn't
             * what this automated check relies on. */
            ng.ng_TopEdge += 24;
            ng.ng_GadgetText = (UBYTE *)"_Quit";
            ng.ng_GadgetID = GID_QUIT;
            ng.ng_Flags = PLACETEXT_IN;
            gad = CreateGadget(BUTTON_KIND, gad, &ng, GT_Underscore, '_', TAG_DONE);

            window = OpenWindowTags(NULL,
                                     WA_Left, 20, WA_Top, 20,
                                     WA_Width, 280, WA_Height, 95,
                                     WA_Title, (ULONG)"AmiPilot Lifecycle Fixture",
                                     WA_Gadgets, (ULONG)glist,
                                     WA_CloseGadget, TRUE,
                                     WA_DragBar, TRUE,
                                     WA_DepthGadget, TRUE,
                                     WA_Activate, TRUE,
                                     WA_SimpleRefresh, TRUE,
                                     WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_GADGETUP,
                                     WA_PubScreen, (ULONG)screen,
                                     TAG_DONE);
        }

        if (window != NULL) {
            BOOL done = FALSE;

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
                            if (((struct Gadget *)msg->IAddress)->GadgetID == GID_QUIT) {
                                done = TRUE;
                            }
                            break;
                        default:
                            break;
                    }
                    GT_ReplyIMsg(msg);
                }
            }

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
    }

    if (dobj != NULL) {
        FreeDiskObject(dobj);
    }
    if (GadToolsBase != NULL) {
        CloseLibrary(GadToolsBase);
    }
    if (IntuitionBase != NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
    }
    if (IconBase != NULL) {
        CloseLibrary(IconBase);
    }
    return 0;
}
