/*
 * AmiPilotServer -- the server commodity (phase 0.2). Hosts the action
 * engine (server/src/action.c) and intuition-model's walker behind a
 * genuine public ARexx port ("AMIPILOT.<n>"), so an ARexx script running
 * on the SAME Amiga can locate and drive another program's GUI -- no
 * host, transport, or emulator involved. See
 * docs/implementation-plan.md's phase 0.2 release gate: "an ARexx script
 * clicks a button on the test app and asserts [state] changed."
 *
 * Verb set (arexx_cmd.h): TREE/CLICK/TYPE/GETTEXT/MANIFEST/VERSION/QUIT
 * -- a small, real subset of the plan's full v1 verb list; launch, fs,
 * and menu/drag verbs are 0.4 scope, not invented here ahead of need.
 *
 * Phase 0.3 adds the wire: the same verb grammar over serial.device
 * (SERIAL switch), framed per server/WIRE.md -- LF-terminated request
 * lines in, "RC <code> <byte-count>\n" + payload out. One parser
 * (arexx_cmd.c) and one dispatch (HandleCommand below) serve both
 * transports; the serial layer (serial.c) only moves bytes and lines.
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
#include "manifest.h"
#include "serial.h"

#define STR(s)  #s
#define XSTR(s) STR(s)

#ifndef VERSION
#define VERSION 0
#endif
#ifndef REVISION
#define REVISION 0
#endif

/* Standard AmigaDOS "$VER:" version cookie -- see amiinspect/src/main.c's
 * copy of this same pattern for the full rationale (RKRM: DOS, "The
 * Version Cookie"; `static volatile` so -O2 can't drop the
 * never-read-in-program array). version.mk is the single source of
 * truth for VERSION/REVISION. */
static volatile char version[] =
    "$VER: AmiPilotServer " XSTR(VERSION) "." XSTR(REVISION) " (06.08.2026)";

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
#define AMIP_MANIFEST_FILE_MAX 8192

/* The currently-loaded manifest (MANIFEST command). One at a time --
 * loading a new one replaces the old, matching how a test session
 * actually works (one application under test at a time); driving two
 * manifest-carrying apps at once is 0.3+ scope if it's ever needed. */
static AmipManifest g_manifest;
static BOOL g_manifestLoaded = FALSE;

/* Reads path into a static buffer and parses it into g_manifest.
 * Returns an AMIP_AREXX_RC_* code; on any failure the previously-loaded
 * manifest is forgotten rather than left half-replaced, and errOut
 * (cap errCap) gets a one-line reason. */
static int LoadManifest(const char *path, char *errOut, int errCap)
{
    static char fileBuf[AMIP_MANIFEST_FILE_MAX];
    FILE *f;
    size_t n;

    g_manifestLoaded = FALSE;

    f = fopen(path, "r");
    if (f == NULL) {
        snprintf(errOut, errCap, "cannot open %s", path);
        return AMIP_AREXX_RC_FAIL;
    }
    n = fread(fileBuf, 1, sizeof(fileBuf) - 1, f);
    fclose(f);
    if (n >= sizeof(fileBuf) - 1) {
        snprintf(errOut, errCap, "manifest larger than %d bytes", AMIP_MANIFEST_FILE_MAX);
        return AMIP_AREXX_RC_FAIL;
    }
    fileBuf[n] = '\0';

    if (AmipManifestParse(fileBuf, &g_manifest, errOut, errCap) != 0) {
        return AMIP_AREXX_RC_ERROR;
    }

    g_manifestLoaded = TRUE;
    return AMIP_AREXX_RC_OK;
}

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

/* static, not stack-allocated: a Shell-launched process's default stack
 * is small and treeBuf+resultBuf approach 4.5KB -- confirmed the hard
 * way, 2026-08-05: as locals they silently overflowed the default
 * stack, corrupting state such that the FIRST ARexx command handled
 * fine but the process took an illegal-instruction exception (Guru
 * #80000004) before the second -- `rx` then reported a confusing "Host
 * environment not found" for every later command, since the crashed
 * task was never servicing the port again. Only one command is ever
 * dispatched at a time (single-threaded event loop, both transports),
 * so file-static is exactly as safe as per-message allocation, at zero
 * cost. Don't move these back into a function. */
static char g_resultBuf[AMIP_RESULT_BUF_SIZE];
static char g_treeBuf[AMIP_TREE_BUF_SIZE];

/* Executes one parsed command -- the single dispatch both transports
 * share (ARexx RESULT string and wire payload are the same bytes; see
 * server/WIRE.md). Returns the AMIP_AREXX_RC_* code, points *resultOut
 * at the payload (NULL/empty for none; valid until the next call --
 * file-static buffers above), and clears *runningOut on QUIT. */
