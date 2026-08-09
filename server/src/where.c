/* where.c -- see where.h. AmigaOS only (never built into a host tool),
 * same split as muirexx.c/arexx.c. */
#include <exec/types.h>
#include <exec/ports.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/rexxsyslib.h>
#include <rexx/storage.h>
#include <rexx/rxslib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "where.h"

/* Opened once by amipilotserver's main.c, shared across every file that
 * talks to rexxsyslib.library -- arexx.c is the one owner; declared
 * extern here, not redefined, same as muirexx.c's own. */
extern struct RxsLib *RexxSysBase;

#define AMIP_WHERE_POLL_TICKS 5      /* ~100ms, same granularity MUIREXX's own polling uses */
#define AMIP_WHERE_DEFAULT_TIMEOUT 10 /* seconds, matching MUIREXX's own default */

/* Reads one decimal integer (optional leading '-', at least one digit)
 * starting at *p, skipping any leading whitespace first; advances *p
 * past it. Returns 0 (leaving *p unmoved past the whitespace skip) if
 * no digit is found. Hand-rolled rather than sscanf("%ld%n", ...):
 * libnix's own minimal sscanf does not honor "%n" (confirmed live --
 * it silently leaves the count untouched, which made an earlier
 * version of this parser accept every reply as "malformed" since the
 * untouched consumed-count of 0 made the trailing-content check see
 * the whole string as junk) -- see the libnix skill's own notes on
 * libnix's minimal stdio. */
static int ReadLong(const char **p, long *out)
{
    const char *s = *p;
    int neg = 0;
    long value = 0;
    int sawDigit = 0;

    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') {
        neg = 1;
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (*s - '0');
        s++;
        sawDigit = 1;
    }
    if (!sawDigit) {
        return 0;
    }
    *out = neg ? -value : value;
    *p = s;
    return 1;
}

/* Strictly parses "<x> <y> <w> <h>" -- exactly four decimal integers,
 * separated by whitespace, with nothing else (leading/trailing
 * whitespace tolerated). A malformed reply is a bug in the target
 * application's own WHERE implementation, not an ordinary error, so
 * this deliberately does not try to salvage a partial parse. */
static int ParseGeometry(const char *text, long *x, long *y, long *w, long *h)
{
    long vx, vy, vw, vh;
    const char *p;

    if (text == NULL) return 0;
    p = text;
    if (!ReadLong(&p, &vx)) return 0;
    if (!ReadLong(&p, &vy)) return 0;
    if (!ReadLong(&p, &vw)) return 0;
    if (!ReadLong(&p, &vh)) return 0;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\0') return 0;

    *x = vx; *y = vy; *w = vw; *h = vh;
    return 1;
}

AmipWhereResult AmipWhereQuery(const char *portName, const char *logicalName,
                                long timeoutSeconds,
                                long *x, long *y, long *w, long *h,
                                char *appText, size_t appTextCap)
{
    struct MsgPort *replyPort;
    struct MsgPort *target;
    struct RexxMsg *msg;
    struct RexxMsg *reply;
    char command[64 + 32]; /* "WHERE " + a manifest logical name */
    ULONG ticksTotal;
    ULONG ticksWaited = 0;
    AmipWhereResult outcome;

    if (RexxSysBase == NULL) {
        return AMIP_WHERE_ALLOC_FAIL;
    }

    /* Exact match only -- no "<name>.1" fallback (see where.h's own
     * doc comment on why this deliberately does not share
     * muirexx.c's FindTargetPort()). */
    Forbid();
    target = FindPort((CONST_STRPTR)portName);
    Permit();
    if (target == NULL) {
        return AMIP_WHERE_PORT_NOT_FOUND;
    }

    replyPort = CreateMsgPort();
    if (replyPort == NULL) {
        return AMIP_WHERE_ALLOC_FAIL;
    }

    msg = CreateRexxMsg(replyPort, NULL, NULL);
    if (msg == NULL) {
        DeleteMsgPort(replyPort);
        return AMIP_WHERE_ALLOC_FAIL;
    }

    snprintf(command, sizeof(command), "WHERE %s", logicalName);
    msg->rm_Args[0] = (STRPTR)command;
    if (!FillRexxMsg(msg, 1, 0)) {
        DeleteRexxMsg(msg);
        DeleteMsgPort(replyPort);
        return AMIP_WHERE_ALLOC_FAIL;
    }
    msg->rm_Action = RXCOMM | RXFF_RESULT;

    /* Re-resolve under the SAME Forbid() as the send -- closes the gap
     * a quitting target could otherwise fall into, same reasoning as
     * muirexx.c's own AmipMuiRexxSend(). */
    Forbid();
    target = FindPort((CONST_STRPTR)portName);
    if (target != NULL) {
        PutMsg(target, (struct Message *)msg);
    }
    Permit();

    if (target == NULL) {
        DeleteRexxMsg(msg);
        DeleteMsgPort(replyPort);
        return AMIP_WHERE_PORT_NOT_FOUND;
    }

    ticksTotal = (ULONG)(timeoutSeconds > 0 ? timeoutSeconds : AMIP_WHERE_DEFAULT_TIMEOUT) * 50;
    for (;;) {
        reply = (struct RexxMsg *)GetMsg(replyPort);
        if (reply != NULL) {
            break;
        }
        if (ticksWaited >= ticksTotal) {
            /* Deliberately leaked, same as muirexx.c's own timeout
             * path -- the target may still own/reply to *msg, so
             * deleting it out from under an in-flight send would be
             * the real bug. */
            DeleteMsgPort(replyPort);
            return AMIP_WHERE_TIMEOUT;
        }
        Delay(AMIP_WHERE_POLL_TICKS);
        ticksWaited += AMIP_WHERE_POLL_TICKS;
    }

    if (appText != NULL && appTextCap > 0) {
        appText[0] = '\0';
    }

    if (reply->rm_Result1 == 0) {
        const char *replyText = (reply->rm_Result2 != 0)
            ? (const char *)reply->rm_Result2 : "";
        if (ParseGeometry(replyText, x, y, w, h)) {
            outcome = AMIP_WHERE_OK;
        } else {
            outcome = AMIP_WHERE_BAD_REPLY;
            if (appText != NULL && appTextCap > 0) {
                strncpy(appText, replyText, appTextCap - 1);
                appText[appTextCap - 1] = '\0';
            }
        }
    } else {
        outcome = AMIP_WHERE_APP_ERROR;
        if (appText != NULL && appTextCap > 0 && reply->rm_Result2 != 0) {
            strncpy(appText, (const char *)reply->rm_Result2, appTextCap - 1);
            appText[appTextCap - 1] = '\0';
        }
    }

    if (reply->rm_Result2 != 0) {
        DeleteArgstring((UBYTE *)reply->rm_Result2);
    }
    DeleteRexxMsg(reply);
    DeleteMsgPort(replyPort);

    return outcome;
}
