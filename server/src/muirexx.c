/* muirexx.c -- see muirexx.h. AmigaOS only (never built into a host
 * tool), same split as arexx.c. */
#include <exec/types.h>
#include <exec/ports.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/rexxsyslib.h>
#include <rexx/storage.h>
#include <rexx/rxslib.h>
#include <string.h>

#include "muirexx.h"

/* Opened once by amipilotserver's main.c and shared across every file
 * that talks to rexxsyslib.library -- same global arexx.c already
 * defines (and the one <proto/rexxsyslib.h>'s inline stubs reference
 * by name); declared extern here, not redefined, since arexx.c is the
 * one owner. */
extern struct RxsLib *RexxSysBase;

#define AMIP_MUIREXX_POLL_TICKS 5      /* ~100ms, same granularity WAITFOR's own polling uses */
#define AMIP_MUIREXX_DEFAULT_TIMEOUT 10 /* seconds, matching WAITFOR's own default */

/* Tries `base` verbatim first, then "<base>.1" -- see AmipMuiRexxSend's
 * own doc comment for why both. Must be called under Forbid() (the
 * caller's job, so it can PutMsg() to the same result without a gap a
 * concurrent RemPort() could fall into). */
static struct MsgPort *FindTargetPort(const char *base)
{
    struct MsgPort *port;
    char name[40]; /* base (<=30 per MUI's own naming rule) + ".1" + slack */

    port = FindPort((CONST_STRPTR)base);
    if (port != NULL) {
        return port;
    }

    strncpy(name, base, sizeof(name) - 3);
    name[sizeof(name) - 3] = '\0';
    strcat(name, ".1");
    return FindPort((CONST_STRPTR)name);
}

AmipMuiRexxResult AmipMuiRexxSend(const char *base, const char *command,
                                   long timeoutSeconds, long *appRC,
                                   char *result, size_t resultCap)
{
    struct MsgPort *replyPort;
    struct MsgPort *target;
    struct RexxMsg *msg;
    struct RexxMsg *reply;
    ULONG ticksTotal;
    ULONG ticksWaited = 0;
    AmipMuiRexxResult outcome;

    if (RexxSysBase == NULL) {
        return AMIP_MUIREXX_ALLOC_FAIL;
    }

    Forbid();
    target = FindTargetPort(base);
    Permit();
    if (target == NULL) {
        return AMIP_MUIREXX_NOT_FOUND;
    }

    replyPort = CreateMsgPort();
    if (replyPort == NULL) {
        return AMIP_MUIREXX_ALLOC_FAIL;
    }

    msg = CreateRexxMsg(replyPort, NULL, NULL);
    if (msg == NULL) {
        DeleteMsgPort(replyPort);
        return AMIP_MUIREXX_ALLOC_FAIL;
    }

    msg->rm_Args[0] = (STRPTR)command;
    if (!FillRexxMsg(msg, 1, 0)) {
        DeleteRexxMsg(msg);
        DeleteMsgPort(replyPort);
        return AMIP_MUIREXX_ALLOC_FAIL;
    }
    msg->rm_Action = RXCOMM | RXFF_RESULT;

    /* Re-resolve under the SAME Forbid() as the send -- the pair above
     * only proves the port existed a moment ago; closing that (already
     * tiny) window matters here since a hostile/quitting target is
     * exactly the kind of thing a test author would be probing for. */
    Forbid();
    target = FindTargetPort(base);
    if (target != NULL) {
        PutMsg(target, (struct Message *)msg);
    }
    Permit();

    if (target == NULL) {
        DeleteRexxMsg(msg);
        DeleteMsgPort(replyPort);
        return AMIP_MUIREXX_NOT_FOUND;
    }

    ticksTotal = (ULONG)(timeoutSeconds > 0 ? timeoutSeconds : AMIP_MUIREXX_DEFAULT_TIMEOUT) * 50;
    for (;;) {
        reply = (struct RexxMsg *)GetMsg(replyPort);
        if (reply != NULL) {
            break;
        }
        if (ticksWaited >= ticksTotal) {
            /* The target may still reply after we give up -- that
             * reply just lands on a port nothing is listening to
             * anymore rather than being read, since deleting *msg out
             * from under an in-flight send would be the real bug
             * (the target still owns a pointer to it). Leak-shaped
             * but safe: one message, once, on a genuine timeout, not
             * a loop -- same "never pull the rug out from under an
             * in-flight reply" reasoning connect_with_retry() applies
             * to its own socket (host/amipilot/client.py). */
            DeleteMsgPort(replyPort);
            return AMIP_MUIREXX_TIMEOUT;
        }
        Delay(AMIP_MUIREXX_POLL_TICKS);
        ticksWaited += AMIP_MUIREXX_POLL_TICKS;
    }

    if (appRC != NULL) {
        *appRC = reply->rm_Result1;
    }
    if (result != NULL && resultCap > 0) {
        result[0] = '\0';
        if (reply->rm_Result2 != 0) {
            strncpy(result, (const char *)reply->rm_Result2, resultCap - 1);
            result[resultCap - 1] = '\0';
        }
    }
    outcome = (reply->rm_Result1 == 0) ? AMIP_MUIREXX_OK : AMIP_MUIREXX_APP_ERROR;

    if (reply->rm_Result2 != 0) {
        DeleteArgstring((UBYTE *)reply->rm_Result2);
    }
    DeleteRexxMsg(reply);
    DeleteMsgPort(replyPort);

    return outcome;
}