static int HandleCommand(AmipArexxParsed *cmd, const char **resultOut,
                         BOOL *runningOut)
{
    int rc = AMIP_AREXX_RC_OK;
    const char *result = NULL;

    g_resultBuf[0] = '\0';

    /* "@name" locator: resolve against the loaded manifest into the
     * same windowPattern/gadgetId fields the classic form fills, so the
     * verb handlers below run identically for both. Unknown name / no
     * manifest loaded are both script errors (RC 10), same class as a
     * bad argument. */
    if (cmd->manifestName[0] != '\0') {
        const char *title;
        long id;

        if (!g_manifestLoaded) {
            strncpy(g_resultBuf, "no manifest loaded", sizeof(g_resultBuf) - 1);
            *resultOut = g_resultBuf;
            return AMIP_AREXX_RC_ERROR;
        }
        if (AmipManifestResolve(&g_manifest, cmd->manifestName, &title, &id) != 0) {
            snprintf(g_resultBuf, sizeof(g_resultBuf),
                     "no such name in manifest: %s", cmd->manifestName);
            *resultOut = g_resultBuf;
            return AMIP_AREXX_RC_ERROR;
        }
        strncpy(cmd->windowPattern, title, sizeof(cmd->windowPattern) - 1);
        cmd->windowPattern[sizeof(cmd->windowPattern) - 1] = '\0';
        cmd->gadgetId = id;
    }

    switch (cmd->type) {
        case AMIP_AREXX_CMD_MANIFEST:
            rc = LoadManifest(cmd->path, g_resultBuf, sizeof(g_resultBuf));
            if (rc == AMIP_AREXX_RC_OK) {
                snprintf(g_resultBuf, sizeof(g_resultBuf),
                         "loaded %s: %d windows, %d gadgets",
                         g_manifest.appName, g_manifest.windowCount,
                         g_manifest.gadgetCount);
            }
            result = g_resultBuf;
            break;

        case AMIP_AREXX_CMD_VERSION:
            /* The handshake payload, byte-identical on both transports
             * -- shape and stable/experimental split per server/WIRE.md
             * (everything but VERSION itself is experimental until the
             * 1.0 promotion pass). */
            snprintf(g_resultBuf, sizeof(g_resultBuf),
                     "AMIPILOT " XSTR(VERSION) "." XSTR(REVISION) " PROTOCOL 1\n"
                     "STABLE VERSION\n"
                     "EXPERIMENTAL TREE CLICK TYPE GETTEXT MANIFEST QUIT\n");
            result = g_resultBuf;
            break;

        case AMIP_AREXX_CMD_TREE: {
            struct Window *w = AmipFindWindow((CONST_STRPTR)cmd->windowPattern);
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
            BuildTreeResult(model, g_treeBuf, sizeof(g_treeBuf));
            AmipFreeWindowModel(model);
            result = g_treeBuf;
            break;
        }

        case AMIP_AREXX_CMD_CLICK: {
            struct Window *w = AmipFindWindow((CONST_STRPTR)cmd->windowPattern);
            struct Gadget *g;

            if (w == NULL) {
                rc = AMIP_AREXX_RC_WARN;
                break;
            }
            g = AmipFindGadgetById(w, (ULONG)cmd->gadgetId);
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
            struct Window *w = AmipFindWindow((CONST_STRPTR)cmd->windowPattern);
            struct Gadget *g;

            if (w == NULL) {
                rc = AMIP_AREXX_RC_WARN;
                break;
            }
            g = AmipFindGadgetById(w, (ULONG)cmd->gadgetId);
            if (g == NULL || !AmipIsWindowOpen(w)) {
                rc = AMIP_AREXX_RC_WARN;
                break;
            }
            if (!AmipClickGadget(w, g) || !AmipTypeString((CONST_STRPTR)cmd->text)) {
                rc = AMIP_AREXX_RC_FAIL;
            }
            break;
        }

        case AMIP_AREXX_CMD_GETTEXT: {
            struct Window *w = AmipFindWindow((CONST_STRPTR)cmd->windowPattern);

            if (w == NULL) {
                rc = AMIP_AREXX_RC_WARN;
                break;
            }
            if (!FindGadgetText(w, (ULONG)cmd->gadgetId, g_resultBuf, sizeof(g_resultBuf))) {
                rc = AMIP_AREXX_RC_WARN;
                break;
            }
            result = g_resultBuf;
            break;
        }

        case AMIP_AREXX_CMD_QUIT:
            *runningOut = FALSE;
            break;

        case AMIP_AREXX_CMD_UNKNOWN:
        default:
            strncpy(g_resultBuf, "unknown command or bad arguments",
                    sizeof(g_resultBuf) - 1);
            result = g_resultBuf;
            rc = AMIP_AREXX_RC_ERROR;
            break;
    }

    *resultOut = result;
    return rc;
}

