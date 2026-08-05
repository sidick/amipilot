/*
 * AmiPilotServer -- the server commodity (phase 0.2). Hosts the action
 * engine (server/src/action.c) and intuition-model's walker behind a
 * genuine public ARexx port ("AMIPILOT.<n>"), so an ARexx script running
 * on the SAME Amiga can locate and drive another program's GUI -- no
 * host, transport, or emulator involved. See
 * docs/implementation-plan.md's phase 0.2 release gate: "an ARexx script
 * clicks a button on the test app and asserts [state] changed."
 *
 * Verb set (arexx_cmd.h): TREE/CLICK/TYPE/GETTEXT/QUIT -- a small, real
 * subset of the plan's full v1 verb list; wire protocol, launch, fs, and
 * menu/drag verbs are 0.3/0.4 scope, not invented here ahead of need.
 *
 * Runs as an ordinary CLI/Shell process, not (yet) a commodities.library
 * broker -- AmipClickGadget() already brings its own target window/
 * screen forward, so there's no icon-in-the-Exchange-list requirement
 * this phase needs to satisfy.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <intuition/intuition.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/rexxsyslib.h>
#include <stdio.h>
#include <string.h>

#include "action_engine.h"
#include "arexx.h"
#include "intuition_model.h"

struct IntuitionBase *IntuitionBase = NULL;
/* Consumed by intuition-model/walk.c's BUTTON_KIND/CHECKBOX_KIND
 * discriminator (GT_GetGadgetAttrsA) -- optional, same graceful
 * degradation as AmiInspect: absent just means that one discrimination
 * degrades to a guess, not a failure to run. */
struct Library *GadToolsBase = NULL;
/* Consumed by AmipTypeString() (MapANSI). Optional -- TYPE just fails
 * (AMIP_AREXX_RC_FAIL) if it's absent; TREE/CLICK/GETTEXT don't need it. */
struct Library *KeymapBase = NULL;

#define AMIP_TREE_BUF_SIZE 4096
#define AMIP_RESULT_BUF_SIZE 320

/* Appends one gadget's line to buf in the same shape AmiInspect's
 * PrintModel prints it, tracking remaining space so a window with more
 * gadgets than fit just truncates rather than overflowing. */
static void AppendGadgetLine(char *buf, size_t cap, const AmipGadgetModel *gadget)
{
    size_t used = strlen(buf);
    if (used >= cap - 1) {
        return;
    }
    snprintf(buf + used, cap - used, "  gadget id=%lu role=%s class=\"%s\" label=\"%s\"",
             (unsigned long)gadget->gadgetId, AmipRoleName(gadget->role),
             gadget->className != NULL ? (const char *)gadget->className : "",
             gadget->label != NULL ? (const char *)gadget->label : "");
    if (gadget->value != NULL) {
        used = strlen(buf);
        snprintf(buf + used, cap - used, " value=\"%s\"", (const char *)gadget->value);
    }
    used = strlen(buf);
    snprintf(buf + used, cap - used, " [%d,%d %dx%d]\n",
             gadget->left, gadget->top, gadget->width, gadget->height);
}

static void BuildTreeResult(const AmipWindowModel *model, char *buf, size_t cap)
{
    const AmipGadgetModel *gadget;

    buf[0] = '\0';
    snprintf(buf, cap, "window \"%s\" [%d,%d %dx%d]\n",
             model->title != NULL ? (const char *)model->title : "(untitled)",
             model->left, model->top, model->width, model->height);

    for (gadget = model->gadgets; gadget != NULL; gadget = gadget->next) {
        AppendGadgetLine(buf, cap, gadget);
    }
}

/* Text a script can assert on for one gadget: its live value (string/
 * integer gadgets) if it has one, else its label -- covers both "did the
 * string field get typed into" and "did a label change" without the
 * caller needing to know which. */
static BOOL FindGadgetText(struct Window *window, ULONG gadgetId, char *buf, size_t cap)
{
    AmipWindowModel *model = AmipWalkWindow(window);
    const AmipGadgetModel *gadget;
    BOOL found = FALSE;

    if (model == NULL) {
        return FALSE;
    }

    for (gadget = model->gadgets; gadget != NULL; gadget = gadget->next) {
        if (gadget->gadgetId == gadgetId) {
            const STRPTR text = gadget->value != NULL ? gadget->value : gadget->label;
            strncpy(buf, text != NULL ? (const char *)text : "", cap - 1);
            buf[cap - 1] = '\0';
            found = TRUE;
            break;
        }
    }

    AmipFreeWindowModel(model);
    return found;
}

