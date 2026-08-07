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
 * Phase 0.4 adds a second wire carrier: TCP (TCP/TCPPORT switches,
 * bsdsocket.library, tcp.c) -- same framing, same dispatch, so
 * real-hardware Amigas with TCP/IP and emulators without a serial
 * bridge (e.g. NAT-only networking) both reach the wire.
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
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/rexxsyslib.h>
#include <proto/bsdsocket.h>
#include <stdio.h>
#include <string.h>

#include "action_engine.h"
#include "arexx.h"
#include "fs.h"
#include "intuition_model.h"
#include "manifest.h"
#include "serial.h"
#include "tcp.h"

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
/* Consumed by tcp.c's socket calls -- proto/bsdsocket.h's own extern
 * declaration expects exactly this name. Only opened when TCP is
 * requested (see RealMain); always initialized regardless, per this
 * project's own hard-won rule about library-base globals (CLAUDE.md,
 * "Always initialize library-base globals") -- an uninitialized
 * `struct Library *FooBase;` is a COMMON symbol libnix's own archive
 * can silently pull in an auto-open constructor for. */
struct Library *SocketBase = NULL;

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

/* Escapes '"' and '\' as '\"'/'\\' so a text field can be safely
 * delimited by its own surrounding quotes on the wire. Before this,
 * none of this file's response payloads escaped anything -- a real
 * Amiga gadget label, window/screen title, or menu item text
 * containing a literal '"' (e.g. a `3.5" Drive` label) produced a
 * genuinely well-formed RC 0 response that broke every host parser's
 * fixed-format regex, since `[^"]*` stops at the first embedded quote.
 * Returns g_escapeBuf; NOT reentrant (a second call before the first
 * result is consumed overwrites it) -- every call site below uses it
 * for exactly one field per snprintf(), never two in the same call,
 * for exactly this reason. Truncates silently if the escaped form
 * would overflow AMIP_ESCAPE_BUF_SIZE, same graceful-degradation
 * policy as this project's other bounded buffers. */
#define AMIP_ESCAPE_BUF_SIZE 512
static char g_escapeBuf[AMIP_ESCAPE_BUF_SIZE];

static const char *EscapeQuotes(const char *in)
{
    size_t o = 0;

    if (in == NULL) {
        g_escapeBuf[0] = '\0';
        return g_escapeBuf;
    }
    for (; *in != '\0' && o + 2 < AMIP_ESCAPE_BUF_SIZE; in++) {
        if (*in == '"' || *in == '\\') {
            g_escapeBuf[o++] = '\\';
        }
        g_escapeBuf[o++] = (char)*in;
    }
    g_escapeBuf[o] = '\0';
    return g_escapeBuf;
}

/* Appends one gadget's line to buf in the same shape AmiInspect's
 * PrintModel prints it, tracking remaining space so a window with more
 * gadgets than fit just truncates rather than overflowing. Each
 * quoted field gets its own EscapeQuotes() + snprintf() pair -- see
 * EscapeQuotes()'s own comment for why never two in one call. */
static void AppendGadgetLine(char *buf, size_t cap, const AmipGadgetModel *gadget)
{
    size_t used = strlen(buf);
    if (used >= cap - 1) {
        return;
    }
    snprintf(buf + used, cap - used, "  gadget id=%lu role=%s class=\"",
             (unsigned long)gadget->gadgetId, AmipRoleName(gadget->role));
    used = strlen(buf);
    snprintf(buf + used, cap - used, "%s", EscapeQuotes((const char *)gadget->className));
    used = strlen(buf);
    snprintf(buf + used, cap - used, "\" label=\"");
    used = strlen(buf);
    snprintf(buf + used, cap - used, "%s", EscapeQuotes((const char *)gadget->label));
    used = strlen(buf);
    snprintf(buf + used, cap - used, "\"");
    if (gadget->value != NULL) {
        used = strlen(buf);
        snprintf(buf + used, cap - used, " value=\"");
        used = strlen(buf);
        snprintf(buf + used, cap - used, "%s", EscapeQuotes((const char *)gadget->value));
        used = strlen(buf);
        snprintf(buf + used, cap - used, "\"");
    }
    used = strlen(buf);
    snprintf(buf + used, cap - used, " [%d,%d %dx%d]\n",
             gadget->left, gadget->top, gadget->width, gadget->height);
}

