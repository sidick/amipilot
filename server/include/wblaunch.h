/* wblaunch.h -- real Workbench-style launch (phase 1.0).
 *
 * docs/implementation-plan.md's "Program launch" section calls this out
 * explicitly: "read the tool's icon via icon.library, merge per-launch
 * tooltype overrides over the icon's array in memory, and construct a
 * genuine WBStartup message -- argument list with locks and names,
 * including project-file arguments... The launched program experiences
 * a real Workbench start, which matters precisely because tooltype
 * parsing and WBStartup handling are code paths its tests should
 * exercise." LAUNCH (server/src/amipilotserver/main.c) already covers
 * Shell-style starts (SystemTagList()); this module is the other half.
 *
 * Genuinely hand-rolled, not a V44+ shortcut: workbench.library's
 * OpenWorkbenchObjectA() looks tempting but is V44-only and its own
 * autodoc BUGS section says launching (not just opening drawers) was
 * unsafe and could trash memory up to and including V45.38 -- both
 * disqualifying against this project's V37 floor (CLAUDE.md's "Minimum
 * requirements"). This instead builds the WBStartup/WBArg messages by
 * hand with V36 dos.library/icon.library primitives only, the same
 * technique real launcher utilities (AmigaOS 45's own WBLoad; the
 * classic "WBRun") use -- confirmed against the amigados-rkrm skill's
 * own processes.md, which documents exactly this: "Such a message can
 * be created manually and sent to the process once started."
 *
 * One real deviation from this feature's own original plan sketch,
 * found doing this research: the plan assumed tooltype overrides could
 * be merged "in memory... no disk writes." That turns out not to be
 * achievable against a REAL, unmodified target app: a Workbench-started
 * program discovers its own tooltypes by calling icon.library's
 * GetDiskObject() on ITS OWN icon file, using the very
 * WBArg[0].wa_Lock/wa_Name this module hands it -- there is no in-
 * memory channel to hand tooltypes to an off-the-shelf binary at all.
 * The real, documented mechanism every "tooltype override" launcher
 * actually uses is: read the real icon (GetDiskObject), build a merged
 * do_ToolTypes array in memory, PutDiskObject() it to a SCRATCH path
 * (never the original -- the real .info on disk is never touched), and
 * point WBArg[0] at that scratch location instead of the tool's real
 * one. The launched program's own self-lookup then transparently reads
 * the merged set. This is a real disk write (to T:, cleaned up once
 * the launch's WBStartup reply arrives), just never to the app's own
 * real icon.
 */
#ifndef AMIPILOT_WBLAUNCH_H
#define AMIPILOT_WBLAUNCH_H

#include <exec/types.h>

/* Opens icon.library and this module's own reply MsgPort (used to
 * learn when a launched process has exited so its seglist/locks/
 * scratch icon can be freed -- see AmipWbPoll()). Call once at server
 * startup, before any AmipWbLaunch() call. */
void AmipWbInit(void);

/* The signal mask the caller folds into its Wait()/AmipTcpWait() --
 * fires when a launched process replies its WBStartup message (i.e.
 * it has exited and released the message). 0 if AmipWbInit() failed
 * to create the port (out of memory) -- WBLAUNCH then always fails
 * cleanly with AMIP_AREXX_RC_FAIL rather than leaking. */
ULONG AmipWbSigMask(void);

/* Reaps every pending launch that has replied since the last call --
 * frees its seglist (UnLoadSeg), WBArg locks (UnLock), the WBStartup/
 * WBArg allocations, and its scratch icon file, if one was written for
 * TOOLTYPE= overrides. Call whenever AmipWbSigMask()'s bit is set in
 * the signals a Wait()/AmipTcpWait() call returned. Safe to call any
 * time (e.g. speculatively); does nothing if no reply is waiting. */
void AmipWbPoll(void);

/* Closes icon.library and this module's reply port. Any launch still
 * pending at this point (the child hasn't exited yet) has its seglist/
 * locks/scratch icon leaked -- there is no safe way to force it closed
 * (this project's own documented "no kill verb" stance, docs/
 * implementation-plan.md's "Teardown without a kill verb, by design"),
 * and the server itself is going away, so nothing could free them
 * anyway. Rare in practice (AmiPilotServer only reaches shutdown
 * between test runs, by which point launched subjects have long since
 * been asked to quit) and no worse than the equivalent gap LAUNCH's
 * own async SystemTagList() launches already have. */
void AmipWbShutdown(void);

#define AMIP_WB_MAX_TOOLTYPES 8  /* WBLAUNCH's repeatable TOOLTYPE= cap */
#define AMIP_WB_MAX_ARGS      8  /* WBLAUNCH's repeatable ARG= cap */
#define AMIP_WB_TT_KEY_LEN    32
#define AMIP_WB_TT_VALUE_LEN  192
#define AMIP_WB_ARG_PATH_LEN  256

/* Launches `iconPath` (a tool or project icon's path, WITHOUT the
 * ".info" suffix -- icon.library's own convention) as if its icon had
 * been double-clicked: reads the real icon via GetDiskObject(),
 * resolves the actual binary to run (`iconPath` itself for a WBTOOL
 * icon; the project's own do_DefaultTool for a WBPROJECT icon, with
 * the project itself becoming a second WBArg), LoadSeg()s it, and
 * CreateNewProc()s a genuine non-CLI ("Workbench-style") process, then
 * hand-builds and PutMsg()s a real WBStartup message to it -- see this
 * header's own top comment for the full mechanism and its one real
 * deviation from the original plan sketch.
 *
 * `toolTypeKeys`/`toolTypeValues` (parallel arrays, `numToolTypes`
 * entries, each capped at AMIP_WB_MAX_TOOLTYPES) are merged over the
 * icon's own do_ToolTypes -- a matching existing KEY (case-
 * insensitive) is overridden in place, anything else new is appended,
 * everything not named is left alone. If `numToolTypes` is 0 the
 * REAL icon and REAL tool location are used directly, with no scratch
 * write at all.
 *
 * `argPaths` (an array of `numArgPaths` fully-qualified paths, capped
 * at AMIP_WB_MAX_ARGS) become additional WBArg entries after the
 * primary tool/project argument(s) -- the "multiple project files"
 * case docs/implementation-plan.md and the RKRM's own Workbench
 * chapter both describe.
 *
 * Asynchronous, like LAUNCH: returns AMIP_AREXX_RC_OK as soon as the
 * process is created and the startup message is queued to it -- NOT
 * proof the launched program actually started running or found its
 * tooltypes/arguments meaningful, same honest caveat LAUNCH's own doc
 * comment (amipilotserver/main.c) already carries. `*resultOut`
 * (cap `*outLen` follows the usual HandleCommand() convention) gets a
 * one-line reason on any non-OK return; AMIP_AREXX_RC_ERROR for a bad
 * icon/path/type/too-many-pending, AMIP_AREXX_RC_FAIL for
 * CreateNewProc() itself failing (out of memory, no process slot). */
int AmipWbLaunch(const char *iconPath,
                  const char toolTypeKeys[][AMIP_WB_TT_KEY_LEN],
                  const char toolTypeValues[][AMIP_WB_TT_VALUE_LEN],
                  int numToolTypes,
                  const char argPaths[][AMIP_WB_ARG_PATH_LEN],
                  int numArgPaths,
                  const char **resultOut, ULONG *outLen);

#endif /* AMIPILOT_WBLAUNCH_H */
