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
 * Also implements the CAAPP.WHERE ARexx port (issue #49,
 * manifest/SPEC.md's "The cooperative geometry port"): the layout-
 * child limit above means none of this app's own three gadgets are
 * reachable via a plain GADGET manifest entry, so CAApp.manifest (v2)
 * names all three as WHEREGADGETs instead, resolved live through this
 * port rather than by GA_ID.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <intuition/classusr.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/checkbox.h>
#include <gadgets/string.h>
#include <classes/window.h>

/* Included in this exact order (proto/rexxsyslib.h before the rexx/
 * headers, and both before __CLIB_PRAGMA_LIBCALL is defined below) to
 * match server/src/arexx.c's own proven-working include order --
 * reordering these was found, empirically, to matter: with rexx/
 * storage.h included first instead, struct RexxMsg came out
 * genuinely incomplete in this translation unit (sizeof reporting 0),
 * so RexxSysBase's IsRexxMsg() silently operated on garbage instead
 * of the real message. */
#include <proto/rexxsyslib.h>
#include <rexx/storage.h>
#include <rexx/rxslib.h>

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

/* rexxsyslib.library's own base -- must be named exactly RexxSysBase
 * (not static), since <proto/rexxsyslib.h>'s inline call stubs
 * reference this global by name. Same convention server/src/arexx.c
 * follows; opening it here (rather than requiring it, like the
 * libraries above) is deliberate -- the WHEREPORT it backs is an
 * OPTIONAL manifest feature (manifest/SPEC.md), so its absence
 * degrades this fixture to "no cooperative geometry port", not a
 * launch failure. */
struct RxsLib *RexxSysBase = NULL;

#define GID_CONNECT  1
#define GID_HOST     2
#define GID_ENABLED  3

/* The ARexx port name this fixture's CAApp.manifest declares via
 * WHEREPORT -- see manifest/SPEC.md's "The cooperative geometry port"
 * section and its "Clash guard" note recommending a dedicated,
 * app-specific port name rather than reusing a general-purpose one. */
#define CAAPP_WHERE_PORT "CAAPP.WHERE"

/* PLACETEXT_RIGHT's canonical home is <libraries/gadtools.h>, not any
 * ReAction header, despite CHECKBOX_TextPlace using the same constant --
 * defined directly here rather than pulling in a GadTools header this
 * app has no other reason to depend on. */
#define CHECKBOX_TEXT_RIGHT 0x0002

/* File-scope, not main()'s own locals: the WHERE port dispatcher
 * (HandleWhereMessage() below) needs to read their live GA_Left/GA_Top/
 * GA_Width/GA_Height on demand, from the same event loop that created
 * them, exactly the "the app already holds the pointers it needs for
 * its own event dispatch" premise manifest/SPEC.md's "The cooperative
 * geometry port" section describes. */
static Object *g_connectButton = NULL;
static Object *g_hostString = NULL;
static Object *g_enabledCheckbox = NULL;
static struct MsgPort *g_wherePort = NULL;

/* The WHEREGADGET logical names CAApp.manifest declares, mapped to the
 * live object each one queries -- exact match, case-insensitive per
 * the WHERE port contract. */
static const struct {
    const char *name;
    Object **obj;
} g_whereTable[] = {
    { "connect_button",   &g_connectButton },
    { "host_field",       &g_hostString },
    { "enabled_checkbox", &g_enabledCheckbox },
};
#define WHERE_TABLE_COUNT (sizeof(g_whereTable) / sizeof(g_whereTable[0]))

/* Raw dos.library Write() to Output() so failure paths are visible in a
 * Run-redirected log without depending on stdio buffering. */
static void Diag(const char *msg)
{
    Write(Output(), (APTR)msg, (LONG)strlen(msg));
}

/* Appends msg to a fixed, host-readable log file, independent of this
 * process's own Output() stream. Used only for the WHERE end-to-end
 * test's one observable (tests/copperline/where-test.py) -- "Run
 * >file <command>" was tried first and does NOT reliably route a
 * background CLI's own Output() to the given file the way "Run >NIL:
 * <command>" reliably discards it (confirmed empirically: the
 * redirected file captured only the new CLI's own startup banner, none
 * of this program's own Diag() output at all, even lines logged before
 * any WHERE handling) -- opening the file directly here sidesteps that
 * uncertainty entirely. Silently does nothing if the path can't be
 * opened (e.g. SRC: isn't assigned outside the dev harness this test
 * runs under) -- an optional diagnostic, not something this fixture's
 * own correctness depends on. */
static void DiagFile(const char *path, const char *msg)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_READWRITE);
    if (fh == 0) {
        fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    }
    if (fh == 0) {
        return;
    }
    Seek(fh, 0, OFFSET_END);
    Write(fh, (APTR)msg, (LONG)strlen(msg));
    Close(fh);
}

