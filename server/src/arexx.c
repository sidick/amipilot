/* arexx.c -- see arexx.h. AmigaOS only (never built into a host tool). */
#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/ports.h>
#include <proto/exec.h>
#include <proto/rexxsyslib.h>
#include <rexx/storage.h>
#include <rexx/rxslib.h>

#include <stdio.h>
#include <string.h>

#include "arexx.h"

/* Defined here (the one file that actually calls rexxsyslib functions);
 * <proto/rexxsyslib.h>'s inline stubs reference this exact global by
 * name -- same convention as IntuitionBase/GadToolsBase/KeymapBase
 * elsewhere in this project, and ../amiauth's own RexxSysBase. Must NOT
 * be static. The commodity's main.c owns OpenLibrary()/CloseLibrary(). */
struct RxsLib *RexxSysBase = NULL;

/* Try slots 1..99 (generous, arbitrary -- realistically 1-2 instances
 * ever run) under one Forbid() so two AmiPilot server instances launched
 * at once can't race onto the same name. */
#define AMIP_AREXX_MAX_SLOT 99

struct MsgPort *AmipArexxOpen(const char *portNameOverride,
                               char *outName, size_t outNameCap)
{
    static char name[32]; /* "AMIPILOT.NN" -- static: AddPort() keeps a
                            * reference, must outlive the port itself */
    struct MsgPort *port = NULL;

    if (RexxSysBase == NULL) {
        return NULL;
    }

    Forbid();
    if (portNameOverride != NULL && portNameOverride[0] != '\0') {
        strncpy(name, portNameOverride, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        if (FindPort((CONST_STRPTR)name) == NULL) {
            port = CreateMsgPort();
            if (port != NULL) {
                port->mp_Node.ln_Name = name;
                AddPort(port);
            }
        }
    } else {
        int n;
        for (n = 1; n <= AMIP_AREXX_MAX_SLOT; n++) {
            sprintf(name, "AMIPILOT.%d", n);
            if (FindPort((CONST_STRPTR)name) == NULL) {
                port = CreateMsgPort();
                if (port != NULL) {
                    port->mp_Node.ln_Name = name;
                    AddPort(port);
                }
                break;
            }
        }
    }
    Permit();

    if (port != NULL && outName != NULL && outNameCap > 0) {
        strncpy(outName, name, outNameCap - 1);
        outName[outNameCap - 1] = '\0';
    }
    return port;
}

void AmipArexxClose(struct MsgPort *port)
{
    struct RexxMsg *msg;

    if (port == NULL) {
        return;
    }

    Forbid();
    RemPort(port);
    Permit();

    while ((msg = (struct RexxMsg *)GetMsg(port)) != NULL) {
        /* Same caution as AmipArexxReceive(): only touch RexxMsg-specific
         * fields (via AmipArexxReply) once IsRexxMsg confirms the layout. */
        if (IsRexxMsg(msg)) {
            AmipArexxReply(msg, AMIP_AREXX_RC_FAIL, NULL);
        } else {
            ReplyMsg((struct Message *)msg);
        }
    }
    DeleteMsgPort(port);
}

void *AmipArexxReceive(struct MsgPort *port, AmipArexxParsed *out)
{
    struct RexxMsg *msg;

    while ((msg = (struct RexxMsg *)GetMsg(port)) != NULL) {
        if (!IsRexxMsg(msg)) {
            /* Not ours; shouldn't happen on a dedicated ARexx port
             * ("AMIPILOT.N"), but every AmigaOS message must be replied
             * by its receiver or the sender leaks/hangs waiting on a
             * reply that never comes -- drop only the assumption that
             * it's a RexxMsg, not the reply itself. Plain ReplyMsg()
             * only touches the generic Message fields (guaranteed by
             * whoever sent it), unlike AmipArexxReply() which writes
             * RexxMsg-specific fields we can't assume are there. */
            ReplyMsg((struct Message *)msg);
            continue;
        }
        if (AmipArexxParse((const char *)ARG0(msg), out) != 0) {
            out->type = AMIP_AREXX_CMD_UNKNOWN; /* still reply -- RC 10, see caller */
        }
        return (void *)msg;
    }
    return NULL;
}

void AmipArexxReply(void *handle, int rc, const char *result)
{
    struct RexxMsg *msg = (struct RexxMsg *)handle;

    if (msg == NULL) {
        return;
    }

    msg->rm_Result1 = rc;
    msg->rm_Result2 = 0;
    if ((msg->rm_Action & RXFF_RESULT) && result != NULL && result[0] != '\0') {
        msg->rm_Result2 = (LONG)CreateArgstring((UBYTE *)result, (ULONG)strlen(result));
    }
    ReplyMsg((struct Message *)msg);
    /* Do not DeleteArgstring(rm_Result2) here -- ARexx frees it after
     * consuming the reply. See arexx.h's note on this. */
}
