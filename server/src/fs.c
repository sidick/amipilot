/* fs.c -- see fs.h.
 *
 * Real functions confirmed against the NDK autodocs before use, not
 * guessed:
 *   - SameLock() (V36) returns LOCK_SAME/LOCK_SAME_VOLUME/
 *     LOCK_DIFFERENT; ParentDir() (V36) of a volume root returns lock
 *     0, which represents the BOOT volume's own root (its documented
 *     "in effect, the parent of all other file system roots"), not a
 *     generic "no parent" sentinel -- so IsUnderGrantedRoot()'s walk
 *     must treat reaching 0 as "outside every granted root", never as
 *     "found a root".
 *   - dos.h's own comment: "Regular RWED bits are 0 == allowed. NOTE:
 *     GRP and OTR RWED permissions are 0 == not allowed!" -- the
 *     classic owner rwed string this file renders is the INVERTED
 *     sense (bit set = flag absent), unlike the GRP_ and OTR_ bits,
 *     which this file doesn't surface at all (matching what AmigaDOS's own
 *     `List`/`Info` commands show).
 *   - ExAll() (V36, "advised you only use this under V37 and later" --
 *     exactly this project's own floor) with ED_COMMENT: ED_OWNER
 *     needs V39 and V37 filesystems reject it with ERROR_BAD_NUMBER,
 *     so this file never asks for it.
 */
#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/exall.h>
#include <dos/datetime.h>

#include "arexx_cmd.h"
#include "errbuf.h"
#include "fs.h"

#define AMIP_FS_MAX_ROOTS 8
#define AMIP_FS_ROOT_PATH 128
#define AMIP_FS_EXALL_BUF 1024
/* The one shared result buffer every verb writes into (only one fs
 * command is ever in flight at a time -- same single-threaded-
 * dispatch safety as main.c's own static result buffers). Also
 * FSGET's hard cap: "a test-staging channel, not a file manager"
 * (docs/implementation-plan.md) -- 16K comfortably covers icons,
 * configs, and small test fixtures without an unbounded allocation on
 * this project's plain-68000/~1MB floor. */
#define AMIP_FS_BUF_SIZE 16384

typedef struct {
    BPTR lock;
    char path[AMIP_FS_ROOT_PATH];
} AmipFsRoot;

static AmipFsRoot g_fsRoots[AMIP_FS_MAX_ROOTS];
static int g_fsRootCount = 0;
static char g_fsBuf[AMIP_FS_BUF_SIZE];

static void SetErr(const char **resultOut, ULONG *outLen, const char *msg)
{
    AmipSetErrBuf(g_fsBuf, sizeof(g_fsBuf), resultOut, outLen, msg);
}

BOOL AmipFsGrantRoot(const char *path, char *errOut, int errCap)
{
    BPTR lock;

    if (g_fsRootCount >= AMIP_FS_MAX_ROOTS) {
        if (errOut != NULL) {
            strncpy(errOut, "too many FSROOT grants", errCap - 1);
            errOut[errCap - 1] = '\0';
        }
        return FALSE;
    }
    lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    if (lock == 0) {
        if (errOut != NULL) {
            strncpy(errOut, "could not lock path", errCap - 1);
            errOut[errCap - 1] = '\0';
        }
        return FALSE;
    }
    g_fsRoots[g_fsRootCount].lock = lock;
    strncpy(g_fsRoots[g_fsRootCount].path, path, AMIP_FS_ROOT_PATH - 1);
    g_fsRoots[g_fsRootCount].path[AMIP_FS_ROOT_PATH - 1] = '\0';
    g_fsRootCount++;
    return TRUE;
}

void AmipFsShutdown(void)
{
    int i;

    for (i = 0; i < g_fsRootCount; i++) {
        UnLock(g_fsRoots[i].lock);
    }
    g_fsRootCount = 0;
}

BOOL AmipFsEnabled(void)
{
    return g_fsRootCount > 0;
}