/* ReadArgs template: SERIAL fits the wire transport (server/WIRE.md)
 * alongside the always-on ARexx port; SERDEVICE/SERUNIT pick the device
 * (default serial.device unit 0 -- a multi-port card's driver slots in
 * by name), BAUD the line rate (default 19200: conservative for the
 * plain-68000 floor; both ends must simply agree, and under an
 * emulator's TCP bridge the guest-side number is inert anyway). */
#define AMIP_ARG_TEMPLATE "SERIAL/S,SERDEVICE/K,SERUNIT/K/N,BAUD/K/N"
enum { ARG_SERIAL, ARG_SERDEVICE, ARG_SERUNIT, ARG_BAUD, ARG_COUNT };

static int RealMain(void)
{
    struct MsgPort *arexxPort;
    struct RDArgs *rdargs;
    LONG argArray[ARG_COUNT] = { 0, 0, 0, 0 };
    AmipSerial *serial = NULL;
    char portName[32];
    ULONG rexxSig, serialSig = 0;
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

    /* SERIAL requested but unopenable is fatal, not a degraded start: a
     * host-driven test session must never silently run without its
     * transport (same never-substitute philosophy as the emulator's own
     * bridge failures). */
    rdargs = ReadArgs((CONST_STRPTR)AMIP_ARG_TEMPLATE, argArray, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"AmiPilotServer");
        AmipArexxClose(arexxPort);
        AmipActionShutdown();
        goto cleanup;
    }
    if (argArray[ARG_SERIAL]) {
        const char *device = argArray[ARG_SERDEVICE] != 0
            ? (const char *)argArray[ARG_SERDEVICE] : "serial.device";
        LONG unit = argArray[ARG_SERUNIT] != 0
            ? *(LONG *)argArray[ARG_SERUNIT] : 0;
        LONG baud = argArray[ARG_BAUD] != 0
            ? *(LONG *)argArray[ARG_BAUD] : 19200;
        char serErr[80];

        serial = AmipSerialOpen(device, unit, baud, serErr, sizeof(serErr));
        if (serial == NULL) {
            fprintf(stderr, "AmiPilotServer: wire transport failed: %s\n", serErr);
            if (rdargs != NULL) {
                FreeArgs(rdargs);
            }
            AmipArexxClose(arexxPort);
            AmipActionShutdown();
            goto cleanup;
        }
        serialSig = AmipSerialSigMask(serial);
        printf("AmiPilotServer: wire on %s unit %ld at %ld baud\n",
               device, (long)unit, (long)baud);
    }
    printf("AmiPilotServer: ARexx port %s ready\n", portName);
    fflush(stdout);

    rexxSig = 1UL << arexxPort->mp_SigBit;

    while (running) {
        ULONG sigs = Wait(rexxSig | serialSig | SIGBREAKF_CTRL_C);

        if (sigs & SIGBREAKF_CTRL_C) {
            running = FALSE;
        }

        if (sigs & rexxSig) {
            void *handle;
            AmipArexxParsed cmd;

            while ((handle = AmipArexxReceive(arexxPort, &cmd)) != NULL) {
                const char *result = NULL;
                int rc = HandleCommand(&cmd, &result, &running);

                AmipArexxReply(handle, rc, result);
            }
        }

        /* The wire (server/WIRE.md): parse each complete request line
         * with the same parser the ARexx port uses, dispatch through
         * the same HandleCommand, frame the reply as
         * "RC <code> <byte-count>\n" + exactly that many payload
         * bytes. A parse failure isn't special-cased: AmipArexxParse
         * leaves cmd.type at UNKNOWN and the dispatch maps that to
         * RC 10 with a one-line reason, as the spec requires. QUIT
         * replies before running goes FALSE, satisfying the spec's
         * reply-then-exit order. */
        if (serial != NULL && (sigs & serialSig)) {
            const char *lineIn;

            while ((lineIn = AmipSerialNextLine(serial)) != NULL) {
                AmipArexxParsed cmd;
                const char *result = NULL;
                char header[32];
                int rc;
                ULONG payloadLen;

                AmipArexxParse(lineIn, &cmd);
                rc = HandleCommand(&cmd, &result, &running);

                payloadLen = result != NULL ? strlen(result) : 0;
                snprintf(header, sizeof(header), "RC %d %lu\n",
                         rc, (unsigned long)payloadLen);
                if (!AmipSerialWrite(serial, header, strlen(header)) ||
                    !AmipSerialWrite(serial, result, payloadLen)) {
                    fprintf(stderr, "AmiPilotServer: serial write failed\n");
                }
            }
        }
    }

    AmipSerialClose(serial);
    if (rdargs != NULL) {
        FreeArgs(rdargs);
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