static int RealMain(void)
{
    struct MsgPort *arexxPort;
    char portName[32];
    ULONG rexxSig;
    BOOL running = TRUE;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 37);
    if (IntuitionBase == NULL) {
        fprintf(stderr, "AmiPilotServer: requires intuition.library V37 or newer\n");
        return RETURN_FAIL;
    }
    GadToolsBase = OpenLibrary((CONST_STRPTR)"gadtools.library", 37);
    KeymapBase = OpenLibrary((CONST_STRPTR)"keymap.library", 37);
    RexxSysBase = (struct RxsLib *)OpenLibrary((CONST_STRPTR)"rexxsyslib.library", 0);

    if (RexxSysBase == NULL) {
        fprintf(stderr, "AmiPilotServer: requires rexxsyslib.library\n");
        goto cleanup;
    }
    if (!AmipActionInit()) {
        fprintf(stderr, "AmiPilotServer: AmipActionInit failed (no input.device?)\n");
        goto cleanup;
    }

    arexxPort = AmipArexxOpen(NULL, portName, sizeof(portName));
    if (arexxPort == NULL) {
        fprintf(stderr, "AmiPilotServer: could not open an ARexx port\n");
        AmipActionShutdown();
        goto cleanup;
    }
    printf("AmiPilotServer: ARexx port %s ready\n", portName);
    fflush(stdout);

    rexxSig = 1UL << arexxPort->mp_SigBit;

    while (running) {
        ULONG sigs = Wait(rexxSig | SIGBREAKF_CTRL_C);

        if (sigs & SIGBREAKF_CTRL_C) {
            running = FALSE;
        }

        if (sigs & rexxSig) {
            void *handle;
            AmipArexxParsed cmd;

            while ((handle = AmipArexxReceive(arexxPort, &cmd)) != NULL) {
                int rc = AMIP_AREXX_RC_OK;
                /* static, not stack-allocated: a Shell-launched process's
                 * default stack is small (the classic AmigaDOS "STACK="
                 * you'd otherwise have to remember to set on every launch
                 * of this commodity), and treeBuf+resultBuf alone
                 * approach 4.5KB -- confirmed the hard way, 2026-08-05:
                 * silently overflowed the default stack, corrupting
                 * something past the crash's own frame such that the
                 * FIRST ARexx command handled fine (its own reply went
                 * out correctly) but the process took an illegal-
                 * instruction exception (Guru #80000004) before the
                 * second could be processed -- `rx` then reported "Host
                 * environment not found" for every later command, since
                 * the crashed task was never servicing the port again,
                 * a confusing downstream symptom of a stack overflow
                 * that had nothing to do with ARexx port lookup itself.
                 * Only one message is ever in flight at a time (this is
                 * a single-threaded, one-message-processed-at-once event
                 * loop), so `static` here is exactly as safe as
                 * dynamically allocating and freeing per message, at
                 * zero cost. */
                static char resultBuf[AMIP_RESULT_BUF_SIZE];
                static char treeBuf[AMIP_TREE_BUF_SIZE];
                const char *result = NULL;

                resultBuf[0] = '\0';

                switch (cmd.type) {
                    case AMIP_AREXX_CMD_TREE: {
                        struct Window *w = AmipFindWindow((CONST_STRPTR)cmd.windowPattern);
                        AmipWindowModel *model;

                        if (w == NULL) {
                            rc = AMIP_AREXX_RC_WARN;
                            break;
                        }
                        model = AmipWalkWindow(w);
                        if (model == NULL) {
                            rc = AMIP_AREXX_RC_FAIL;
                            break;
                        }
                        BuildTreeResult(model, treeBuf, sizeof(treeBuf));
                        AmipFreeWindowModel(model);
                        result = treeBuf;
                        break;
                    }

                    case AMIP_AREXX_CMD_CLICK: {
                        struct Window *w = AmipFindWindow((CONST_STRPTR)cmd.windowPattern);
                        struct Gadget *g;

                        if (w == NULL) {
                            rc = AMIP_AREXX_RC_WARN;
                            break;
                        }
                        g = AmipFindGadgetById(w, (ULONG)cmd.gadgetId);
                        if (g == NULL || !AmipIsWindowOpen(w)) {
                            rc = AMIP_AREXX_RC_WARN;
                            break;
                        }
                        if (!AmipClickGadget(w, g)) {
                            rc = AMIP_AREXX_RC_FAIL;
                        }
                        break;
                    }

                    case AMIP_AREXX_CMD_TYPE: {
                        struct Window *w = AmipFindWindow((CONST_STRPTR)cmd.windowPattern);
                        struct Gadget *g;

                        if (w == NULL) {
                            rc = AMIP_AREXX_RC_WARN;
                            break;
                        }
                        g = AmipFindGadgetById(w, (ULONG)cmd.gadgetId);
                        if (g == NULL || !AmipIsWindowOpen(w)) {
                            rc = AMIP_AREXX_RC_WARN;
                            break;
                        }
                        if (!AmipClickGadget(w, g) || !AmipTypeString((CONST_STRPTR)cmd.text)) {
                            rc = AMIP_AREXX_RC_FAIL;
                        }
                        break;
                    }

                    case AMIP_AREXX_CMD_GETTEXT: {
                        struct Window *w = AmipFindWindow((CONST_STRPTR)cmd.windowPattern);

                        if (w == NULL) {
                            rc = AMIP_AREXX_RC_WARN;
                            break;
                        }
                        if (!FindGadgetText(w, (ULONG)cmd.gadgetId, resultBuf, sizeof(resultBuf))) {
                            rc = AMIP_AREXX_RC_WARN;
                            break;
                        }
                        result = resultBuf;
                        break;
                    }

                    case AMIP_AREXX_CMD_QUIT:
                        running = FALSE;
                        break;

                    case AMIP_AREXX_CMD_UNKNOWN:
                    default:
                        rc = AMIP_AREXX_RC_ERROR;
                        break;
                }

                AmipArexxReply(handle, rc, result);
            }
        }
    }

    AmipArexxClose(arexxPort);
    AmipActionShutdown();

