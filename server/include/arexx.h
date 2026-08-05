/* arexx.h -- ARexx port for the AmiPilot server commodity (phase 0.2).
 *
 * A genuine public RexxMsg port ("AMIPILOT.<n>"). Port creation/teardown
 * and RexxMsg mechanics live here; command parsing itself is portable
 * (arexx_cmd.h), so only this file's RexxMsg glue is Amiga-only -- same
 * split ../amiauth's arexx.h/arexx_cmd.h use.
 */
#ifndef AMIPILOT_AREXX_H
#define AMIPILOT_AREXX_H

#include <stddef.h>
#include <exec/types.h>
#include <exec/ports.h>

#include "arexx_cmd.h"

/* Opens the port. Tries "AMIPILOT.1", "AMIPILOT.2", ... under one
 * Forbid() (the standard <BASENAME>.<slot#> convention) so two AmiPilot
 * server instances launched at once can't race onto the same name.
 * Copies the actual name used into outName (outNameCap bytes, for
 * display) if outName is non-NULL. Returns the port, or NULL if
 * RexxSysBase is NULL (the caller's own library-open code must set it
 * first) or no port could be created -- absence just means no ARexx
 * port, not a fatal error. */
struct MsgPort *AmipArexxOpen(const char *portNameOverride,
                               char *outName, size_t outNameCap);

/* Replies any still-queued message with AMIP_AREXX_RC_FAIL, then
 * RemPort + DeleteMsgPort. */
void AmipArexxClose(struct MsgPort *port);

/* Pulls the next genuine ARexx message off port (validated via
 * IsRexxMsg(); anything else is silently dropped -- nothing but the
 * ARexx interpreter should ever PutMsg() to a public ARexx-named port)
 * and parses it into out via AmipArexxParse(). Returns an opaque handle
 * for AmipArexxReply(), or NULL once the port is drained for this
 * signal. */
void *AmipArexxReceive(struct MsgPort *port, AmipArexxParsed *out);

/* Replies to the message handle identifies (from AmipArexxReceive).
 * Always sets the RC; only builds a RESULT argstring if the caller
 * actually asked for one (RXFF_RESULT) -- result may be NULL/empty
 * either way. The argstring (if any) is never freed here: ReplyMsg()
 * hands ownership to the ARexx interpreter, which frees it after
 * consuming the reply -- do not "fix" this into a leak by adding a
 * DeleteArgstring call. */
void AmipArexxReply(void *handle, int rc, const char *result);

#endif /* AMIPILOT_AREXX_H */