void AmipFsDescribeRoots(char *out, int outCap)
{
    int i;

    out[0] = '\0';
    for (i = 0; i < g_fsRootCount; i++) {
        size_t used = strlen(out);
        if (i > 0 && used + 2 < (size_t)outCap) {
            strcat(out, ", ");
        }
        used = strlen(out);
        if ((int)used < outCap - 1) {
            strncat(out, g_fsRoots[i].path, (size_t)(outCap - (int)used - 1));
        }
    }
}

/* Walks up from `targetLock` via ParentDir(), checking SameLock()
 * against every granted root at each step. See this file's header
 * comment for why reaching lock 0 means "outside every granted root",
 * not "found a root". Does not consume/UnLock the caller's own
 * `targetLock`. */
static BOOL IsUnderGrantedRoot(BPTR targetLock)
{
    BPTR walk = targetLock;
    BOOL owned = FALSE;
    int i;

    for (;;) {
        for (i = 0; i < g_fsRootCount; i++) {
            if (SameLock(walk, g_fsRoots[i].lock) == LOCK_SAME) {
                if (owned) UnLock(walk);
                return TRUE;
            }
        }
        {
            BPTR parent = ParentDir(walk);
            if (owned) UnLock(walk);
            if (parent == 0) {
                return FALSE;
            }
            walk = parent;
            owned = TRUE;
        }
    }
}

static void DescribeRootsError(const char **resultOut, ULONG *outLen)
{
    char roots[200];
    char msg[256];

    AmipFsDescribeRoots(roots, sizeof(roots));
    snprintf(msg, sizeof(msg), "path outside granted roots: %s", roots);
    SetErr(resultOut, outLen, msg);
}

/* Locks `path` (an EXISTING file or directory) and confirms it's
 * under a granted root. Returns the lock (caller must UnLock it), or
 * 0 with *rcOut, *resultOut, and *outLen already set.
 *
 * KNOWN LIMITATION (TOCTOU, accepted, matching AmipIsWindowOpen()'s own
 * documented "shrinks the gap, doesn't close it entirely" precedent --
 * see action_engine.h): callers that need to act on the ORIGINAL PATH
 * STRING rather than the lock itself (AmipFsDelete()'s DeleteFile(),
 * AmipFsGet()'s Open()) must release this lock first, since AmigaDOS
 * (V37 floor) has no "open/delete relative to an already-held lock"
 * primitive for these operations. Between that UnLock() and the real
 * operation, an assign or a symlink component in the path could be
 * repointed so the path no longer resolves to the location this
 * function verified -- the containment check and the actual operation
 * are not atomic. This is a real, standing gap, not a hypothetical:
 * documented here rather than silently assumed closed. The general fix
 * (act on a lock/handle throughout, never re-resolve a path string) is
 * blocked on AmigaDOS's own API shape for these verbs and isn't
 * planned as a near-term fix. */
static BPTR ResolveExisting(const char *path, int *rcOut,
                            const char **resultOut, ULONG *outLen)
{
    BPTR lock;

    if (!AmipFsEnabled()) {
        *rcOut = AMIP_AREXX_RC_ERROR;
        SetErr(resultOut, outLen, "file API not enabled -- no FSROOT granted at startup");
        return 0;
    }
    lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    if (lock == 0) {
        *rcOut = AMIP_AREXX_RC_WARN;
        SetErr(resultOut, outLen, "no such file or directory");
        return 0;
    }
    if (!IsUnderGrantedRoot(lock)) {
        UnLock(lock);
        *rcOut = AMIP_AREXX_RC_ERROR;
        DescribeRootsError(resultOut, outLen);
        return 0;
    }
    return lock;
}

/* Renders the classic owner rwed string -- see this file's header
 * comment for the inverted-bits gotcha. `out` must be char[5]. */
