/* wblaunch.c -- see wblaunch.h. */
#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <exec/libraries.h>

#include <dos/dos.h>
#include <dos/dostags.h>

#include <workbench/workbench.h>
#include <workbench/startup.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>

#include "arexx_cmd.h"
#include "wblaunch.h"

struct Library *IconBase = NULL;

#define AMIP_WB_MAX_PENDING 8
#define AMIP_WB_MAX_TOTAL_ARGS (2 + AMIP_WB_MAX_ARGS) /* tool + project + ARG= */
#define AMIP_WB_PATH_BUF 256

#define AMIP_WB_NAME_BUF 64

typedef struct {
    BOOL inUse;
    BPTR seglist;
    struct WBArg *argList;
    LONG numArgs;
    struct WBStartup *startup;
    char scratchName[32]; /* "T:amipilot-wb-<n>", no ".info"; empty = none written */
    /* Owned copies of every WBArg's wa_Name -- the leaf names FillArg()
     * computes come from paths that live in a caller's stack frame
     * (arexx_cmd's parsed fields, or this function's own local
     * `toolPath`/`iconPath`), all long gone by the time the LAUNCHED
     * PROCESS actually reads wa_Name, arbitrarily later. Every wa_Name
     * this module hands out points in here, never into transient
     * storage. */
    char argNames[AMIP_WB_MAX_TOTAL_ARGS][AMIP_WB_NAME_BUF];
} PendingLaunch;

static struct MsgPort *g_replyPort = NULL;
static PendingLaunch g_pending[AMIP_WB_MAX_PENDING];
static int g_scratchCounter = 0;
static char g_resultBuf[160];

static void SetErr(const char **resultOut, ULONG *outLen, const char *msg)
{
    strncpy(g_resultBuf, msg, sizeof(g_resultBuf) - 1);
    g_resultBuf[sizeof(g_resultBuf) - 1] = '\0';
    *resultOut = g_resultBuf;
    *outLen = (ULONG)strlen(g_resultBuf);
}

void AmipWbInit(void)
{
    IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 0);
    g_replyPort = CreateMsgPort();
    memset(g_pending, 0, sizeof(g_pending));
}

ULONG AmipWbSigMask(void)
{
    return g_replyPort != NULL ? (1UL << g_replyPort->mp_SigBit) : 0;
}

/* Releases everything a launch's cleanup owns once its WBStartup
 * reply has arrived -- the seglist, every WBArg lock, the WBStartup/
 * WBArg allocations, and its scratch icon (if TOOLTYPE= wrote one). */
static void FreePending(PendingLaunch *p)
{
    LONG i;

    for (i = 0; i < p->numArgs; i++) {
        if (p->argList[i].wa_Lock != 0) {
            UnLock(p->argList[i].wa_Lock);
        }
    }
    if (p->argList != NULL) {
        FreeVec(p->argList);
    }
    if (p->startup != NULL) {
        FreeVec(p->startup);
    }
    if (p->seglist != 0) {
        UnLoadSeg(p->seglist);
    }
    if (p->scratchName[0] != '\0') {
        char infoPath[sizeof(p->scratchName) + 8];

        snprintf(infoPath, sizeof(infoPath), "%s.info", p->scratchName);
        DeleteFile((CONST_STRPTR)infoPath);
    }
    memset(p, 0, sizeof(*p));
}

void AmipWbPoll(void)
{
    struct WBStartup *msg;
    int i;

    if (g_replyPort == NULL) {
        return;
    }
    while ((msg = (struct WBStartup *)GetMsg(g_replyPort)) != NULL) {
        for (i = 0; i < AMIP_WB_MAX_PENDING; i++) {
            if (g_pending[i].inUse && g_pending[i].startup == msg) {
                FreePending(&g_pending[i]);
                break;
            }
        }
    }
}

void AmipWbShutdown(void)
{
    /* Any launch still inUse here has its resources leaked -- see
     * this function's own doc comment in wblaunch.h for why that's an
     * accepted, documented gap rather than something worth a forced
     * teardown. */
    if (g_replyPort != NULL) {
        DeleteMsgPort(g_replyPort);
        g_replyPort = NULL;
    }
    if (IconBase != NULL) {
        CloseLibrary(IconBase);
        IconBase = NULL;
    }
}