static int CiStreq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

static int CiStrnEqAscii(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        int ca = a[i], cb = b[i];
        if (ca == '\0' || cb == '\0') return 0;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
    }
    return 1;
}

/* Replies to a WHERE query -- same rm_Result1/rm_Result2/ReplyMsg
 * shape as server/src/arexx.c's own AmipArexxReply(), duplicated
 * (rather than shared -- this fixture links no AmiPilot server code)
 * for the same "genuinely a different program" reason
 * gadtools-app/classact-app never link intuition-model. Does not
 * DeleteArgstring(rm_Result2) -- ARexx frees it after the caller
 * consumes the reply, same note as AmipArexxReply's own. */
static void ReplyWhere(struct RexxMsg *msg, int rc, const char *text)
{
    msg->rm_Result1 = rc;
    msg->rm_Result2 = 0;
    if ((msg->rm_Action & RXFF_RESULT) && text != NULL && text[0] != '\0') {
        msg->rm_Result2 = (LONG)CreateArgstring((UBYTE *)text, (ULONG)strlen(text));
    }
    ReplyMsg((struct Message *)msg);
}

/* Handles one "WHERE <logical-name>" query against g_whereTable --
 * manifest/SPEC.md's "The cooperative geometry port" contract: reports
 * the live GA_Left/GA_Top/GA_Width/GA_Height of the named object as
 * "<x> <y> <w> <h>" (RC 0), or RC 10 for an unrecognised command or
 * name. GA_Left/GA_Top/GA_Width/GA_Height are documented window-
 * relative (including the window's own border/title-bar area), the
 * same convention AmipGadgetCenter()/AmipClickWindowRelative() expect
 * on the AmiPilot side (server/src/action.c).
 *
 * Deliberately does NOT gate on rexxsyslib.library's own IsRexxMsg()
 * first, unlike server/src/arexx.c's own receiver. Two rounds of
 * experimentation (2026-08-09) against this exact port, with a
 * completely ordinary, unmodified IsRexxMsg() gate restored each
 * time, ruled out every sender-side fix tried: neither pre-marking
 * the outgoing message's own node type NT_REPLYMSG (the commonly
 * cited technique for a hand-built ARexx command send) nor using a
 * genuine CreateArgstring()-allocated rm_Args[0] (instead of a raw C
 * string pointer) made IsRexxMsg() accept a message built via
 * CreateRexxMsg()/FillRexxMsg()/PutMsg() (server/src/where.c's own
 * AmipWhereQuery() -- the same recipe server/src/muirexx.c's
 * AmipMuiRexxSend() already uses). The receiver's own ln_Type read
 * stayed NT_MESSAGE regardless of what the sender set beforehand --
 * consistent with PutMsg() itself resetting it, a real Exec message-
 * queueing behavior a real ARexx interpreter's own outgoing command
 * sends must go through some other, non-public mechanism to avoid.
 * IsRexxMsg() (a real rexxsyslib.library call, not just a header-
 * field check) is seemingly reachable in its "true" state only for
 * messages a live Rexx interpreter task itself constructs -- not
 * achievable for this project's own hand-built sends by any public
 * API combination tried. arexx.c's own receiver-side use of
 * IsRexxMsg() only ever works because its senders are real ARexx
 * scripts (`rx`), never this project's own MUIREXX/WHERE bridges.
 * CAAPP.WHERE is a port dedicated solely to this one protocol
 * (manifest/SPEC.md's own "Clash guard" -- a general-purpose port
 * sharing this same MsgPort would need a real discriminator here
 * instead), so trusting every message that arrives on it is the
 * correct, verified choice -- not a shortcut. Any third-party
 * application implementing WHERE needs the same: a receive loop that
 * does NOT call IsRexxMsg() on what it gets, on a port used for
 * nothing else. */