static void RenderProtection(ULONG prot, char *out)
{
    out[0] = (prot & FIBF_READ)    ? '-' : 'r';
    out[1] = (prot & FIBF_WRITE)   ? '-' : 'w';
    out[2] = (prot & FIBF_EXECUTE) ? '-' : 'e';
    out[3] = (prot & FIBF_DELETE)  ? '-' : 'd';
    out[4] = '\0';
}

/* dateOut/timeOut must each be char[LEN_DATSTRING] (dos/datetime.h). */
static void RenderDate(LONG days, LONG mins, LONG ticks, char *dateOut, char *timeOut)
{
    struct DateTime dt;

    memset(&dt, 0, sizeof(dt));
    dt.dat_Stamp.ds_Days = days;
    dt.dat_Stamp.ds_Minute = mins;
    dt.dat_Stamp.ds_Tick = ticks;
    dt.dat_Format = FORMAT_DOS;
    dt.dat_Flags = 0;
    dt.dat_StrDay = NULL;
    dt.dat_StrDate = (STRPTR)dateOut;
    dt.dat_StrTime = (STRPTR)timeOut;

    if (!DateToStr(&dt)) {
        strcpy(dateOut, "?");
        strcpy(timeOut, "?");
    }
}

/* Escapes '"' and '\' as '\"'/'\\' so `name`/`comment` (arbitrary
 * AmigaDOS text -- a filename or comment can legitimately contain a
 * literal '"', e.g. a file commented "12\" disk") can be safely
 * delimited by the surrounding quotes below; without this, such an
 * entry produced a well-formed response every host parser's
 * fixed-format regex still couldn't match. Writes into `out` (cap
 * outCap), truncating silently if the escaped form would overflow --
 * a local stack buffer, not this file's shared static ones, since
 * AmigaDOS filenames/comments are already short (MAXFILENAMELEN-class
 * bounds), same size class as this function's own `rwed`/`dateStr`
 * locals just below. */
static void EscapeQuotesInto(const char *in, char *out, size_t outCap)
{
    size_t o = 0;

    if (in == NULL) {
        out[0] = '\0';
        return;
    }
    for (; *in != '\0' && o + 2 < outCap; in++) {
        if (*in == '"' || *in == '\\') {
            out[o++] = '\\';
        }
        out[o++] = *in;
    }
    out[o] = '\0';
}

static void AppendEntry(char *buf, size_t bufCap, const char *name, LONG type,
                        ULONG size, ULONG prot, LONG days, LONG mins, LONG ticks,
                        const char *comment)
{
    size_t used = strlen(buf);
    char rwed[5];
    char dateStr[LEN_DATSTRING], timeStr[LEN_DATSTRING];
    char nameEsc[256], commentEsc[256];

    if (used >= bufCap - 1) {
        return;
    }
    RenderProtection(prot, rwed);
    RenderDate(days, mins, ticks, dateStr, timeStr);
    EscapeQuotesInto(name, nameEsc, sizeof(nameEsc));
    EscapeQuotesInto(comment, commentEsc, sizeof(commentEsc));
    snprintf(buf + used, bufCap - used,
             "entry name=\"%s\" type=%s size=%lu prot=%s date=\"%s %s\" comment=\"%s\"\n",
             nameEsc,
             type < 0 ? "file" : "dir",
             (unsigned long)size, rwed, dateStr, timeStr,
             commentEsc);
}

