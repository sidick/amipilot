/* fs.h -- allowlist-scoped file API (phase 0.4).
 *
 * "On real hardware over TCP -- exactly the audience without shared
 * drives or host-mounted filesystems -- the control channel is the
 * only road in" (docs/implementation-plan.md, "File system access").
 * Disabled entirely until the server is started with at least one
 * FSROOT grant -- nothing is granted implicitly, and every verb
 * refuses a path outside every granted root with a clear error naming
 * the granted list (AmipFsDescribeRoots).
 *
 * Containment is checked by LOCK IDENTITY (Lock() + ParentDir() +
 * SameLock(), all V36), never by string prefix matching: Amiga
 * assigns mean two different path strings can name the same or a
 * nested location (e.g. an assign pointing elsewhere, or a multi-
 * directory assign), so string comparison would be both wrong (missing
 * genuine matches) and unsafe (missing genuine escapes). See fs.c's
 * own IsUnderGrantedRoot() for the exact walk and why ParentDir()
 * returning 0 at a volume root is NOT a "reached a root" sentinel.
 */
#ifndef AMIPILOT_FS_H
#define AMIPILOT_FS_H

#include <exec/types.h>

/* Locks `path` (ACCESS_READ) and keeps it open for the server's
 * lifetime as a granted root. Returns FALSE if the path couldn't be
 * locked (bad path, doesn't exist); errOut (cap errCap, may be NULL)
 * gets a one-line reason. Call once per FSROOT at startup, before any
 * fs verb is served. */
BOOL AmipFsGrantRoot(const char *path, char *errOut, int errCap);

/* Releases every granted root's lock. Call once at shutdown. */
void AmipFsShutdown(void);

/* TRUE once at least one root has been granted. */
BOOL AmipFsEnabled(void);

/* Fills `out` (cap outCap) with a comma-separated list of the granted
 * roots' own path strings, for error messages that name what's
 * allowed -- truncates silently if the list doesn't fit, same
 * graceful-degradation policy as this project's other bounded
 * buffers. */
void AmipFsDescribeRoots(char *out, int outCap);

/* Verbs. Each returns an AMIP_AREXX_RC_* code (arexx_cmd.h) and points
 * *resultOut at fs.c's own internal static result buffer (valid until
 * the next fs call -- same convention amipilotserver/main.c's own
 * g_resultBuf/g_treeBuf use), with *outLen its length. On success this
 * is the verb's own result (a listing, a stat line, "created"/
 * "deleted", or FSGET's raw file bytes -- which may contain embedded
 * NULs, hence the explicit length rather than relying on strlen()).
 * On failure it's a one-line human-readable reason. */
int AmipFsList(const char *path, const char **resultOut, ULONG *outLen);
int AmipFsStat(const char *path, const char **resultOut, ULONG *outLen);
int AmipFsMkdir(const char *path, const char **resultOut, ULONG *outLen);
int AmipFsDelete(const char *path, const char **resultOut, ULONG *outLen);
int AmipFsGet(const char *path, const char **resultOut, ULONG *outLen);

/* Writes `len` bytes from `data` to `path`, creating it if it doesn't
 * exist and overwriting it if it does (MODE_NEWFILE handles both the
 * same way) -- containment checked against `path`'s PARENT directory,
 * same shape as AmipFsMkdir(), since the target itself may not exist
 * yet. `data`/`len` are the raw payload a caller must have already
 * received in full off the wire (phase 1.0's FSPUT; see server/
 * WIRE.md's request-payload framing) -- this function has no
 * transport awareness of its own. */
int AmipFsPut(const char *path, const void *data, ULONG len,
              const char **resultOut, ULONG *outLen);

#endif /* AMIPILOT_FS_H */