static void HandleWhereMessage(struct RexxMsg *msg)
{
    const char *cmdline = (const char *)ARG0(msg);
    const char *name;
    unsigned int i;

    if (cmdline == NULL || !CiStrnEqAscii(cmdline, "WHERE", 5)
        || (cmdline[5] != ' ' && cmdline[5] != '\t')) {
        ReplyWhere(msg, 10, "unknown command");
        return;
    }
    name = cmdline + 5;
    while (*name == ' ' || *name == '\t') name++;

    for (i = 0; i < WHERE_TABLE_COUNT; i++) {
        if (CiStreq(name, g_whereTable[i].name)) {
            Object *obj = *g_whereTable[i].obj;
            ULONG gx = 0, gy = 0, gw = 0, gh = 0;
            char reply[48];

            GetAttr(GA_Left, obj, &gx);
            GetAttr(GA_Top, obj, &gy);
            GetAttr(GA_Width, obj, &gw);
            GetAttr(GA_Height, obj, &gh);
            sprintf(reply, "%ld %ld %ld %ld", (long)gx, (long)gy, (long)gw, (long)gh);
            ReplyWhere(msg, 0, reply);
            return;
        }
    }
    ReplyWhere(msg, 10, "unknown name");
}

/* Creates the WHERE port under one Forbid() (matches server/src/
 * arexx.c's AmipArexxOpen() convention). RexxSysBase == NULL (library
 * not present) degrades to no port at all -- graceful, same pattern
 * as this file's own IntuitionBase/WindowBase/etc. opens, except this
 * one is optional rather than fatal (see RexxSysBase's own doc
 * comment above). */
static struct MsgPort *OpenWherePort(void)
{
    struct MsgPort *port = NULL;

    if (RexxSysBase == NULL) {
        return NULL;
    }

    Forbid();
    if (FindPort((CONST_STRPTR)CAAPP_WHERE_PORT) == NULL) {
        port = CreateMsgPort();
        if (port != NULL) {
            port->mp_Node.ln_Name = (char *)CAAPP_WHERE_PORT;
            AddPort(port);
        }
    }
    Permit();
    return port;
}

static void CloseWherePort(struct MsgPort *port)
{
    struct RexxMsg *msg;

    if (port == NULL) {
        return;
    }

    Forbid();
    RemPort(port);
    Permit();

    while ((msg = (struct RexxMsg *)GetMsg(port)) != NULL) {
        if (IsRexxMsg(msg)) {
            ReplyWhere(msg, 20, NULL);
        } else {
            ReplyMsg((struct Message *)msg);
        }
    }
    DeleteMsgPort(port);
}