int AmipFsList(const char *path, const char **resultOut, ULONG *outLen)
{
    int rc = AMIP_AREXX_RC_OK;
    BPTR lock;
    struct FileInfoBlock *fib;
    struct ExAllControl *eac;
    LONG more; /* ExAll()'s real prototype (clib/dos_protos.h) returns
                * LONG, not the BOOL the autodoc's simplified SYNOPSIS
                * line implies */

    lock = ResolveExisting(path, &rc, resultOut, outLen);
    if (lock == 0) {
        return rc;
    }

    fib = AllocDosObject(DOS_FIB, NULL);
    if (fib == NULL) {
        UnLock(lock);
        SetErr(resultOut, outLen, "out of memory");
        return AMIP_AREXX_RC_FAIL;
    }
    if (!Examine(lock, fib) || fib->fib_DirEntryType < 0) {
        FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
        SetErr(resultOut, outLen, "not a directory (use FSSTAT for a single file)");
        return AMIP_AREXX_RC_ERROR;
    }
    FreeDosObject(DOS_FIB, fib);

    eac = AllocDosObject(DOS_EXALLCONTROL, NULL);
    if (eac == NULL) {
        UnLock(lock);
        SetErr(resultOut, outLen, "out of memory");
        return AMIP_AREXX_RC_FAIL;
    }
    eac->eac_LastKey = 0;
    eac->eac_MatchString = NULL;
    eac->eac_MatchFunc = NULL;

    g_fsBuf[0] = '\0';
    do {
        UBYTE exallBuf[AMIP_FS_EXALL_BUF];
        struct ExAllData *ead;

        more = ExAll(lock, (struct ExAllData *)exallBuf, sizeof(exallBuf),
                     ED_COMMENT, eac);
        if (!more && IoErr() != ERROR_NO_MORE_ENTRIES) {
            FreeDosObject(DOS_EXALLCONTROL, eac);
            UnLock(lock);
            SetErr(resultOut, outLen, "directory listing failed");
            return AMIP_AREXX_RC_FAIL;
        }
        if (eac->eac_Entries == 0) {
            continue;
        }
        ead = (struct ExAllData *)exallBuf;
        do {
            AppendEntry(g_fsBuf, sizeof(g_fsBuf), (const char *)ead->ed_Name,
                       ead->ed_Type, ead->ed_Size, ead->ed_Prot,
                       (LONG)ead->ed_Days, (LONG)ead->ed_Mins, (LONG)ead->ed_Ticks,
                       (const char *)ead->ed_Comment);
            ead = ead->ed_Next;
        } while (ead != NULL);
    } while (more);

    FreeDosObject(DOS_EXALLCONTROL, eac);
    UnLock(lock);

    *resultOut = g_fsBuf;
    *outLen = (ULONG)strlen(g_fsBuf);
    return AMIP_AREXX_RC_OK;
}

int AmipFsStat(const char *path, const char **resultOut, ULONG *outLen)
{
    int rc = AMIP_AREXX_RC_OK;
    BPTR lock;
    struct FileInfoBlock *fib;

    lock = ResolveExisting(path, &rc, resultOut, outLen);
    if (lock == 0) {
        return rc;
    }

    fib = AllocDosObject(DOS_FIB, NULL);
    if (fib == NULL || !Examine(lock, fib)) {
        if (fib != NULL) FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
        SetErr(resultOut, outLen, "could not examine path");
        return AMIP_AREXX_RC_FAIL;
    }

    g_fsBuf[0] = '\0';
    AppendEntry(g_fsBuf, sizeof(g_fsBuf), (const char *)fib->fib_FileName,
               fib->fib_DirEntryType, (ULONG)fib->fib_Size,
               (ULONG)fib->fib_Protection, fib->fib_Date.ds_Days,
               fib->fib_Date.ds_Minute, fib->fib_Date.ds_Tick,
               (const char *)fib->fib_Comment);

    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);

    *resultOut = g_fsBuf;
    *outLen = (ULONG)strlen(g_fsBuf);
    return AMIP_AREXX_RC_OK;
}

