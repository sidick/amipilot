/* errbuf.h -- the one-line-error-into-a-static-buffer body every verb
 * module's own SetErr() needs (fs.c/wblaunch.c/screenshot.c each
 * carried an identical copy of this before code review consolidated
 * it here).
 *
 * Deliberately NOT a shared buffer across modules -- each caller
 * keeps its own static buffer (g_fsBuf/g_resultBuf/etc), matching the
 * existing one-buffer-per-verb-family convention this codebase
 * already uses (e.g. FSGET/FSPUT's own g_fsBuf is distinct from
 * SCREENSHOT's). This header only removes the duplicated THREE-LINE
 * BODY that fills whichever buffer a caller already owns; it changes
 * no ownership or lifetime semantics anywhere. `static` here means
 * each including .c file gets its own private copy, the ordinary
 * (and only) way to share a small function across independently-
 * compiled translation units without a new .c/.o of its own.
 */
#ifndef AMIPILOT_ERRBUF_H
#define AMIPILOT_ERRBUF_H

#include <exec/types.h>
#include <string.h>

/* Copies `msg` into `buf` (cap `bufCap`), NUL-capped, and points
 * `*resultOut`/`*outLen` at it. Callers keep their own thin SetErr()
 * wrapper (same name/signature every call site already uses) so this
 * consolidation needed zero changes anywhere else in each file. */
static void AmipSetErrBuf(char *buf, int bufCap, const char **resultOut,
                          ULONG *outLen, const char *msg)
{
    strncpy(buf, msg, (size_t)(bufCap - 1));
    buf[bufCap - 1] = '\0';
    *resultOut = buf;
    *outLen = (ULONG)strlen(buf);
}

#endif /* AMIPILOT_ERRBUF_H */