/* Appends the shared "window ... screen ... [...]" header line
 * BuildTreeResult()/BuildMenuResult() both start with. */
static void AppendWindowHeaderLine(const char *title, const char *screenTitle,
                                    WORD left, WORD top, WORD width, WORD height,
                                    char *buf, size_t cap)
{
    buf[0] = '\0';
    snprintf(buf, cap, "window \"");
    snprintf(buf + strlen(buf), cap - strlen(buf), "%s",
             title != NULL ? EscapeQuotes(title) : "(untitled)");
    snprintf(buf + strlen(buf), cap - strlen(buf), "\" screen=\"");
    snprintf(buf + strlen(buf), cap - strlen(buf), "%s", EscapeQuotes(screenTitle));
    snprintf(buf + strlen(buf), cap - strlen(buf), "\" [%d,%d %dx%d]\n",
             left, top, width, height);
}

static void BuildTreeResult(const AmipWindowModel *model, char *buf, size_t cap)
{
    const AmipGadgetModel *gadget;

    AppendWindowHeaderLine(model->title != NULL ? (const char *)model->title : NULL,
                            (const char *)model->screenTitle,
                            model->left, model->top, model->width, model->height,
                            buf, cap);

    for (gadget = model->gadgets; gadget != NULL; gadget = gadget->next) {
        AppendGadgetLine(buf, cap, gadget);
    }
}

/* Appends one menu item's line, same shape AmiInspect's PrintMenuItemLine
 * prints it -- "item" for a top-level entry (num=<menu>/<item>),
 * "subitem" for one of its submenu children (num=<menu>/<item>/<sub>). */
static void AppendMenuItemLine(char *buf, size_t cap, const AmipMenuItemModel *item,
                                const char *tag, int indent)
{
    size_t used = strlen(buf);
    if (used >= cap - 1) {
        return;
    }
    snprintf(buf + used, cap - used, "%*s%s num=%ld/%ld", indent, "", tag,
             (long)item->menuNum, (long)item->itemNum);
    if (item->subNum >= 0) {
        used = strlen(buf);
        snprintf(buf + used, cap - used, "/%ld", (long)item->subNum);
    }
    used = strlen(buf);
    snprintf(buf + used, cap - used, " text=\"");
    used = strlen(buf);
    snprintf(buf + used, cap - used, "%s", EscapeQuotes((const char *)item->text));
    used = strlen(buf);
    snprintf(buf + used, cap - used, "\"");
    if (item->hasShortcut) {
        used = strlen(buf);
        snprintf(buf + used, cap - used, " shortcut=%c", (char)item->shortcut);
    }
    used = strlen(buf);
    snprintf(buf + used, cap - used, " checkit=%d checked=%d enabled=%d\n",
             item->checkit, item->checked, item->enabled);
}