int AmipFsMkdir(const char *path, const char **resultOut, ULONG *outLen)
{
    const char *leaf;
    int parentLen;
    char parentBuf[256];
    BPTR parentLock;
    BPTR newLock;

    if (!AmipFsEnabled()) {
        SetErr(resultOut, outLen, "file API not enabled -- no FSROOT granted at startup");
        return AMIP_AREXX_RC_ERROR;
    }

    /* The target doesn't exist yet, so it's the PARENT directory
     * that must already exist and be checked for containment.
     * FilePart() (V36) is the documented way to find the leaf
     * component's start; everything before it is the parent. */
    leaf = (const char *)FilePart((CONST_STRPTR)path);
    parentLen = (int)(leaf - path);
    if (parentLen <= 0) {
        /* No parent component at all (a bare relative name with
         * neither ':' nor '/') -- rejected rather than guessing what
         * an empty Lock() path resolves to; this project's own
         * convention is real functions and verified behaviour, not
         * assumed edge cases. */
        SetErr(resultOut, outLen,
               "path must be fully qualified (e.g. RAM:name, not a bare name)");
        return AMIP_AREXX_RC_ERROR;
    }
    if (parentLen >= (int)sizeof(parentBuf)) {
        /* Rejected outright, not silently chopped -- `path` already
         * came through AMIP_AREXX_MAX_PATH (256, arexx_cmd.h), the
         * same size as parentBuf, so this is defense in depth rather
         * than a normally-reachable path; but a truncated parent
         * directory is a DIFFERENT, unintended location, not just a
         * shorter one, so silently acting on it would be a real
         * correctness risk if that invariant ever changed. */
        SetErr(resultOut, outLen, "path too long");
        return AMIP_AREXX_RC_ERROR;
    }
    memcpy(parentBuf, path, (size_t)parentLen);
    parentBuf[parentLen] = '\0';

    parentLock = Lock((CONST_STRPTR)parentBuf, ACCESS_READ);
    if (parentLock == 0) {
        SetErr(resultOut, outLen, "parent directory does not exist");
        return AMIP_AREXX_RC_WARN;
    }
    if (!IsUnderGrantedRoot(parentLock)) {
        UnLock(parentLock);
        DescribeRootsError(resultOut, outLen);
        return AMIP_AREXX_RC_ERROR;
    }
    UnLock(parentLock);

    /* Same TOCTOU gap ResolveExisting()'s doc comment describes: the
     * parent lock verified above is released before CreateDir()
     * re-resolves `path` from scratch (CreateDir() has no
     * relative-to-lock form on this floor). */
    newLock = CreateDir((CONST_STRPTR)path);
    if (newLock == 0) {
        SetErr(resultOut, outLen, "could not create directory (already exists?)");
        return AMIP_AREXX_RC_FAIL;
    }
    UnLock(newLock);

    SetErr(resultOut, outLen, "created");
    return AMIP_AREXX_RC_OK;
}

int AmipFsDelete(const char *path, const char **resultOut, ULONG *outLen)
{
    int rc = AMIP_AREXX_RC_OK;
    BPTR lock;

    lock = ResolveExisting(path, &rc, resultOut, outLen);
    if (lock == 0) {
        return rc;
    }
    /* DeleteFile() takes a path string, not a lock -- and a shared
     * read lock like this one would block deletion on some
     * filesystems anyway, so release it first. This reopens the TOCTOU
     * gap ResolveExisting()'s doc comment describes: the containment
     * check just passed applies to the lock, not to whatever `path`
     * resolves to by the time DeleteFile() below actually runs. */
    UnLock(lock);

    if (!DeleteFile((CONST_STRPTR)path)) {
        SetErr(resultOut, outLen, "delete failed (non-empty directory, or in use)");
        return AMIP_AREXX_RC_FAIL;
    }

    SetErr(resultOut, outLen, "deleted");
    return AMIP_AREXX_RC_OK;
}

