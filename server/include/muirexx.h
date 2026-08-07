/* muirexx.h -- the MUI-ARexx bridge tier (phase 0.5, docs/
 * implementation-plan.md's "Locator tiers": "MUI internals are
 * deliberately opaque to external walkers, but every MUI app carries
 * an automatic ARexx port -- so the MUI tier drives through that port
 * (standard commands plus app-defined ones)").
 *
 * This is a genuinely different mechanism from tiers 1-2 (no
 * structural walk, no input.device synthesis): it sends an ARexx
 * command STRING to another application's own public ARexx port and
 * reports back whatever that app's own command handler replied with.
 * AmiPilot doesn't -- can't -- invent a generic "get/set this widget's
 * value" command: confirmed live against AmigaOS 3.2's own MUI-Demo
 * (MUI:Demos/MUI-Demo) that MUI's BUILT-IN ARexx support is just seven
 * universal commands (QUIT/HIDE/SHOW/ACTIVATE/DEACTIVATE/INFO/HELP,
 * all window-lifecycle or fixed-metadata, no generic attribute access)
 * -- anything richer than that is entirely up to the target
 * application registering its own commands
 * (MUIA_Application_Commands), which this module passes through
 * verbatim rather than second-guesses. "MUI coverage depends on what
 * each app's port exposes" (the plan's own honest framing) is a
 * property of MUI itself, not a gap in this bridge.
 */
#ifndef AMIPILOT_MUIREXX_H
#define AMIPILOT_MUIREXX_H

#include <stddef.h>

typedef enum {
    AMIP_MUIREXX_OK = 0,       /* sent, replied, the app's own RC was 0 --
                                * appRC/result filled in */
    AMIP_MUIREXX_APP_ERROR,    /* sent, replied, the app's own RC was
                                * nonzero -- appRC/result filled in;
                                * this is the TARGET app's own failure
                                * report, not a transport problem */
    AMIP_MUIREXX_NOT_FOUND,    /* no ARexx port found under `base`
                                * (tried bare, then "<base>.1" -- see
                                * AmipMuiRexxSend's own doc comment) */
    AMIP_MUIREXX_TIMEOUT,      /* sent, but no reply within
                                * timeoutSeconds */
    AMIP_MUIREXX_ALLOC_FAIL    /* CreateMsgPort()/CreateRexxMsg()/
                                * FillRexxMsg() failed (out of memory) --
                                * distinct from every outcome above,
                                * none of which are this program's own
                                * resource problem */
} AmipMuiRexxResult;

/* Sends `command` to the public ARexx port for application `base`,
 * polling (Delay()-based, same ~100ms granularity WAITFOR's own
 * polling uses -- not a signal wait, so this can be interrupted by
 * nothing and simply times out on schedule) for up to `timeoutSeconds`
 * (0 = a 10s default, matching WAITFOR's own).
 *
 * Port name resolution tries `base` verbatim first, then
 * "<base>.1" -- both are real, observed conventions (MUI's own dev
 * docs document the "<base>.N" slot convention explicitly, the same
 * one this server's own AmiPilotServer port uses; a shipped MUI
 * example macro (WbMan.mrx) addresses its target by the bare base
 * name with no suffix at all, and this module doesn't assume which
 * one any given app chose over the other).
 *
 * On AMIP_MUIREXX_OK or AMIP_MUIREXX_APP_ERROR, *appRC is the
 * target's own RexxMsg rm_Result1 (an arbitrary, app-defined code --
 * this bridge does not reinterpret it, only relays it) and result is
 * filled with its rm_Result2 argstring if the app set one (empty
 * string, not left untouched, if it didn't -- always NUL-terminated).
 * On every other outcome, *appRC and result are untouched. */
AmipMuiRexxResult AmipMuiRexxSend(const char *base, const char *command,
                                   long timeoutSeconds, long *appRC,
                                   char *result, size_t resultCap);

#endif /* AMIPILOT_MUIREXX_H */