static void BuildMenuResult(const AmipWindowModel *window, const AmipMenuModel *menus,
                             char *buf, size_t cap)
{
    const AmipMenuModel *menu;

    AppendWindowHeaderLine(window->title != NULL ? (const char *)window->title : NULL,
                            (const char *)window->screenTitle,
                            window->left, window->top, window->width, window->height,
                            buf, cap);

    for (menu = menus; menu != NULL; menu = menu->next) {
        const AmipMenuItemModel *item;
        size_t used = strlen(buf);

        if (used >= cap - 1) {
            break;
        }
        snprintf(buf + used, cap - used, "menu num=%ld title=\"", (long)menu->menuNum);
        used = strlen(buf);
        snprintf(buf + used, cap - used, "%s", EscapeQuotes((const char *)menu->title));
        used = strlen(buf);
        snprintf(buf + used, cap - used, "\" enabled=%d\n", menu->enabled);

        for (item = menu->items; item != NULL; item = item->next) {
            const AmipMenuItemModel *sub;

            AppendMenuItemLine(buf, cap, item, "item", 2);
            for (sub = item->subItems; sub != NULL; sub = sub->next) {
                AppendMenuItemLine(buf, cap, sub, "subitem", 4);
            }
        }
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

#define AMIP_TCP_PASSWORD_MAX 64
/* SECURITY: "amipilot" is a PUBLIC default (it's in this open-source
 * repo) -- it exists only to give TCP a sane, non-empty starting
 * point ("a token amount of security" against a blind/naive scan of
 * an open port), not to actually protect anything. Set a real
 * TCPPASSWORD for any deployment that matters. See tcp.h's own
 * SECURITY note and server/README.md's TCP section. */
#define AMIP_TCP_DEFAULT_PASSWORD "amipilot"
static char g_tcpPassword[AMIP_TCP_PASSWORD_MAX] = AMIP_TCP_DEFAULT_PASSWORD;

/* Executes one parsed command -- the single dispatch both transports
 * share (ARexx RESULT string and wire payload are the same bytes; see
 * server/WIRE.md). Returns the AMIP_AREXX_RC_* code, points *resultOut
 * at the payload (NULL/empty for none; valid until the next call --
 * file-static buffers above), writes its length to *resultLenOut, and
 * clears *runningOut on QUIT.
 *
 * *resultLenOut exists because FSGET's payload is raw file bytes and
 * may contain embedded NULs -- strlen() would truncate it. Every verb
 * except FSGET leaves *resultLenOut at its initial 0, in which case
 * the caller falls back to strlen(result): safe even for a genuinely
 * empty FSGET result, since a buffer that starts with a NUL byte
 * strlen()s to 0 either way. */
static int HandleCommand(AmipArexxParsed *cmd, const char **resultOut,
                         ULONG *resultLenOut, BOOL *runningOut,
                         BOOL requiresAuth, BOOL *authenticated)
{
    int rc = AMIP_AREXX_RC_OK;
    const char *result = NULL;

    *resultLenOut = 0;

    g_resultBuf[0] = '\0';

    /* AUTH is handled before anything else, including the auth gate
     * below -- otherwise a client could never authenticate at all.
     * Parseable and answerable on every transport (one grammar), but
     * only meaningful where requiresAuth is set (TCP's own dispatch
     * loop); ARexx/serial.device pass requiresAuth=FALSE and never
     * check *authenticated, so AUTH there is compared but inert. */
    if (cmd->type == AMIP_AREXX_CMD_AUTH) {
        if (strcmp(cmd->path, g_tcpPassword) == 0) {
            *authenticated = TRUE;
            *resultOut = NULL;
            return AMIP_AREXX_RC_OK;
        }
        strncpy(g_resultBuf, "authentication failed: wrong password",
                sizeof(g_resultBuf) - 1);
        *resultLenOut = (ULONG)strlen(g_resultBuf);
        *resultOut = g_resultBuf;
        return AMIP_AREXX_RC_ERROR;
    }

    /* On TCP, until AUTH has succeeded, every command except VERSION
     * (needed to feature-test before authenticating) and QUIT (so a
     * confused client can always disconnect cleanly) is refused
     * without being dispatched at all. */
    if (requiresAuth && !*authenticated
        && cmd->type != AMIP_AREXX_CMD_VERSION
        && cmd->type != AMIP_AREXX_CMD_QUIT) {
        strncpy(g_resultBuf, "not authenticated -- send AUTH <password> first",
                sizeof(g_resultBuf) - 1);
        *resultLenOut = (ULONG)strlen(g_resultBuf);
        *resultOut = g_resultBuf;
        return AMIP_AREXX_RC_ERROR;
    }

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
                     "EXPERIMENTAL TREE CLICK TYPE GETTEXT MANIFEST LAUNCH "
                     "FSLIST FSSTAT FSMKDIR FSDELETE FSGET MENU MENUPICK "
                     "SCREENS AUTH QUIT\n");
            result = g_resultBuf;
            break;

        case AMIP_AREXX_CMD_TREE: {
            struct Window *w = AmipFindWindow((CONST_STRPTR)cmd->screenPattern, (CONST_STRPTR)cmd->windowPattern);
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
            struct Window *w = AmipFindWindow((CONST_STRPTR)cmd->screenPattern, (CONST_STRPTR)cmd->windowPattern);
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
            struct Window *w = AmipFindWindow((CONST_STRPTR)cmd->screenPattern, (CONST_STRPTR)cmd->windowPattern);
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
            struct Window *w = AmipFindWindow((CONST_STRPTR)cmd->screenPattern, (CONST_STRPTR)cmd->windowPattern);

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

        case AMIP_AREXX_CMD_LAUNCH: {
            BPTR input, output;
            struct TagItem tags[5];
            int nTags = 0;
            LONG sysResult;

            /* Explicit NIL: handles, not defaulted: SystemTagList()'s
             * own docs say an async launch closes the caller's
             * Input()/Output() on completion "even if these were your
             * Input() and Output()!" -- leaving SYS_Input/SYS_Output
             * unset would eventually close AmiPilotServer's own
             * stdio. No output capture yet (phase 0.4 follow-up, see
             * server/README.md); the child's output goes to NIL:. */
            input = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);
            if (input == 0) {
                rc = AMIP_AREXX_RC_FAIL;
                strncpy(g_resultBuf, "could not open NIL: for input",
                        sizeof(g_resultBuf) - 1);
                result = g_resultBuf;
                break;
            }
            output = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
            if (output == 0) {
                Close(input);
                rc = AMIP_AREXX_RC_FAIL;
                strncpy(g_resultBuf, "could not open NIL: for output",
                        sizeof(g_resultBuf) - 1);
                result = g_resultBuf;
                break;
            }

            tags[nTags].ti_Tag = SYS_Input;  tags[nTags++].ti_Data = (ULONG)input;
            tags[nTags].ti_Tag = SYS_Output; tags[nTags++].ti_Data = (ULONG)output;
            tags[nTags].ti_Tag = SYS_Asynch; tags[nTags++].ti_Data = TRUE;
            if (cmd->stackSize > 0) {
                tags[nTags].ti_Tag = NP_StackSize;
                tags[nTags++].ti_Data = (ULONG)cmd->stackSize;
            }
            tags[nTags].ti_Tag = TAG_DONE; tags[nTags++].ti_Data = 0;

            /* Asynchronous launch: SystemTagList() returns as soon as
             * the shell process itself is created, 0 for success or
             * -1 if dos couldn't create it at all (out of memory, no
             * process slot) -- NOT the eventual exit code, and NOT
             * proof the command was found. A bad command name still
             * returns 0 here; the spawned shell reports that failure
             * on its own (discarded) output stream. Real command-
             * found verification and exit-code retrieval need the
             * proc-wait verb this phase's own plan section calls for
             * separately -- not invented here ahead of it. */
            sysResult = SystemTagList((CONST_STRPTR)cmd->command, tags);
            if (sysResult != 0) {
                rc = AMIP_AREXX_RC_FAIL;
                strncpy(g_resultBuf, "launch failed (out of memory or no process slot)",
                        sizeof(g_resultBuf) - 1);
                result = g_resultBuf;
            }
            break;
        }

        case AMIP_AREXX_CMD_FSLIST:
            rc = AmipFsList(cmd->path, &result, resultLenOut);
            break;

        case AMIP_AREXX_CMD_FSSTAT:
            rc = AmipFsStat(cmd->path, &result, resultLenOut);
            break;

        case AMIP_AREXX_CMD_FSMKDIR:
            rc = AmipFsMkdir(cmd->path, &result, resultLenOut);
            break;

        case AMIP_AREXX_CMD_FSDELETE:
            rc = AmipFsDelete(cmd->path, &result, resultLenOut);
            break;

        case AMIP_AREXX_CMD_FSGET:
            rc = AmipFsGet(cmd->path, &result, resultLenOut);
            break;

        case AMIP_AREXX_CMD_MENU: {
            struct Window *w = AmipFindWindow((CONST_STRPTR)cmd->screenPattern, (CONST_STRPTR)cmd->windowPattern);
            AmipWindowModel *model;
            AmipMenuModel *menus;

            if (w == NULL) {
                rc = AMIP_AREXX_RC_WARN;
                break;
            }
            model = AmipWalkWindow(w);
            if (model == NULL) {
                rc = AMIP_AREXX_RC_FAIL;
                break;
            }
            menus = AmipWalkMenuStrip(w);
            BuildMenuResult(model, menus, g_treeBuf, sizeof(g_treeBuf));
            AmipFreeMenuModel(menus);
            AmipFreeWindowModel(model);
            result = g_treeBuf;
            break;
        }

        case AMIP_AREXX_CMD_MENUPICK: {
            struct Window *w = AmipFindWindow((CONST_STRPTR)cmd->screenPattern, (CONST_STRPTR)cmd->windowPattern);
            struct MenuItem *item;
            AmipMenuPickResult pickRc;

            if (w == NULL) {
                rc = AMIP_AREXX_RC_WARN;
                break;
            }
            item = AmipFindMenuItem(w, cmd->menuNum, cmd->itemNum, cmd->subNum);
            if (item == NULL || !AmipIsWindowOpen(w)) {
                rc = AMIP_AREXX_RC_WARN;
                break;
            }
            pickRc = AmipMenuPickByShortcut(w, item);
            switch (pickRc) {
                case AMIP_MENUPICK_OK:
                    break;
                case AMIP_MENUPICK_DISABLED:
                    rc = AMIP_AREXX_RC_FAIL;
                    strncpy(g_resultBuf, "menu item is disabled", sizeof(g_resultBuf) - 1);
                    result = g_resultBuf;
                    break;
                case AMIP_MENUPICK_NO_SHORTCUT:
                    rc = AMIP_AREXX_RC_FAIL;
                    strncpy(g_resultBuf,
                            "item has no keyboard shortcut -- pointer-based menu "
                            "selection isn't built yet (see server/README.md)",
                            sizeof(g_resultBuf) - 1);
                    result = g_resultBuf;
                    break;
                case AMIP_MENUPICK_INJECT_FAILED:
                default:
                    rc = AMIP_AREXX_RC_FAIL;
                    strncpy(g_resultBuf, "menu pick failed to inject input",
                            sizeof(g_resultBuf) - 1);
                    result = g_resultBuf;
                    break;
            }
            break;
        }

        case AMIP_AREXX_CMD_SCREENS: {
            struct Screen *screen;
            size_t used;

            g_treeBuf[0] = '\0';
            /* A brief LockIBase hold for the whole walk, same as
             * AmipIsWindowOpen's own raw structure walk -- this is a
             * short, allocation-free loop (just formatting into an
             * already-owned buffer), not a copy-out-then-release model
             * like intuition-model's own walkers use for the (longer)
             * gadget-tree walk. */
            LockIBase(0);
            for (screen = IntuitionBase->FirstScreen; screen != NULL; screen = screen->NextScreen) {
                used = strlen(g_treeBuf);
                if (used >= sizeof(g_treeBuf) - 1) {
                    break;
                }
                /* DefaultTitle, not the live Title field -- see
                 * action_engine.h's AmipFindWindow doc comment for why
                 * Title isn't a stable screen identity. */
                snprintf(g_treeBuf + used, sizeof(g_treeBuf) - used,
                         "screen title=\"%s\" [%d,%d %dx%d] frontmost=%d\n",
                         EscapeQuotes((const char *)screen->DefaultTitle),
                         screen->LeftEdge, screen->TopEdge, screen->Width, screen->Height,
                         screen == IntuitionBase->FirstScreen ? 1 : 0);
            }
            UnlockIBase(0);
            result = g_treeBuf;
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

    if (result != NULL && *resultLenOut == 0) {
        *resultLenOut = (ULONG)strlen(result);
    }
    *resultOut = result;
    return rc;
}

/* ReadArgs template: SERIAL fits the wire transport (server/WIRE.md)
 * alongside the always-on ARexx port; SERDEVICE/SERUNIT pick the device
 * (default serial.device unit 0 -- a multi-port card's driver slots in
 * by name), BAUD the line rate (default 19200: conservative for the
 * plain-68000 floor; both ends must simply agree, and under an
 * emulator's TCP bridge the guest-side number is inert anyway). TCP
 * fits the same wire over bsdsocket.library instead; TCPPORT picks the
 * listen port (no canonical default port is claimed for this project
 * yet, so one must be given explicitly -- see server/README.md). SERIAL
 * and TCP are independent and either or both may be given at once.
 *
 * FSROOT/M grants one or more root directories to the file API
 * (server/include/fs.h) -- never granted implicitly (server/README.md's
 * "never assumed" rule): with no FSROOT, every FS* verb returns RC 10.
 * "FSROOT T: RAM:" grants both in one command (see ReadArgs()'s own
 * /M semantics: any number of space-separated values after the one
 * keyword occurrence, not a repeated keyword).
 *
 * SECURITY (see tcp.h's own SECURITY note -- this transport is for a
 * trusted LAN, never an open/internet-facing port): TCPALLOW grants a
 * source-IP/CIDR allowlist for the TCP transport only. A single /K
 * value, NOT /M -- ReadArgs()'s template grammar allows at most one
 * /M keyword per template, and FSROOT already claims that slot,
 * confirmed live (a second /M keyword makes ReadArgs() fail with
 * ERROR_BAD_TEMPLATE on EVERY invocation, not just when TCPALLOW is
 * used -- caught via Amiberry verification, 2026-08-07). Multiple
 * entries are comma-separated in the one value instead:
 * "TCPALLOW=192.168.1.0/24,10.0.0.5". With none granted, every source
 * is accepted, unchanged from this transport's original behavior.
 * TCPPASSWORD sets the password the AUTH verb checks (also TCP-only --
 * ARexx and serial.device keep their existing implicit trust
 * boundaries); if omitted, AMIP_TCP_DEFAULT_PASSWORD ("amipilot", a
 * PUBLIC value checked into this repo) applies, which the bundled
 * host client already sends automatically -- a sane non-empty
 * starting point, not real protection. Neither option is mandatory;
 * both are independent and combinable. */
#define AMIP_ARG_TEMPLATE \
    "SERIAL/S,SERDEVICE/K,SERUNIT/K/N,BAUD/K/N,TCP/S,TCPPORT/K/N,FSROOT/K/M," \
    "TCPALLOW/K,TCPPASSWORD/K"
enum {
    ARG_SERIAL, ARG_SERDEVICE, ARG_SERUNIT, ARG_BAUD,
    ARG_TCP, ARG_TCPPORT, ARG_FSROOT,
    ARG_TCPALLOW, ARG_TCPPASSWORD,
    ARG_COUNT
};

static int RealMain(void)
{
    struct MsgPort *arexxPort;
    struct RDArgs *rdargs;
    LONG argArray[ARG_COUNT] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    AmipSerial *serial = NULL;
    AmipTcp *tcp = NULL;
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
    /* TCP requested but unopenable is fatal too, same rationale as
     * SERIAL above -- both are genuine requests for the wire, not
     * best-effort extras. */
    if (argArray[ARG_TCP]) {
        LONG port = argArray[ARG_TCPPORT] != 0 ? *(LONG *)argArray[ARG_TCPPORT] : 0;
        char tcpErr[80];

        if (port == 0) {
            fprintf(stderr, "AmiPilotServer: TCP requires TCPPORT\n");
            AmipSerialClose(serial);
            FreeArgs(rdargs);
            AmipArexxClose(arexxPort);
            AmipActionShutdown();
            goto cleanup;
        }

        SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 0);
        if (SocketBase == NULL) {
            fprintf(stderr, "AmiPilotServer: TCP requires bsdsocket.library\n");
            AmipSerialClose(serial);
            FreeArgs(rdargs);
            AmipArexxClose(arexxPort);
            AmipActionShutdown();
            goto cleanup;
        }

        tcp = AmipTcpOpen(port, tcpErr, sizeof(tcpErr));
        if (tcp == NULL) {
            fprintf(stderr, "AmiPilotServer: wire transport failed: %s\n", tcpErr);
            AmipSerialClose(serial);
            FreeArgs(rdargs);
            AmipArexxClose(arexxPort);
            AmipActionShutdown();
            goto cleanup;
        }
        printf("AmiPilotServer: wire on TCP port %ld\n", (long)port);

        /* A bad TCPALLOW entry is a config mistake, not a soft-degrade
         * case -- fatal now, same posture FSROOT's own grant loop
         * (below) uses for a bad root. TCPALLOW is a single /K value
         * (see this section's own comment above AMIP_ARG_TEMPLATE for
         * why, not /M), so multiple entries are comma-separated in
         * one string and split here rather than via ReadArgs()
         * itself. */
        if (argArray[ARG_TCPALLOW] != 0) {
            char allowBuf[256];
            char *tokenStart;

            strncpy(allowBuf, (const char *)argArray[ARG_TCPALLOW], sizeof(allowBuf) - 1);
            allowBuf[sizeof(allowBuf) - 1] = '\0';

            tokenStart = allowBuf;
            for (;;) {
                char *comma = strchr(tokenStart, ',');
                char allowErr[80];

                if (comma != NULL) {
                    *comma = '\0';
                }
                if (!AmipTcpAllow(tcp, tokenStart, allowErr, sizeof(allowErr))) {
                    fprintf(stderr, "AmiPilotServer: TCPALLOW %s failed: %s\n",
                            tokenStart, allowErr);
                    AmipTcpClose(tcp);
                    AmipSerialClose(serial);
                    FreeArgs(rdargs);
                    AmipArexxClose(arexxPort);
                    AmipActionShutdown();
                    goto cleanup;
                }
                printf("AmiPilotServer: TCP allowlist grants %s\n", tokenStart);
                if (comma == NULL) {
                    break;
                }
                tokenStart = comma + 1;
            }
        }

        /* Never print the password itself, custom or default -- only
         * whether it's been changed from the public default, so
         * operator logs/screen-sharing can't leak it. */
        if (argArray[ARG_TCPPASSWORD] != 0) {
            strncpy(g_tcpPassword, (const char *)argArray[ARG_TCPPASSWORD],
                    sizeof(g_tcpPassword) - 1);
            g_tcpPassword[sizeof(g_tcpPassword) - 1] = '\0';
            printf("AmiPilotServer: TCP password set (custom)\n");
        } else {
            printf("AmiPilotServer: TCP password set (default -- override with "
                   "TCPPASSWORD=<value> for real protection)\n");
        }

        /* Per this repo's own SECURITY note (tcp.h): TCPALLOW/
         * TCPPASSWORD raise the bar above "wide open," but neither
         * makes this transport internet-safe -- no TLS, a public
         * default password, no rate-limiting. This prints every time
         * TCP is enabled, not just when the docs happen to get read. */
        printf("AmiPilotServer: TCP is NOT safe to expose on an open/"
               "internet-facing port -- LAN/trusted-network use only, "
               "see server/README.md\n");
    }

    /* A bad FSROOT is a config mistake, not a soft-degrade case --
     * fatal now, same as SERIAL/TCP above, rather than silently
     * running with fewer roots than the caller asked for. */
    if (argArray[ARG_FSROOT] != 0) {
        STRPTR *roots = (STRPTR *)argArray[ARG_FSROOT];
        int i;

        for (i = 0; roots[i] != NULL; i++) {
            char fsErr[80];

            if (!AmipFsGrantRoot((const char *)roots[i], fsErr, sizeof(fsErr))) {
                fprintf(stderr, "AmiPilotServer: FSROOT %s failed: %s\n",
                        (const char *)roots[i], fsErr);
                AmipFsShutdown(); /* release any roots already granted
                                   * before this one failed */
                AmipTcpClose(tcp);
                AmipSerialClose(serial);
                FreeArgs(rdargs);
                AmipArexxClose(arexxPort);
                AmipActionShutdown();
                goto cleanup;
            }
            printf("AmiPilotServer: file API granted %s\n", (const char *)roots[i]);
        }
    }

    printf("AmiPilotServer: ARexx port %s ready\n", portName);
    fflush(stdout);

    rexxSig = 1UL << arexxPort->mp_SigBit;

    while (running) {
        /* TCP's readiness is driven by WaitSelect() (tcp.c's own header
         * comment has the full story: the more obvious SBTC_SIGEVENTMASK
         * async-signal mechanism reported success but never actually
         * delivered a signal when tested live), which is itself the
         * blocking call -- when TCP is active it replaces this plain
         * Wait() outright rather than contributing a signal bit to it. */
        ULONG sigs = tcp != NULL
            ? AmipTcpWait(tcp, rexxSig | serialSig | SIGBREAKF_CTRL_C)
            : Wait(rexxSig | serialSig | SIGBREAKF_CTRL_C);

        if (sigs & SIGBREAKF_CTRL_C) {
            running = FALSE;
        }

        if (sigs & rexxSig) {
            void *handle;
            AmipArexxParsed cmd;

            while ((handle = AmipArexxReceive(arexxPort, &cmd)) != NULL) {
                const char *result = NULL;
                ULONG resultLen; /* unused here -- ARexx RESULT is always
                                  * a NUL-terminated C string; only the
                                  * wire's binary-safe framing below
                                  * needs the explicit length (FSGET). */
                BOOL authIgnored = TRUE; /* ARexx never enforces AUTH --
                                           * local machine, same implicit
                                           * trust boundary as always. */
                int rc = HandleCommand(&cmd, &result, &resultLen, &running,
                                        FALSE, &authIgnored);

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
                BOOL authIgnored = TRUE; /* serial.device never enforces
                                           * AUTH -- physical cable is
                                           * its own trust boundary. */

                AmipArexxParse(lineIn, &cmd);
                rc = HandleCommand(&cmd, &result, &payloadLen, &running,
                                    FALSE, &authIgnored);

                snprintf(header, sizeof(header), "RC %d %lu\n",
                         rc, (unsigned long)payloadLen);
                if (!AmipSerialWrite(serial, header, strlen(header)) ||
                    !AmipSerialWrite(serial, result, payloadLen)) {
                    fprintf(stderr, "AmiPilotServer: serial write failed\n");
                }
            }
        }

        /* Same wire, same dispatch, TCP carrier -- see the serial
         * block above for the shared framing rationale. Unconditional
         * on `tcp != NULL`, not gated on a signal bit: AmipTcpWait()
         * above already did the actual accept()/recv() work (its own
         * blocking call replaces Wait() outright when TCP is active --
         * see tcp.h), so any newly available lines are already
         * buffered here regardless of what `sigs` reports; draining
         * with nothing buffered is just an immediate NULL, not a
         * wasted blocking call. */
        if (tcp != NULL) {
            const char *lineIn;

            while ((lineIn = AmipTcpNextLine(tcp)) != NULL) {
                AmipArexxParsed cmd;
                const char *result = NULL;
                char header[32];
                int rc;
                ULONG payloadLen;
                BOOL authState = AmipTcpIsAuthenticated(tcp);

                AmipArexxParse(lineIn, &cmd);
                rc = HandleCommand(&cmd, &result, &payloadLen, &running,
                                    TRUE, &authState);
                AmipTcpSetAuthenticated(tcp, authState);

                snprintf(header, sizeof(header), "RC %d %lu\n",
                         rc, (unsigned long)payloadLen);
                if (!AmipTcpWrite(tcp, header, strlen(header)) ||
                    !AmipTcpWrite(tcp, result, payloadLen)) {
                    fprintf(stderr, "AmiPilotServer: TCP write failed\n");
                }
            }
        }
    }

    AmipFsShutdown();
    AmipTcpClose(tcp);
    AmipSerialClose(serial);
    if (rdargs != NULL) {
        FreeArgs(rdargs);
    }
    AmipArexxClose(arexxPort);
    AmipActionShutdown();

cleanup:
    if (SocketBase != NULL) {
        CloseLibrary(SocketBase);
    }
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
