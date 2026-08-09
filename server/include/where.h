/* where.h -- the cooperative geometry port query (issue #49,
 * docs/implementation-plan.md's "future tier between 1 and 2"). A
 * WHEREGADGET manifest entry (manifest/SPEC.md) names an ARexx port an
 * application exposes itself, answering "WHERE <logical-name>" with its
 * own live GetAttr(GA_Left/GA_Top/GA_Width/GA_Height) geometry. This
 * module sends that query and parses the reply; the caller (amipilot
 * server's own HandleCommand()) turns the resulting geometry into a
 * real input.device click via AmipClickWindowRelative()
 * (action_engine.h).
 *
 * Deliberately NOT built on top of muirexx.c's AmipMuiRexxSend(): that
 * function's port-name resolution tries the declared name verbatim,
 * then falls back to "<name>.1" -- a real MUI naming convention that
 * has no place here (manifest/SPEC.md's "Clash guard" section). A
 * WHEREPORT name that doesn't exist under the exact spelling the
 * manifest declared must fail loudly (port-not-found), not silently
 * probe a related name -- so this module resolves the port itself,
 * separately, with no fallback, even though the actual RexxMsg
 * send/poll/reply mechanics below are otherwise the same shape as
 * muirexx.c's own (a second, deliberate copy -- same "separate copies
 * on purpose" convention arexx_cmd.c/manifest.c's shared portable
 * helpers already follow, here because the two are subtly different on
 * purpose, not by oversight). */
#ifndef AMIPILOT_WHERE_H
#define AMIPILOT_WHERE_H

#include <stddef.h>

typedef enum {
    AMIP_WHERE_OK = 0,       /* sent, replied, app RC was 0 -- x/y/w/h filled in */
    AMIP_WHERE_APP_ERROR,    /* sent, replied, app RC was nonzero -- the
                              * TARGET app's own rejection (usually
                              * "unknown name"), not a transport
                              * problem; appText filled in if the app
                              * gave a reason */
    AMIP_WHERE_PORT_NOT_FOUND, /* no ARexx port found under the exact
                              * declared name -- no ".1" fallback, see
                              * this header's own doc comment above */
    AMIP_WHERE_TIMEOUT,      /* sent, but no reply within timeoutSeconds */
    AMIP_WHERE_BAD_REPLY,    /* replied with RC 0, but rm_Result2 did
                              * not parse as exactly four decimal
                              * integers -- a malformed WHERE
                              * implementation, not a normal error */
    AMIP_WHERE_ALLOC_FAIL    /* CreateMsgPort()/CreateRexxMsg()/
                              * FillRexxMsg() failed (out of memory) */
} AmipWhereResult;

/* Sends "WHERE <logicalName>" to the ARexx port named `portName` (exact
 * match only -- see doc comment above), polling for up to
 * `timeoutSeconds` (0 = a 10s default, matching MUIREXX's own). On
 * AMIP_WHERE_OK, *x, *y, *w, *h are the parsed window-relative geometry
 * (manifest/SPEC.md's "The cooperative geometry port" section: pixels,
 * including the window's own border/title bar -- add the window's
 * LeftEdge/TopEdge, nothing else, to get screen coordinates). On
 * AMIP_WHERE_APP_ERROR, appText (if non-NULL/nonzero-cap) is filled
 * with whatever reason text the app gave (empty string if none). On
 * every other outcome, none of the out parameters are touched. */
AmipWhereResult AmipWhereQuery(const char *portName, const char *logicalName,
                                long timeoutSeconds,
                                long *x, long *y, long *w, long *h,
                                char *appText, size_t appTextCap);

#endif /* AMIPILOT_WHERE_H */