static void CleanExit(Object *windowObject, int rc)
{
    CloseWherePort(g_wherePort);
    g_wherePort = NULL;

    if (windowObject != NULL) {
        DisposeObject(windowObject);
    }

    if (RexxSysBase != NULL) {
        CloseLibrary((struct Library *)RexxSysBase);
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

static void ProcessEvents(Object *windowObject, struct MsgPort *wherePort)
{
    ULONG windowSignal;
    ULONG whereSignal = (wherePort != NULL) ? (1UL << wherePort->mp_SigBit) : 0;
    ULONG signals;
    ULONG result;
    ULONG code;
    BOOL done = FALSE;

    GetAttr(WINDOW_SigMask, windowObject, &windowSignal);

    while (!done) {
        signals = Wait(windowSignal | whereSignal);

        if (whereSignal != 0 && (signals & whereSignal) != 0) {
            /* No IsRexxMsg() gate here -- see HandleWhereMessage()'s
             * own doc comment for why not, and why that's the
             * correct choice for a port dedicated solely to this one
             * protocol, not a shortcut. Every message on this port
             * must still be replied by its receiver, or the sender
             * leaks/hangs waiting for a reply that never comes --
             * HandleWhereMessage() always does, on every path. */
            struct RexxMsg *msg;
            while ((msg = (struct RexxMsg *)GetMsg(wherePort)) != NULL) {
                HandleWhereMessage(msg);
            }
        }

        if ((signals & windowSignal) == 0) {
            continue;
        }

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

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 37);
    WindowBase = OpenLibrary((CONST_STRPTR)"window.class", 44);
    LayoutBase = OpenLibrary((CONST_STRPTR)"gadgets/layout.gadget", 44);
    ButtonBase = OpenLibrary((CONST_STRPTR)"gadgets/button.gadget", 44);
    CheckBoxBase = OpenLibrary((CONST_STRPTR)"gadgets/checkbox.gadget", 44);
    StringBase = OpenLibrary((CONST_STRPTR)"gadgets/string.gadget", 44);
    RexxSysBase = (struct RxsLib *)OpenLibrary((CONST_STRPTR)"rexxsyslib.library", 0);

    Diag(IntuitionBase != NULL ? "caapp: intuition.library ok\n" : "caapp: intuition.library FAILED\n");
    Diag(WindowBase != NULL ? "caapp: window.class ok\n" : "caapp: window.class FAILED\n");
    Diag(LayoutBase != NULL ? "caapp: layout.gadget ok\n" : "caapp: layout.gadget FAILED\n");
    Diag(ButtonBase != NULL ? "caapp: button.gadget ok\n" : "caapp: button.gadget FAILED\n");
    Diag(CheckBoxBase != NULL ? "caapp: checkbox.gadget ok\n" : "caapp: checkbox.gadget FAILED\n");
    Diag(StringBase != NULL ? "caapp: string.gadget ok\n" : "caapp: string.gadget FAILED\n");
    /* Optional -- see RexxSysBase's own doc comment above. */
    Diag(RexxSysBase != NULL ? "caapp: rexxsyslib.library ok\n" : "caapp: rexxsyslib.library FAILED (no WHERE port)\n");

    if (IntuitionBase == NULL || WindowBase == NULL || LayoutBase == NULL
        || ButtonBase == NULL || CheckBoxBase == NULL || StringBase == NULL) {
        CleanExit(NULL, RETURN_FAIL);
    }

    g_connectButton = NewObject(NULL, (CONST_STRPTR)"button.gadget",
                                 GA_ID, GID_CONNECT,
                                 GA_Text, (ULONG)"Connect",
                                 GA_RelVerify, TRUE,
                                 TAG_DONE);

    /* STRING_GetClass()/CHECKBOX_GetClass(), not NewObject(NULL, "name"):
     * unlike button.gadget, these classes don't register a public class
     * name, so lookup-by-name returns NULL (confirmed empirically under
     * WB 3.2.3 -- button ok, string/checkbox FAILED via the name form). */
    g_hostString = NewObject(STRING_GetClass(), NULL,
                              GA_ID, GID_HOST,
                              GA_Text, (ULONG)"Host:",
                              STRINGA_TextVal, (ULONG)"",
                              STRINGA_MaxChars, 64,
                              TAG_DONE);

    g_enabledCheckbox = NewObject(CHECKBOX_GetClass(), NULL,
                                   GA_ID, GID_ENABLED,
                                   GA_Text, (ULONG)"Enabled",
                                   CHECKBOX_Checked, FALSE,
                                   CHECKBOX_TextPlace, CHECKBOX_TEXT_RIGHT,
                                   TAG_DONE);

    Diag(g_connectButton != NULL ? "caapp: button obj ok\n" : "caapp: button obj FAILED\n");
    Diag(g_hostString != NULL ? "caapp: string obj ok\n" : "caapp: string obj FAILED\n");
    Diag(g_enabledCheckbox != NULL ? "caapp: checkbox obj ok\n" : "caapp: checkbox obj FAILED\n");

    if (g_connectButton == NULL || g_hostString == NULL || g_enabledCheckbox == NULL) {
        CleanExit(NULL, RETURN_FAIL);
    }

    mainLayout = NewObject(LAYOUT_GetClass(), NULL,
                            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                            LAYOUT_SpaceInner, TRUE,
                            LAYOUT_SpaceOuter, TRUE,
                            LAYOUT_AddChild, (ULONG)g_connectButton,
                            LAYOUT_AddChild, (ULONG)g_hostString,
                            LAYOUT_AddChild, (ULONG)g_enabledCheckbox,
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

    g_wherePort = OpenWherePort();
    Diag(g_wherePort != NULL ? "caapp: " CAAPP_WHERE_PORT " port ok\n"
                              : "caapp: " CAAPP_WHERE_PORT " port FAILED\n");

    Diag("caapp: window open, entering event loop\n");

    ProcessEvents(windowObject, g_wherePort);

    /* Observable for the WHERE end-to-end test (tests/copperline/
     * where-test.py): proves a TYPE @host_field click-then-type
     * genuinely landed in the layout child's own string gadget --
     * GETTEXT can't read it back (the same layout.gadget-child limit
     * this whole feature exists to work around), so this Diag()
     * line, redirected to a host-readable log by the smoke script, is
     * the only way to confirm it from outside. Logged before either
     * exit path (close-gadget or the Connect button) so it always
     * appears regardless of how the loop ended. */
    {
        char logbuf[96];
        STRPTR hostText = NULL;
        GetAttr(STRINGA_TextVal, g_hostString, (ULONG *)&hostText);
        sprintf(logbuf, "caapp: host=%s\n", hostText != NULL ? (const char *)hostText : "");
        Diag(logbuf);
        DiagFile("SRC:build/caapp-log.txt", logbuf);
    }

    DoMethod(windowObject, WM_CLOSE);
    CleanExit(windowObject, RETURN_OK);
    return RETURN_OK; /* unreachable, CleanExit() calls exit() */
}