int AmipFsGet(const char *path, const char **resultOut, ULONG *outLen)
{
    int rc = AMIP_AREXX_RC_OK;
    BPTR lock;
    struct FileInfoBlock *fib;
    BPTR fh;
    LONG size;
    LONG n;

    lock = ResolveExisting(path, &rc, resultOut, outLen);
    if (lock == 0) {
        return rc;
    }

    fib = AllocDosObject(DOS_FIB, NULL);
    if (fib == NULL || !Examine(lock, fib)) {
        if (fib != NULL) FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
        SetErr(resultOut, outLen, "could not examine path");
        return AMIP_AREXX_RC_FAIL;
    }
    if (fib->fib_DirEntryType >= 0) {
        FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
        SetErr(resultOut, outLen, "is a directory (FSGET reads files only)");
        return AMIP_AREXX_RC_ERROR;
    }
    size = fib->fib_Size;
    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);

    if (size < 0 || (size_t)size > sizeof(g_fsBuf)) {
        char msg[96];
        snprintf(msg, sizeof(msg), "file too large for FSGET (%ld bytes, max %d)",
                 (long)size, (int)sizeof(g_fsBuf));
        SetErr(resultOut, outLen, msg);
        return AMIP_AREXX_RC_FAIL;
    }

    /* Same TOCTOU gap ResolveExisting()'s doc comment describes: the
     * lock above is already released (Examine() needed it, Open()
     * below doesn't accept one), so this Open() re-resolves `path`
     * from scratch rather than reusing anything already verified as
     * contained. */
    fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (fh == 0) {
        SetErr(resultOut, outLen, "could not open file");
        return AMIP_AREXX_RC_FAIL;
    }
    n = size > 0 ? Read(fh, g_fsBuf, size) : 0;
    Close(fh);
    if (n != size) {
        SetErr(resultOut, outLen, "short read");
        return AMIP_AREXX_RC_FAIL;
    }

    *resultOut = g_fsBuf;
    *outLen = (ULONG)size;
    return AMIP_AREXX_RC_OK;
}

int AmipFsPut(const char *path, const void *data, ULONG len,
              const char **resultOut, ULONG *outLen)
{
    const char *leaf;
    int parentLen;
    char parentBuf[256];
    BPTR parentLock;
    BPTR fh;
    LONG n;

    if (!AmipFsEnabled()) {
        SetErr(resultOut, outLen, "file API not enabled -- no FSROOT granted at startup");
        return AMIP_AREXX_RC_ERROR;
    }

    /* Same containment shape as AmipFsMkdir(): the target may not
     * exist yet (a fresh file) or may already (an overwrite -- Open()
     * with MODE_NEWFILE handles both identically), so it's the PARENT
     * directory that must already exist and be checked, not the
     * target itself. */
    leaf = (const char *)FilePart((CONST_STRPTR)path);
    parentLen = (int)(leaf - path);
    if (parentLen <= 0) {
        SetErr(resultOut, outLen,
               "path must be fully qualified (e.g. RAM:name, not a bare name)");
        return AMIP_AREXX_RC_ERROR;
    }
    if (parentLen >= (int)sizeof(parentBuf)) {
        SetErr(resultOut, outLen, "path too long");
        return AMIP_AREXX_RC_ERROR;
    }
    memcpy(parentBuf, path, (size_t)parentLen);
    parentBuf[parentLen] = '\0';

    parentLock = Lock((CONST_STRPTR)parentBuf, ACCESS_READ);
    if (parentLock == 0) {
        SetErr(resultOut, outLen, "parent directory does not exist");
        return AMIP_AREXX_RC_WARN;
    }
    if (!IsUnderGrantedRoot(parentLock)) {
        UnLock(parentLock);
        DescribeRootsError(resultOut, outLen);
        return AMIP_AREXX_RC_ERROR;
    }
    UnLock(parentLock);

    /* Same TOCTOU gap every other verb here already documents: the
     * parent lock verified above is released before Open() re-resolves
     * `path` from scratch. */
    fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    if (fh == 0) {
        SetErr(resultOut, outLen, "could not create file");
        return AMIP_AREXX_RC_FAIL;
    }
    n = len > 0 ? Write(fh, (APTR)data, (LONG)len) : 0;
    Close(fh);
    if (n != (LONG)len) {
        SetErr(resultOut, outLen, "short write (disk full?)");
        return AMIP_AREXX_RC_FAIL;
    }

    SetErr(resultOut, outLen, "written");
    return AMIP_AREXX_RC_OK;
}