/* Splits `path` into a NUL-terminated directory prefix (into `dirBuf`,
 * cap `dirCap`) and a leaf pointer into `path` itself -- same idiom
 * fs.c's AmipFsPut() already uses for the identical need. Returns
 * FALSE if `path` has no directory component (a bare, unqualified
 * name) or doesn't fit `dirBuf`. */
static BOOL SplitDirLeaf(const char *path, char *dirBuf, int dirCap, const char **leafOut)
{
    const char *leaf = (const char *)FilePart((CONST_STRPTR)path);
    int dirLen = (int)(leaf - path);

    if (dirLen <= 0 || dirLen >= dirCap) {
        return FALSE;
    }
    memcpy(dirBuf, path, (size_t)dirLen);
    dirBuf[dirLen] = '\0';
    *leafOut = leaf;
    return TRUE;
}

/* Locks `path`'s directory and fills in one WBArg entry for it, with
 * wa_Name copied into `nameBuf` (cap AMIP_WB_NAME_BUF) rather than
 * left pointing into `path` -- `path` is always caller/stack-transient
 * storage that won't outlive the launched process actually reading
 * wa_Name (see PendingLaunch's own argNames comment). Returns FALSE
 * (arg->wa_Lock left 0) on a bad path, a leaf name too long for
 * nameBuf, or a failed Lock() -- the caller is responsible for
 * unwinding any earlier entries already filled in. */
static BOOL FillArg(struct WBArg *arg, const char *path, char *nameBuf)
{
    char dirBuf[AMIP_WB_PATH_BUF];
    const char *leaf;

    arg->wa_Lock = 0;
    arg->wa_Name = NULL;
    if (!SplitDirLeaf(path, dirBuf, sizeof(dirBuf), &leaf)) {
        return FALSE;
    }
    if (strlen(leaf) >= AMIP_WB_NAME_BUF) {
        return FALSE;
    }
    arg->wa_Lock = Lock((CONST_STRPTR)dirBuf, SHARED_LOCK);
    if (arg->wa_Lock == 0) {
        return FALSE;
    }
    strcpy(nameBuf, leaf);
    arg->wa_Name = (STRPTR)nameBuf;
    return TRUE;
}

static void UnwindArgs(struct WBArg *args, LONG count)
{
    LONG i;

    for (i = 0; i < count; i++) {
        if (args[i].wa_Lock != 0) {
            UnLock(args[i].wa_Lock);
        }
    }
}

/* Deletes slot's scratch icon (the TOOLTYPE= merge's T: write, if any
 * was made -- scratchName is empty otherwise) and clears the field so
 * a later successful launch reusing this slot doesn't see a stale
 * name. A real bug found in code review: every failure path between
 * writing the scratch icon (numToolTypes > 0 branch, above) and
 * setting inUse = TRUE at the very end of AmipWbLaunch() used to
 * return without calling this at all -- FreePending() (the only
 * other place that deletes it) never runs for a slot that never
 * became inUse, so the .info file was orphaned on T: permanently.
 * Safe to call even when scratchName is already empty (a no-op). */
static void CleanupScratchIcon(PendingLaunch *p)
{
    if (p->scratchName[0] != '\0') {
        char infoPath[sizeof(p->scratchName) + 8];

        snprintf(infoPath, sizeof(infoPath), "%s.info", p->scratchName);
        DeleteFile((CONST_STRPTR)infoPath);
        p->scratchName[0] = '\0';
    }
}

/* Case-insensitive "does this do_ToolTypes entry's KEY match `key`"
 * (up to the first '=', or the whole string for a valueless tooltype
 * like "NOICONIFY") -- FindToolType()'s own matching convention. A
 * hand-rolled compare rather than utility.library's Strnicmp(): that
 * would need UtilityBase opened for one prefix compare, not worth the
 * extra library dependency. */
static BOOL ToolTypeKeyMatches(const char *entry, const char *key)
{
    size_t i;

    for (i = 0; key[i] != '\0'; i++) {
        char a = entry[i];
        char b = key[i];

        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) {
            return FALSE;
        }
    }
    return entry[i] == '=' || entry[i] == '\0';
}