cleanup:
    if (RexxSysBase != NULL) {
        CloseLibrary((struct Library *)RexxSysBase);
    }
    if (KeymapBase != NULL) {
        CloseLibrary(KeymapBase);
    }
    if (GadToolsBase != NULL) {
        CloseLibrary(GadToolsBase);
    }
    CloseLibrary((struct Library *)IntuitionBase);
    return RETURN_OK;
}

/* A Shell-launched process's default AmigaDOS stack is small, and this
 * commodity's dispatch loop (TREE especially, walking a whole window's
 * gadget tree into a result string) has real depth: intuition-model's
 * recursive-shaped copy-out, this file's own switch dispatch, and
 * (before 2026-08-05) two now-static result buffers that used to
 * silently overflow it -- see server/README.md's stack note for exactly
 * how confusing the resulting failure looked (a crashed task, but `rx`
 * reporting "Host environment not found" against a port that measurably
 * existed moments earlier). Making the two known-large buffers `static`
 * fixed that specific overflow, but StackSwap() (V37, this project's own
 * floor -- exec.doc) is the general fix: give the whole call tree
 * genuine headroom up front rather than accounting for every future
 * local by hand. Swapping happens in this thin wrapper, never inside
 * RealMain() itself -- StackSwap() only repoints the stack pointer, it
 * doesn't relocate or copy anything already on the old stack, so a
 * function's own locals must all live on ONE side of the swap; calling
 * into a separate function after swapping is what makes that clean,
 * rather than trying to keep using this function's own (old-stack)
 * locals afterward. */
#define AMIP_STACK_SIZE 32000

int main(void)
{
    struct StackSwapStruct stackSwap;
    APTR newStack;
    int rc;

    newStack = AllocMem(AMIP_STACK_SIZE, MEMF_PUBLIC);
    if (newStack == NULL) {
        fprintf(stderr, "AmiPilotServer: could not allocate a %d-byte stack\n", AMIP_STACK_SIZE);
        return RETURN_FAIL;
    }

    stackSwap.stk_Lower = newStack;
    stackSwap.stk_Upper = (ULONG)newStack + AMIP_STACK_SIZE;
    stackSwap.stk_Pointer = (APTR)((ULONG)newStack + AMIP_STACK_SIZE);

    StackSwap(&stackSwap);
    rc = RealMain();
    StackSwap(&stackSwap); /* restore the original stack before freeing the swapped-out one */

    FreeMem(newStack, AMIP_STACK_SIZE);
    return rc;
}