int AmipWbLaunch(const char *iconPath,
                  const char toolTypeKeys[][AMIP_WB_TT_KEY_LEN],
                  const char toolTypeValues[][AMIP_WB_TT_VALUE_LEN],
                  int numToolTypes,
                  const char argPaths[][AMIP_WB_ARG_PATH_LEN],
                  int numArgPaths,
                  const char **resultOut, ULONG *outLen)
{
    struct DiskObject *dobj;
    char toolPath[AMIP_WB_PATH_BUF];
    char overrideStrs[AMIP_WB_MAX_TOOLTYPES][AMIP_WB_TT_KEY_LEN + AMIP_WB_TT_VALUE_LEN + 2];
    LONG stackSize;
    struct WBArg args[AMIP_WB_MAX_TOTAL_ARGS];
    LONG nArgs = 0;
    BPTR seglist;
    struct WBStartup *startup;
    struct Process *proc;
    struct TagItem tags[7];
    int nTags = 0;
    int slot;
    int i;

    if (IconBase == NULL) {
        SetErr(resultOut, outLen, "icon.library unavailable");
        return AMIP_AREXX_RC_ERROR;
    }
    if (numToolTypes > AMIP_WB_MAX_TOOLTYPES || numArgPaths > AMIP_WB_MAX_ARGS) {
        SetErr(resultOut, outLen, "too many TOOLTYPE=/ARG= entries");
        return AMIP_AREXX_RC_ERROR;
    }

    for (slot = 0; slot < AMIP_WB_MAX_PENDING; slot++) {
        if (!g_pending[slot].inUse) {
            break;
        }
    }
    if (slot == AMIP_WB_MAX_PENDING) {
        SetErr(resultOut, outLen, "too many WBLAUNCH processes still running");
        return AMIP_AREXX_RC_ERROR;
    }

    dobj = GetDiskObject((CONST_STRPTR)iconPath);
    if (dobj == NULL) {
        SetErr(resultOut, outLen, "icon not found (expects <path>, reads <path>.info)");
        return AMIP_AREXX_RC_ERROR;
    }

    if (dobj->do_Type == WBTOOL) {
        strncpy(toolPath, iconPath, sizeof(toolPath) - 1);
        toolPath[sizeof(toolPath) - 1] = '\0';
    } else if (dobj->do_Type == WBPROJECT) {
        if (dobj->do_DefaultTool == NULL || dobj->do_DefaultTool[0] == '\0') {
            FreeDiskObject(dobj);
            SetErr(resultOut, outLen, "project icon has no default tool");
            return AMIP_AREXX_RC_ERROR;
        }
        strncpy(toolPath, (const char *)dobj->do_DefaultTool, sizeof(toolPath) - 1);
        toolPath[sizeof(toolPath) - 1] = '\0';
    } else {
        FreeDiskObject(dobj);
        SetErr(resultOut, outLen, "not a launchable icon (drawer/disk/kick)");
        return AMIP_AREXX_RC_ERROR;
    }
    stackSize = dobj->do_StackSize;

    /* Primary WBArg(s): the tool itself always comes first (what a
     * launched program's own self-lookup, CurrentDir(wa_Lock) +
     * GetDiskObject(wa_Name), reads its tooltypes from -- see
     * wblaunch.h's top comment); a WBPROJECT icon adds the project
     * file itself as a second entry, matching the RKRM's own "Two
     * Arguments" case. */
    if (!FillArg(&args[nArgs], toolPath, g_pending[slot].argNames[nArgs])) {
        FreeDiskObject(dobj);
        SetErr(resultOut, outLen, "could not lock tool's directory (fully-qualified path required)");
        return AMIP_AREXX_RC_ERROR;
    }
    nArgs++;
    if (dobj->do_Type == WBPROJECT) {
        if (!FillArg(&args[nArgs], iconPath, g_pending[slot].argNames[nArgs])) {
            UnwindArgs(args, nArgs);
            FreeDiskObject(dobj);
            SetErr(resultOut, outLen, "could not lock project's directory");
            return AMIP_AREXX_RC_ERROR;
        }
        nArgs++;
    }

    /* TOOLTYPE= overrides: merge into a scratch copy of the icon
     * written to T: (never the original -- see wblaunch.h's top
     * comment for why this, not an in-memory-only channel, is the
     * real mechanism) and repoint the tool's own WBArg entry at it. */
    if (numToolTypes > 0) {
        STRPTR *oldTT = dobj->do_ToolTypes;
        int origTotal = 0;
        STRPTR *merged;
        int mergedCount = 0;
        BOOL matched;
        char scratchName[32];

        /* Sized to hold every original entry PLUS every override,
         * never capped below that -- a fixed-size stack array here
         * would silently drop real tooltypes on an icon with more
         * entries than expected, exactly the "silent truncation"
         * this project's own conventions forbid. */
        if (oldTT != NULL) {
            while (oldTT[origTotal] != NULL) {
                origTotal++;
            }
        }
        merged = AllocVec(sizeof(STRPTR) * (ULONG)(origTotal + numToolTypes + 1), MEMF_PUBLIC);
        if (merged == NULL) {
            UnwindArgs(args, nArgs);
            FreeDiskObject(dobj);
            SetErr(resultOut, outLen, "out of memory merging tooltypes");
            return AMIP_AREXX_RC_ERROR;
        }

        for (i = 0; i < origTotal; i++) {
            int j;

            matched = FALSE;
            for (j = 0; j < numToolTypes; j++) {
                if (ToolTypeKeyMatches((const char *)oldTT[i], toolTypeKeys[j])) {
                    matched = TRUE;
                    break;
                }
            }
            if (!matched) {
                merged[mergedCount++] = oldTT[i];
            }
        }
        for (i = 0; i < numToolTypes; i++) {
            snprintf(overrideStrs[i], sizeof(overrideStrs[i]), "%s=%s",
                     toolTypeKeys[i], toolTypeValues[i]);
            merged[mergedCount++] = (STRPTR)overrideStrs[i];
        }
        merged[mergedCount] = NULL;

        snprintf(scratchName, sizeof(scratchName), "T:amipilot-wb-%d", g_scratchCounter++);
        dobj->do_ToolTypes = merged;
        if (!PutDiskObject((CONST_STRPTR)scratchName, dobj)) {
            dobj->do_ToolTypes = oldTT;
            FreeVec(merged);
            UnwindArgs(args, nArgs);
            FreeDiskObject(dobj);
            SetErr(resultOut, outLen, "could not write scratch icon to T: for TOOLTYPE= merge");
            return AMIP_AREXX_RC_ERROR;
        }
        dobj->do_ToolTypes = oldTT;
        FreeVec(merged);

        /* Repoint the tool's own arg (always index 0) at the scratch
         * icon's location so the launched program's self-lookup finds
         * the merged tooltypes instead of the real ones. */
        UnLock(args[0].wa_Lock);
        args[0].wa_Lock = Lock((CONST_STRPTR)"T:", SHARED_LOCK);
        if (args[0].wa_Lock == 0) {
            char scratchInfo[40];

            UnwindArgs(&args[1], nArgs - 1);
            snprintf(scratchInfo, sizeof(scratchInfo), "%s.info", scratchName);
            DeleteFile((CONST_STRPTR)scratchInfo);
            FreeDiskObject(dobj);
            SetErr(resultOut, outLen, "could not lock T: for the scratch icon");
            return AMIP_AREXX_RC_ERROR;
        }
        strncpy(g_pending[slot].scratchName, scratchName, sizeof(g_pending[slot].scratchName) - 1);
        g_pending[slot].scratchName[sizeof(g_pending[slot].scratchName) - 1] = '\0';
        /* wa_Name must survive past this call's own stack frame, same
         * requirement FillArg()'s own nameBuf serves -- copy the leaf
         * (scratchName without its "T:" prefix) into this slot's own
         * persistent argNames[0] rather than pointing at scratchName,
         * which is a local about to go out of scope. */
        strcpy(g_pending[slot].argNames[0], scratchName + 2); /* skip "T:" */
        args[0].wa_Name = (STRPTR)g_pending[slot].argNames[0];
    }

    FreeDiskObject(dobj); /* everything needed has been copied out already */

    /* ARG= project-file arguments, appended after the primary arg(s). */
    for (i = 0; i < numArgPaths; i++) {
        if (!FillArg(&args[nArgs], argPaths[i], g_pending[slot].argNames[nArgs])) {
            UnwindArgs(args, nArgs);
            CleanupScratchIcon(&g_pending[slot]);
            SetErr(resultOut, outLen, "could not lock an ARG= path (fully-qualified path required)");
            return AMIP_AREXX_RC_ERROR;
        }
        nArgs++;
    }

    seglist = LoadSeg((CONST_STRPTR)toolPath);
    if (seglist == 0) {
        UnwindArgs(args, nArgs);
        CleanupScratchIcon(&g_pending[slot]);
        SetErr(resultOut, outLen, "could not load the tool's executable");
        return AMIP_AREXX_RC_ERROR;
    }

    startup = AllocVec(sizeof(*startup), MEMF_PUBLIC | MEMF_CLEAR);
    g_pending[slot].argList = AllocVec(sizeof(struct WBArg) * (ULONG)nArgs, MEMF_PUBLIC);
    if (startup == NULL || g_pending[slot].argList == NULL) {
        if (startup != NULL) {
            FreeVec(startup);
        }
        if (g_pending[slot].argList != NULL) {
            FreeVec(g_pending[slot].argList);
            g_pending[slot].argList = NULL;
        }
        UnLoadSeg(seglist);
        UnwindArgs(args, nArgs);
        CleanupScratchIcon(&g_pending[slot]);
        SetErr(resultOut, outLen, "out of memory building the startup message");
        return AMIP_AREXX_RC_ERROR;
    }
    memcpy(g_pending[slot].argList, args, sizeof(struct WBArg) * (size_t)nArgs);

    startup->sm_Message.mn_ReplyPort = g_replyPort;
    startup->sm_Message.mn_Length = sizeof(*startup);
    startup->sm_NumArgs = nArgs;
    startup->sm_ToolWindow = NULL;
    startup->sm_ArgList = g_pending[slot].argList;

    tags[nTags].ti_Tag = NP_Seglist;     tags[nTags++].ti_Data = (ULONG)seglist;
    tags[nTags].ti_Tag = NP_FreeSeglist; tags[nTags++].ti_Data = FALSE; /* we own it, see FreePending() */
    tags[nTags].ti_Tag = NP_Name;        tags[nTags++].ti_Data = (ULONG)args[0].wa_Name;
    if (stackSize > 0) {
        tags[nTags].ti_Tag = NP_StackSize; tags[nTags++].ti_Data = (ULONG)stackSize;
    }
    tags[nTags].ti_Tag = TAG_DONE; tags[nTags++].ti_Data = 0;

    /* NP_Cli is left at its default (FALSE): a non-CLI, "Workbench-
     * style" process (pr_CLI stays NULL), which is exactly what makes
     * the launched program's own standard startup code wait for a
     * WBStartup message on pr_MsgPort instead of parsing a command
     * line -- see wblaunch.h's top comment. NP_Input/NP_Output are
     * also left default (dos.library opens NIL: for both itself). */
    proc = CreateNewProc(tags);
    if (proc == NULL) {
        FreeVec(startup);
        FreeVec(g_pending[slot].argList);
        g_pending[slot].argList = NULL;
        UnLoadSeg(seglist);
        UnwindArgs(args, nArgs);
        CleanupScratchIcon(&g_pending[slot]);
        SetErr(resultOut, outLen, "could not create process (out of memory or no process slot)");
        return AMIP_AREXX_RC_FAIL;
    }

    startup->sm_Process = &proc->pr_MsgPort;
    PutMsg(&proc->pr_MsgPort, &startup->sm_Message);

    g_pending[slot].inUse = TRUE;
    g_pending[slot].seglist = seglist;
    g_pending[slot].numArgs = nArgs;
    g_pending[slot].startup = startup;

    SetErr(resultOut, outLen, "launched");
    return AMIP_AREXX_RC_OK;
}
