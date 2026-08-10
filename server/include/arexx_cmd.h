/* arexx_cmd.h -- portable ARexx command-line parsing + RC policy for the
 * server commodity's ARexx port (phase 0.2). Pure C, no Amiga types --
 * same split as ../amiauth's arexx_cmd.h/arexx.h: this file is the part
 * that could be host-tested; the Amiga-only RexxMsg glue lives in
 * arexx.h/arexx.c, and the actual action-engine/intuition-model work
 * each command does lives in the commodity's own main.c.
 *
 * Verb set is deliberately a small, real subset of the implementation
 * plan's full v1 list (docs/implementation-plan.md) -- TREE/CLICK/TYPE/
 * GETTEXT/QUIT cover phase 0.2's release gate ("an ARexx script clicks
 * a button on the test app and asserts [state] changed") without
 * reaching into 0.3/0.4 scope (wire protocol, launch, fs, menus, drag).
 */
#ifndef AMIPILOT_AREXX_CMD_H
#define AMIPILOT_AREXX_CMD_H

typedef enum {
    AMIP_AREXX_CMD_UNKNOWN = 0,
    AMIP_AREXX_CMD_TREE,     /* TREE <window-pattern> */
    AMIP_AREXX_CMD_CLICK,    /* CLICK <window-pattern> <gadget-id> [EXPECT=...] [TIMEOUT=<n>] |
                              * CLICK <window-pattern> ROLE=<r> [LABEL=<l>] [INDEX=<n>] [EXPECT=...] [TIMEOUT=<n>] |
                              * CLICK @<name> [EXPECT=...] [TIMEOUT=<n>]
                              * -- EXPECT= is "WINDOW=<pattern>" or bare
                              * "NOWINDOW"; see AmipArexxParse's doc
                              * comment. */
    AMIP_AREXX_CMD_TYPE,     /* TYPE <window-pattern> <gadget-id> <text...> |
                              * TYPE <window-pattern> ROLE=<r> [LABEL=<l>] [INDEX=<n>] <text...> |
                              * TYPE @<name> <text...> */
    AMIP_AREXX_CMD_GETTEXT,  /* GETTEXT <window-pattern> <gadget-id> |
                              * GETTEXT <window-pattern> ROLE=<r> [LABEL=<l>] [INDEX=<n>] |
                              * GETTEXT @<name> */
    AMIP_AREXX_CMD_MANIFEST, /* MANIFEST <file-path> */
    AMIP_AREXX_CMD_VERSION,  /* VERSION -- the wire handshake (server/WIRE.md),
                              * also answerable over ARexx for feature tests */
    AMIP_AREXX_CMD_LAUNCH,   /* LAUNCH [STACK=n] <command-line...> */
    AMIP_AREXX_CMD_WBLAUNCH, /* WBLAUNCH <icon-path> [TOOLTYPE=<key>=<value> ...] [ARG=<path> ...] --
                              * real Workbench-style launch (phase 1.0);
                              * see server/include/wblaunch.h */
    AMIP_AREXX_CMD_FSLIST,   /* FSLIST <path> */
    AMIP_AREXX_CMD_FSSTAT,   /* FSSTAT <path> */
    AMIP_AREXX_CMD_FSMKDIR,  /* FSMKDIR <path> */
    AMIP_AREXX_CMD_FSDELETE, /* FSDELETE <path> */
    AMIP_AREXX_CMD_FSGET,    /* FSGET <path> */
    AMIP_AREXX_CMD_FSPUT,    /* FSPUT <path> <byte-count> [TIMEOUT=<n>] --
                              * wire-only (serial/TCP), NOT answerable over
                              * ARexx -- see AmipArexxParse's own doc
                              * comment and server/WIRE.md's request-payload
                              * framing for why. */
    AMIP_AREXX_CMD_MENU,     /* MENU <window-pattern> */
    AMIP_AREXX_CMD_MENUPICK, /* MENUPICK <window-pattern> <menu-num> <item-num> [<sub-num>] */
    AMIP_AREXX_CMD_DRAG,     /* DRAG <window-pattern> (<gadget-id> | ROLE=<r> [LABEL=<l>] [INDEX=<n>]) <dx> <dy> |
                              * DRAG <window-pattern> (<gadget-id> | ROLE=<r> [LABEL=<l>] [INDEX=<n>]) TO (<dest-gadget-id> | @<dest-name>) |
                              * DRAG @<name> <dx> <dy> |
                              * DRAG @<name> TO (<dest-gadget-id> | @<dest-name>) */
    AMIP_AREXX_CMD_SCREENS,  /* SCREENS */
    AMIP_AREXX_CMD_AUTH,     /* AUTH <password> */
    AMIP_AREXX_CMD_WAITFOR,  /* WAITFOR [SCREEN=<s>] WINDOW=<pattern> [TIMEOUT=<n>] |
                              * WAITFOR [SCREEN=<s>] NOWINDOW=<pattern> [TIMEOUT=<n>] |
                              * WAITFOR [SCREEN=<s>] <window-pattern> (<gadget-id> | ROLE=<r> [LABEL=<l>] [INDEX=<n>]) TEXT=<value> [TIMEOUT=<n>] |
                              * WAITFOR @<name> TEXT=<value> [TIMEOUT=<n>] */
    AMIP_AREXX_CMD_SCREENSHOT, /* SCREENSHOT [SCREEN=<substring>] [WINDOW=<pattern>] --
                              * raw planar bitmap capture (phase 1.0);
                              * see server/include/screenshot.h */
    AMIP_AREXX_CMD_MUIREXX,  /* MUIREXX <app-base> [TIMEOUT=<n>] <command...> --
                              * the MUI-ARexx bridge tier (phase 0.5); see
                              * server/include/muirexx.h */
    AMIP_AREXX_CMD_WHERE,    /* WHERE @<name> [TIMEOUT=<n>] --
                              * diagnostic form of the cooperative
                              * geometry port (issue #49): queries a
                              * WHEREGADGET manifest entry's declared
                              * WHEREPORT and returns the raw
                              * "<x> <y> <w> <h>" reply; manifest-only
                              * (no classic <window-pattern> <gadget-id>
                              * form -- there is nothing to resolve
                              * without a WHEREPORT). See
                              * server/include/where.h and
                              * manifest/SPEC.md's "WHERE port" section.
                              * CLICK/TYPE @<name> route through the
                              * same query automatically when the
                              * manifest resolves the name to a
                              * WHEREGADGET -- this verb exists only as
                              * a standalone diagnostic/test probe. */
    AMIP_AREXX_CMD_WINDOWMOVE, /* WINDOWMOVE [SCREEN=<s>] <window-pattern> <dx> <dy> --
                              * moves a whole window by a real title-bar
                              * drag; classic form only, no "@name" (a
                              * window-level action, same scope as
                              * TREE/MENU, not a gadget-level one) */
    AMIP_AREXX_CMD_WINDOWSIZE, /* WINDOWSIZE [SCREEN=<s>] <window-pattern> <width> <height> --
                              * resizes a whole window to an absolute
                              * target via a real sizing-gadget drag;
                              * classic form only, same reasoning as
                              * WINDOWMOVE above */
    AMIP_AREXX_CMD_PICK,     /* PICK [SCREEN=<substring>] -- interactive
                              * "pick mode" discovery (issue #65): hit-
                              * tests the LIVE global pointer position
                              * against SCREEN='s windows (default
                              * screen if omitted, AmipFindScreen()'s
                              * own convention) and returns the
                              * TREE-shaped window line for whichever
                              * window contains it, plus (if any) the
                              * single gadget line for whichever gadget
                              * within that window also contains it --
                              * point at a gadget, get back its exact
                              * locator, no batch dump required. RC 5
                              * if no window on the target screen
                              * contains the point at all; a window hit
                              * with no gadget hit is still RC 0 (the
                              * window line alone, confirming the
                              * pointer is over chrome/background, not
                              * an error). See intuition-model's
                              * AmipReadPointerPosition() for the
                              * real, live-confirmed interlaced-screen
                              * pointer-Y correction this relies on. */
    AMIP_AREXX_CMD_QUIT      /* QUIT */
} AmipArexxCmdType;

/* ARexx RC convention (matches ../amiauth's, a real prior-art pattern
 * for this project's sibling apps -- see its userdocs/ARexx-Port.md):
 * 0 success, 5 warning (window/gadget not found -- the command was
 * well-formed but had nothing to act on), 10 error (bad syntax/unknown
 * command), 15 timeout (WAITFOR/CLICK's EXPECT= -- the condition never
 * became true within TIMEOUT; distinct from WARN because the command
 * DID find its target and, for CLICK, the action itself DID happen --
 * "the click delivered but the expected effect never showed up" is a
 * genuinely different failure a test author may want to handle
 * differently from "nothing matched the locator at all"), 20 failure
 * (the action itself didn't deliver -- input.device event injection
 * failed). */
enum {
    AMIP_AREXX_RC_OK      =  0,
    AMIP_AREXX_RC_WARN    =  5,
    AMIP_AREXX_RC_ERROR   = 10,
    AMIP_AREXX_RC_TIMEOUT = 15,
    AMIP_AREXX_RC_FAIL    = 20
};

#define AMIP_AREXX_MAX_WINDOW 128
#define AMIP_AREXX_MAX_TEXT   256
#define AMIP_AREXX_MAX_NAME   32   /* manifest logical names ("@name") */
#define AMIP_AREXX_MAX_PATH   256  /* MANIFEST file path */
#define AMIP_AREXX_MAX_COMMAND 256 /* LAUNCH command line */
#define AMIP_AREXX_MAX_FSPUT  16384 /* FSPUT's declared byte-count cap --
                                    * duplicated from fs.c's own
                                    * AMIP_FS_BUF_SIZE (this file stays
                                    * portable/host-testable, no fs.h
                                    * dependency, same reasoning as every
                                    * other per-file duplicated constant
                                    * here) -- keep both in sync if either
                                    * changes. */
#define AMIP_AREXX_MAX_FSPUT_DRAIN (AMIP_AREXX_MAX_FSPUT * 10) /* FSPUT's
                                    * declared-byte-count DRAIN ceiling --
                                    * a real, verified bug found in code
                                    * review: a count between
                                    * AMIP_AREXX_MAX_FSPUT and this ceiling
                                    * is rejected (RC_ERROR, "too large")
                                    * but the wire transport still drains
                                    * exactly that many bytes off the wire
                                    * (discarding, not into g_fsPutBuf,
                                    * which stays sized to the real cap --
                                    * see AmipSerialDrainExact()/
                                    * AmipTcpDrainExact()) before replying,
                                    * so the connection never desyncs for
                                    * this case -- an ordinary "tried to
                                    * FSPUT a file bigger than the cap"
                                    * mistake, not malicious input. A
                                    * count above THIS ceiling, or
                                    * negative, is rejected outright with
                                    * NO drain attempt (out->type forced to
                                    * AMIP_AREXX_CMD_UNKNOWN in
                                    * AmipArexxParse()) -- genuinely
                                    * unrecoverable/not worth the wait, an
                                    * accepted, documented desync risk for
                                    * that specific malformed-or-abusive
                                    * edge case only. Both drain functions
                                    * share ONE timeout budget across the
                                    * whole call (same accounting as
                                    * AmipSerialReadExact()/
                                    * AmipTcpReadExact() -- see their own
                                    * doc comments), so raising this
                                    * ceiling does not raise worst-case
                                    * wait time, only how much a
                                    * legitimately-oversized request can
                                    * have discarded within that time. */
#define AMIP_AREXX_MAX_ROLE   32   /* "ROLE=<name>" -- longest real name is
                                    * "radio_button"/"listbrowser", both well
                                    * under this */
#define AMIP_AREXX_MAX_WB_TOOLTYPES 8   /* WBLAUNCH's repeatable TOOLTYPE=
                                        * cap -- matches wblaunch.h's
                                        * AMIP_WB_MAX_TOOLTYPES (duplicated,
                                        * same per-file-portable reasoning
                                        * as every other cap here) */
#define AMIP_AREXX_MAX_WB_ARGS      8   /* WBLAUNCH's repeatable ARG= cap --
                                        * matches wblaunch.h's
                                        * AMIP_WB_MAX_ARGS */
#define AMIP_AREXX_MAX_WB_TT_KEY   32   /* matches wblaunch.h's
                                        * AMIP_WB_TT_KEY_LEN */
#define AMIP_AREXX_MAX_WB_TT_VALUE 192  /* matches wblaunch.h's
                                        * AMIP_WB_TT_VALUE_LEN */

typedef struct {
    AmipArexxCmdType type;
    char windowPattern[AMIP_AREXX_MAX_WINDOW]; /* TREE/CLICK/TYPE/GETTEXT (classic form) */
    char screenPattern[AMIP_AREXX_MAX_WINDOW]; /* optional "SCREEN=<substring>" prefix on
                                                * TREE/CLICK/TYPE/GETTEXT/MENU/MENUPICK's
                                                * classic form; empty = unset (search every
                                                * screen, today's unchanged behavior) */
    long gadgetId;                             /* CLICK/TYPE/GETTEXT/DRAG (classic
                                                * form) when gadgetLocatorMode is 0 */
    char manifestName[AMIP_AREXX_MAX_NAME];    /* CLICK/TYPE/GETTEXT/DRAG "@name" form;
                                                * empty = classic form was used */
    int gadgetLocatorMode;                     /* CLICK/TYPE/GETTEXT/DRAG classic form
                                                * only: 0 = gadgetId above is the
                                                * locator (today's behavior); 1 =
                                                * roleName/labelSubstring/locatorIndex
                                                * below are the locator (tier-2,
                                                * "gadget by role + label text, or by
                                                * position-in-set" -- docs/
                                                * implementation-plan.md's "Locator
                                                * tiers"). Never set for the "@name"
                                                * form, which is always a single exact
                                                * gadget. */
    char roleName[AMIP_AREXX_MAX_ROLE];        /* "ROLE=<name>" token, e.g. "button" --
                                                * matched case-insensitively against
                                                * AmipRoleName()'s own vocabulary
                                                * (AmipRoleFromName(), intuition-model);
                                                * empty = any role. */
    char labelSubstring[AMIP_AREXX_MAX_TEXT];  /* "LABEL=<substring>" token, quotable
                                                * the same as a window pattern;
                                                * case-SENSITIVE substring match
                                                * (strstr) against a gadget's label --
                                                * same convention as window/screen
                                                * pattern matching (AmipFindWindow,
                                                * action.c), not a new inconsistent
                                                * behavior; empty = any label
                                                * (role-only locator). */
    long locatorIndex;                         /* "INDEX=<n>" token, 0-based; picks the
                                                * n'th gadget (in chain order) matching
                                                * roleName/labelSubstring when more than
                                                * one does. Default 0 (the first
                                                * match) when INDEX= is omitted. */
    char text[AMIP_AREXX_MAX_TEXT];             /* TYPE */
    char path[AMIP_AREXX_MAX_PATH];             /* MANIFEST; also dual-purposed
                                                  * for AUTH's <password> token,
                                                  * same "single-token bucket"
                                                  * FSLIST/FSSTAT/etc. already
                                                  * share -- not worth a
                                                  * dedicated field for one
                                                  * more single-string verb */
    char command[AMIP_AREXX_MAX_COMMAND];       /* LAUNCH */
    long stackSize;                             /* LAUNCH; 0 = use CreateNewProc's
                                                  * own default (4000 bytes) */
    char wbToolTypeKeys[AMIP_AREXX_MAX_WB_TOOLTYPES][AMIP_AREXX_MAX_WB_TT_KEY];
    char wbToolTypeValues[AMIP_AREXX_MAX_WB_TOOLTYPES][AMIP_AREXX_MAX_WB_TT_VALUE];
    int wbNumToolTypes;                         /* WBLAUNCH's repeatable
                                                  * "TOOLTYPE=<key>=<value>" */
    char wbArgs[AMIP_AREXX_MAX_WB_ARGS][AMIP_AREXX_MAX_PATH];
    int wbNumArgs;                              /* WBLAUNCH's repeatable
                                                  * "ARG=<path>" */
    long fsPutLen;                              /* FSPUT's declared byte-count
                                                  * -- how many raw bytes follow
                                                  * the request line on the
                                                  * wire, allowed up to
                                                  * AMIP_AREXX_MAX_FSPUT_DRAIN
                                                  * (see fsPutTooLarge below
                                                  * for anything over the
                                                  * real AMIP_AREXX_MAX_FSPUT
                                                  * cap). Reuses `path` above
                                                  * for the target path and
                                                  * `expectTimeout` below for
                                                  * its own optional TIMEOUT=. */
    int fsPutTooLarge;                          /* FSPUT only: 1 if
                                                  * fsPutLen exceeds the real
                                                  * AMIP_AREXX_MAX_FSPUT cap
                                                  * (but not the drain
                                                  * ceiling, AMIP_AREXX_MAX_
                                                  * FSPUT_DRAIN -- a count
                                                  * beyond THAT is rejected
                                                  * at parse time instead,
                                                  * type forced to
                                                  * AMIP_AREXX_CMD_UNKNOWN).
                                                  * The wire dispatch loops
                                                  * (amipilotserver/main.c)
                                                  * still drain exactly
                                                  * fsPutLen bytes off the
                                                  * wire when this is set --
                                                  * discarding them, never
                                                  * into g_fsPutBuf -- before
                                                  * replying AMIP_AREXX_RC_
                                                  * ERROR, so the connection
                                                  * stays in sync for this
                                                  * ordinary "tried to write
                                                  * a file bigger than the
                                                  * cap" case. */
    long menuNum, itemNum;                      /* MENUPICK */
    long subNum;                                /* MENUPICK; -1 = a top-level
                                                  * item, not a submenu entry */
    int dragIsOffset;                           /* DRAG: 1 = dragDx/dragDy below are
                                                  * the destination (offset form);
                                                  * 0 = dragToGadgetId/
                                                  * dragToManifestName are (gadget-to-
                                                  * gadget form, via "TO ..."). */
    long dragDx, dragDy;                        /* DRAG offset form: pixels from the
                                                  * source gadget's current center.
                                                  * Reused directly for WINDOWMOVE's
                                                  * own <dx> <dy> -- identical
                                                  * "pixel offset" semantic, not
                                                  * worth a dedicated pair of
                                                  * fields. */
    long windowTargetWidth, windowTargetHeight; /* WINDOWSIZE's <width> <height>
                                                  * -- an ABSOLUTE target size, a
                                                  * genuinely different semantic
                                                  * from dragDx/dragDy's offset
                                                  * above, so this gets its own
                                                  * fields rather than reusing
                                                  * those. */
    long dragToGadgetId;                        /* DRAG gadget-to-gadget form:
                                                  * destination GA_ID, in the same
                                                  * window as the source locator.
                                                  * Filled in directly from a numeric
                                                  * "TO <n>" token, or resolved from
                                                  * dragToManifestName below (same
                                                  * up-front manifest-resolution pass
                                                  * the primary locator's @name form
                                                  * already gets, see
                                                  * amipilotserver/main.c). */
    char dragToManifestName[AMIP_AREXX_MAX_NAME]; /* DRAG gadget-to-gadget form:
                                                    * "TO @<dest-name>"; empty =
                                                    * dragToGadgetId was given
                                                    * directly instead. */
    int expectMode;                             /* WAITFOR, and CLICK's optional
                                                  * trailing EXPECT=: 0 = none
                                                  * (CLICK behaves exactly as
                                                  * before this field existed);
                                                  * 1 = WINDOW=<pattern> --  wait
                                                  * for a window matching
                                                  * expectPattern to appear
                                                  * (always a fresh
                                                  * AmipFindWindow() search,
                                                  * there being no prior
                                                  * identity to compare
                                                  * against); 2 = NOWINDOW --
                                                  * wait for a window to close.
                                                  * On WAITFOR this is
                                                  * NOWINDOW=<pattern>
                                                  * (expectPattern set, a fresh
                                                  * AmipFindWindow() search
                                                  * returning NULL -- an
                                                  * honest, slightly weaker
                                                  * guarantee than CLICK's own
                                                  * form, since a DIFFERENT
                                                  * window could coincidentally
                                                  * match the same pattern
                                                  * later); on CLICK this is
                                                  * bare NOWINDOW (no argument,
                                                  * expectPattern unused) --
                                                  * checked by POINTER IDENTITY
                                                  * via the exact struct
                                                  * Window* CLICK itself just
                                                  * resolved and acted on
                                                  * (AmipIsWindowOpen(),
                                                  * action.c), not a pattern
                                                  * re-search -- the precise
                                                  * "snapshot the delta"
                                                  * guarantee docs/
                                                  * implementation-plan.md's
                                                  * "Async by design" section
                                                  * describes; 3 = TEXT=<value>
                                                  * -- WAITFOR only (not
                                                  * CLICK's EXPECT=): wait for
                                                  * a gadget's text (the same
                                                  * value-or-label convention
                                                  * GETTEXT itself uses) to
                                                  * EXACTLY equal expectText.
                                                  * The gadget is located via
                                                  * this same struct's
                                                  * windowPattern/gadgetId/
                                                  * gadgetLocatorMode/
                                                  * roleName/labelSubstring/
                                                  * locatorIndex/manifestName
                                                  * fields -- WAITFOR's TEXT=
                                                  * form falls through into
                                                  * exactly the same window-
                                                  * pattern-or-@name +
                                                  * gadget-locator parsing
                                                  * CLICK/TYPE/GETTEXT/DRAG
                                                  * already share, rather than
                                                  * duplicating it; 4 =
                                                  * REQUESTER -- WAITFOR only,
                                                  * no argument (expectPattern/
                                                  * expectText both unused):
                                                  * wait for ANY currently-
                                                  * open window (optionally
                                                  * narrowed by this same
                                                  * struct's screenPattern) to
                                                  * have a genuine Intuition
                                                  * Requester attached --
                                                  * either a non-NULL
                                                  * FirstRequest (the classic
                                                  * Request()-based struct
                                                  * Requester chain), OR
                                                  * another currently-open
                                                  * window on the same screen
                                                  * sharing its EXACT title
                                                  * text (what
                                                  * AutoRequest()/
                                                  * BuildSysRequest()/
                                                  * EasyRequest() actually
                                                  * produce when given a
                                                  * real owning window --
                                                  * confirmed live against
                                                  * fixtures/gadtools-app's
                                                  * own "Ask" button that
                                                  * BOTH FirstRequest and
                                                  * the GTYP_REQGADGET bit
                                                  * BuildSysRequest()'s own
                                                  * autodoc claims are
                                                  * genuinely absent on this
                                                  * project's target OS/ROM
                                                  * -- see
                                                  * WaitForRequesterPresent()'s
                                                  * own comment in
                                                  * amipilotserver/main.c for
                                                  * the full story).
                                                  * Detection only (GitHub
                                                  * issue #52's "cheap first
                                                  * step"): no way yet to
                                                  * address or click a
                                                  * Requester's own gadgets,
                                                  * and this only sees
                                                  * window-attached
                                                  * Requesters, not a
                                                  * system-wide one with no
                                                  * owning window (a
                                                  * disk-swap prompt, say) --
                                                  * both real, stated scope
                                                  * lines, not silent gaps. */
    char expectPattern[AMIP_AREXX_MAX_WINDOW];  /* WAITFOR's WINDOW=/NOWINDOW=
                                                  * pattern, and CLICK's
                                                  * EXPECT=WINDOW= pattern;
                                                  * unused (empty) for CLICK's
                                                  * bare EXPECT=NOWINDOW and
                                                  * for WAITFOR's TEXT= form. */
    char expectText[AMIP_AREXX_MAX_TEXT];       /* WAITFOR's TEXT=<value>
                                                  * token (expectMode == 3
                                                  * only); quotable the same
                                                  * as a window pattern. */
    long expectTimeout;                         /* WAITFOR/CLICK's EXPECT=:
                                                  * seconds to poll before
                                                  * giving up (AMIP_AREXX_RC_
                                                  * TIMEOUT); 0 = use the
                                                  * server's own default (10s,
                                                  * amipilotserver/main.c).
                                                  * Also reused for MUIREXX's
                                                  * and FSPUT's own TIMEOUT=
                                                  * (FSPUT's default is 30s,
                                                  * not 10s -- see
                                                  * amipilotserver/main.c --
                                                  * a real 16KB payload over
                                                  * a slow serial link
                                                  * genuinely needs longer)
                                                  * -- same "single-token
                                                  * bucket" reuse convention
                                                  * path/command above already
                                                  * use, not worth a
                                                  * dedicated field for one
                                                  * more "seconds to poll"
                                                  * value. */
    char muiBase[AMIP_AREXX_MAX_NAME];          /* MUIREXX's <app-base> token
                                                  * -- the target MUI app's
                                                  * ARexx port base name
                                                  * (MUIA_Application_Base),
                                                  * e.g. "MUIDEMO". Reuses
                                                  * AMIP_AREXX_MAX_NAME since
                                                  * MUI's own naming rule
                                                  * caps a basename at 30
                                                  * characters -- see
                                                  * muirexx.h. */
    int argTooLong;                             /* set when some argument didn't
                                                  * fit its field (see the
                                                  * AMIP_AREXX_MAX_* caps above)
                                                  * -- type is forced to
                                                  * AMIP_AREXX_CMD_UNKNOWN in
                                                  * this case, same as any
                                                  * other parse failure, but
                                                  * this flag lets the caller
                                                  * report a specific reason
                                                  * instead of the generic
                                                  * "bad arguments" message,
                                                  * per this project's own
                                                  * "explicit, not silent"
                                                  * convention -- see the
                                                  * doc comment on
                                                  * read_token() in
                                                  * arexx_cmd.c for why this
                                                  * matters (a value that was
                                                  * silently truncated here
                                                  * could otherwise cause the
                                                  * server to act on the
                                                  * WRONG path/pattern, not
                                                  * just report a cosmetic
                                                  * error). */
} AmipArexxParsed;

/* Parses one ARexx command line into `out`. Case-insensitive command
 * keyword; window-pattern accepts a double-quoted form for patterns
 * containing spaces; TYPE's text argument is everything after the
 * gadget ID (or "@name"), unquoted-verbatim (so "TYPE GadTools 2 hello
 * world" types "hello world" without needing to quote it) or the quoted
 * form if it starts with '"'.
 *
 * CLICK/TYPE/GETTEXT accept "@<logical-name>" in place of the
 * <window-pattern> <gadget-id> pair -- resolved by the caller against
 * the currently-loaded manifest (manifest.h). The '@' prefix is what
 * disambiguates the two forms; a bare name would be ambiguous with a
 * window pattern.
 *
 * CLICK/TYPE/GETTEXT's classic form also accepts a tier-2 semantic
 * locator in place of the bare <gadget-id>: one or more of
 * "ROLE=<name>" (matched against AmipRoleName()'s vocabulary --
 * "button", "string", "slider", etc. -- case-insensitively),
 * "LABEL=<substring>" (quotable the same as a window pattern;
 * case-SENSITIVE substring match, same convention as window/screen
 * pattern matching), and "INDEX=<n>" (0-based; picks
 * the n'th match in gadget-chain order when more than one gadget
 * matches, default 0). At least one of ROLE=/LABEL= must be given to
 * enter this form; a bare digit is always the classic numeric
 * <gadget-id>, unchanged. Resolved server-side against a live walk of
 * the target window (server/src/amipilotserver/main.c's
 * ResolveTargetGadget()) -- no match is AMIP_AREXX_RC_WARN, same
 * class as an unmatched numeric ID. Proximity-to-a-label matching
 * (docs/implementation-plan.md's third tier-2 locator style) is
 * deliberately not built -- see CLAUDE.md's "honest limits"
 * convention; ROLE=/LABEL=/INDEX= is the full locator vocabulary this
 * parser understands today.
 *
 * FSLIST/FSSTAT/FSMKDIR/FSDELETE/FSGET all take a single <path>
 * argument, parsed exactly like MANIFEST's (into the same `path`
 * field) -- see server/include/fs.h for the allowlist enforcement and
 * per-verb semantics; this parser doesn't know or care about roots,
 * only that a path token follows the keyword.
 *
 * LAUNCH's command line is everything after the keyword and an
 * optional leading "STACK=<n>" token (not an AmigaDOS Shell
 * convention -- this wire's own syntax, consumed here before the rest
 * of the line is taken verbatim as the command to run, same
 * unquoted-rest-of-line handling as TYPE's text). "LAUNCH SRC:build/
 * fixtures/GTApp" and "LAUNCH STACK=8192 SRC:build/fixtures/GTApp"
 * are both valid; stackSize is 0 (caller's own default) when STACK
 * isn't given.
 *
 * WBLAUNCH takes a single <icon-path> (parsed exactly like MANIFEST's,
 * into the same `path` field -- WITHOUT the ".info" suffix, icon.
 * library's own convention), followed by any mix, in any order, of
 * repeatable "TOOLTYPE=<key>=<value>" (up to AMIP_AREXX_MAX_WB_
 * TOOLTYPES) and "ARG=<path>" (up to AMIP_AREXX_MAX_WB_ARGS) tokens.
 * A TOOLTYPE='s <key> is everything up to the FIRST '=' after it;
 * <value> is everything after that (so a value may itself contain
 * '=', matching real tooltype values like "CLI=NEWCLI \"NIL:\""). See
 * server/include/wblaunch.h for what this verb actually does (a real,
 * hand-built Workbench-style launch, not SystemTagList()'s Shell-style
 * one LAUNCH above uses) and why tooltype overrides need a scratch
 * disk write. Unlike FSPUT, this carries no binary wire payload, so
 * (like LAUNCH) it's fully answerable over ARexx too -- one grammar,
 * three surfaces, no asymmetry here. No "@name" form, though (manifest
 * locators name GADGETS within an already-loaded app's window, not
 * icon files to launch -- a different kind of identifier).
 *
 * MENU takes a single <window-pattern>, same as TREE (no "@name"
 * form -- menus aren't part of the manifest contract). MENUPICK takes
 * <window-pattern> <menu-num> <item-num> [<sub-num>], three or four
 * space-separated tokens with no "@name" form either; subNum is -1
 * (top-level item) when the fourth token is omitted. These are
 * 0-based chain positions, the same ones intuition-model's
 * AmipWalkMenuStrip() stamps onto its model (see MENU's own output)
 * and Intuition itself reports via IDCMP_MENUPICK's MENUNUM()/
 * ITEMNUM()/SUBNUM() macros.
 *
 * TREE/CLICK/TYPE/GETTEXT/MENU (classic form only, not "@name") and
 * MENUPICK all additionally accept an optional leading
 * "SCREEN=<substring>" token before the window-pattern -- same
 * "consume a KEYWORD=value prefix, then fall through to the normal
 * parse" idiom LAUNCH's own "STACK=<n>" already uses. Narrows the
 * window search to screens whose DefaultTitle contains the substring
 * (server/src/action.c's AmipFindWindow -- deliberately DefaultTitle,
 * not the live Title field, which tracks whichever window is
 * currently active on that screen rather than naming the screen
 * itself). Omitted = today's unchanged behavior: search every screen.
 *
 * DRAG's SOURCE locator is parsed exactly like CLICK/TYPE/GETTEXT's
 * (classic <window-pattern> + numeric-or-ROLE/LABEL/INDEX locator, or
 * "@<name>", with the same optional leading "SCREEN=<substring>" on
 * the classic form) -- it gets tier-2 locators for free from the same
 * code path. After the source locator, DRAG takes exactly one of:
 *   - "<dx> <dy>" -- two signed integers, the offset form
 *     (dragIsOffset=1, dragDx/dragDy set): drags the source gadget's
 *     current center by that pixel offset. The natural shape for
 *     adjusting a slider/scroller (GadTools SLIDER_KIND/PROP_KIND),
 *     which is a delta operation.
 *   - "TO <dest-gadget-id>" or "TO @<dest-name>" -- the gadget-to-
 *     gadget form (dragIsOffset=0): drags the source gadget's current
 *     center onto the destination's, both resolved live at action
 *     time. The destination is always resolved against the SAME
 *     window as the source (no cross-window drag) -- "TO @<dest-name>"
 *     resolves dragToManifestName into dragToGadgetId the same way the
 *     primary "@name" locator resolves into windowPattern/gadgetId
 *     (server/src/amipilotserver/main.c's HandleCommand(), a second,
 *     parallel resolution pass for this second locator). Note the
 *     destination locator is numeric-or-@name ONLY -- no ROLE=/LABEL=
 *     form for the destination, keeping this verb's scope contained.
 * Which of the two forms is present is determined by whether the next
 * token after the source locator is (case-insensitively) "TO" -- not
 * by argument count, since both forms could otherwise be ambiguous
 * with a stray trailing token.
 *
 * SCREENS takes no arguments, like VERSION/QUIT.
 *
 * WAITFOR takes an optional leading "SCREEN=<substring>" (same idiom
 * as TREE/CLICK/etc.'s own), then one of three condition forms:
 *   - "WINDOW=<pattern>" -- waits for a window matching <pattern> to
 *     appear.
 *   - "NOWINDOW=<pattern>" -- waits for no window to match <pattern>
 *     (always a fresh pattern re-search each poll, since a standalone
 *     WAITFOR has no prior action to anchor an exact window identity
 *     to -- see CLICK's own EXPECT=NOWINDOW below for the identity-
 *     based alternative).
 *   - <window-pattern> (or "@<name>") followed by a gadget locator
 *     (numeric <gadget-id>, or ROLE=/LABEL=/INDEX=, exactly like
 *     CLICK/TYPE/GETTEXT/DRAG's own -- this form falls through into
 *     that same shared parsing code) followed by "TEXT=<value>" --
 *     waits for the gadget's text (GETTEXT's own value-or-label
 *     convention) to EXACTLY equal <value>. WAITFOR-only, not
 *     available as one of CLICK's own EXPECT= conditions: the gadget
 *     whose text changes as a result of a click is often a DIFFERENT
 *     gadget than the one clicked, which would need a second,
 *     independent locator embedded inside EXPECT= -- real added
 *     complexity, deferred rather than silently built partway.
 * Then an optional trailing "TIMEOUT=<n>" (seconds; default 10 if
 * omitted) on any form. Maps to AMIP_AREXX_RC_OK if the condition
 * becomes true in time, AMIP_AREXX_RC_TIMEOUT otherwise -- or
 * AMIP_AREXX_RC_WARN, same as CLICK/TYPE/GETTEXT, if the TEXT= form's
 * window/gadget locator itself doesn't resolve to anything (a
 * different failure than "found it but the text never matched").
 *
 * CLICK's classic and "@name" forms both additionally accept an
 * optional trailing "EXPECT=WINDOW=<pattern>" or bare
 * "EXPECT=NOWINDOW" (no argument), plus an optional trailing
 * "TIMEOUT=<n>" -- same condition vocabulary as WAITFOR, but
 * EXPECT=NOWINDOW means something more precise than WAITFOR's own
 * NOWINDOW=<pattern>: it's checked by POINTER IDENTITY against the
 * exact window CLICK itself just resolved and clicked
 * (AmipIsWindowOpen(), server/src/action.c), not a fresh pattern
 * search -- "the window I just acted on is now closed," not "nothing
 * matches this pattern right now" (a different, weaker claim a
 * same-titled replacement window could satisfy without the original
 * ever having closed). This is the atomic "snapshot right after
 * acting, watch for the exact delta" primitive docs/
 * implementation-plan.md's "Async by design" section describes --
 * the click itself still happens regardless of EXPECT=; a timeout
 * waiting for the expected effect (AMIP_AREXX_RC_TIMEOUT) is reported
 * distinctly from the click's own injection failing outright
 * (AMIP_AREXX_RC_FAIL, unchanged). EXPECT=WINDOW=<pattern> does not
 * get its own nested SCREEN= filter -- searches every screen for the
 * expected window, a deliberate v1 scope decision. TYPE/DRAG/MENUPICK
 * don't accept EXPECT= in this pass -- use a separate WAITFOR call
 * after them instead.
 *
 * AUTH takes a single <password> argument, parsed exactly like
 * MANIFEST's (into the same `path` field). Parseable and answerable
 * on every transport -- one grammar, per this project's own design
 * principle -- but only the TCP transport's own dispatch loop
 * (server/src/amipilotserver/main.c) actually gates anything on
 * whether it succeeded; on ARexx/serial.device it's accepted and
 * compared but has no side effect, since neither of those transports
 * ever enforces the auth flag HandleCommand() tracks.
 *
 * WINDOWMOVE/WINDOWSIZE take an optional leading "SCREEN=<substring>"
 * (same idiom TREE/CLICK's own leading one uses) then a plain
 * <window-pattern> (classic form ONLY -- no "@name" locator; these
 * act on a whole WINDOW, the same scope TREE/MENU already have,
 * neither of which takes "@name" either), then two required numeric
 * tokens: WINDOWMOVE's are <dx> <dy> (a pixel offset, into dragDx/
 * dragDy -- the same fields DRAG's own offset form uses), WINDOWSIZE's
 * are <width> <height> (an absolute target size, into
 * windowTargetWidth/windowTargetHeight). See server/include/
 * action_engine.h's AmipWindowMoveBy()/AmipWindowResizeTo() doc
 * comments for the actual mechanism (a real title-bar or sizing-
 * gadget drag) and their honest limits. There is no separate "get
 * window position/size" verb -- TREE's own response already carries
 * a window's `[left,top WxH]`, so querying needs no new verb.
 *
 * SCREENSHOT takes an optional "SCREEN=<substring>" (parsed exactly
 * like TREE/CLICK's own leading one, into the same `screenPattern`
 * field) and an optional "WINDOW=<pattern>" (into the same
 * `windowPattern` field TREE/CLICK's classic form uses -- empty means
 * "not given", since a real window pattern can't legitimately be
 * empty), in either order. Neither is required: with both omitted,
 * captures the frontmost/default public screen; SCREEN= alone selects
 * a screen by substring and captures it whole; WINDOW= (with or
 * without SCREEN= narrowing which screen to search) captures the
 * OWNING SCREEN'S full bitmap plus that window's rectangle in the
 * response header -- there is no separate per-window pixel buffer to
 * grab on classic Intuition (overlapping windows share one screen
 * bitmap), so "capturing a window" is always "capture the screen,
 * then the client crops" (see server/include/screenshot.h).
 *
 * MUIREXX takes <app-base> (a single token, the target MUI
 * application's ARexx port base name -- MUI's own naming rule caps it
 * at 30 characters and forbids spaces/":/()#?*..."), an optional
 * leading "TIMEOUT=<n>" (seconds; default 10, same idiom LAUNCH's own
 * "STACK=<n>" and WAITFOR's trailing "TIMEOUT=" use, but leading here
 * -- see AmipMuiRexxSend's own doc comment for why the command text
 * has to be the rest of the line, unparsed, same as LAUNCH's/TYPE's
 * own verbatim-rest-of-line handling), then the ARexx command line
 * itself, verbatim, handed to the target application's own port
 * exactly as an ARexx script's own ADDRESS would. This bridge does
 * not interpret or validate the command text -- see muirexx.h for the
 * full rationale (MUI's own built-in ARexx support is a small,
 * universal command set plus whatever the target app chose to add;
 * nothing generic enough to build a CLICK/TYPE-shaped verb on top of
 * exists).
 *
 * WHERE takes exactly "@<name>" (manifest form only -- no classic
 * <window-pattern> <gadget-id> alternative; there is no live gadget to
 * name numerically here, only a manifest entry naming a WHEREPORT), then
 * an optional trailing "TIMEOUT=<n>" (seconds; default 10, same idiom as
 * MUIREXX's own query timeout -- reuses expectTimeout). Returns the raw
 * "<x> <y> <w> <h>" reply as its RESULT text. RC_ERROR if the name isn't
 * in the manifest, or resolves to a plain GADGET rather than a
 * WHEREGADGET (this verb is WHEREGADGET-only, by design -- CLICK/TYPE
 * already handle a plain GADGET @name without going anywhere near a
 * port); RC mapping for the port query itself is in
 * server/include/where.h.
 *
 * FSPUT takes <path> (parsed exactly like FSLIST's own), then a
 * required <byte-count> token (decimal). A negative count, or one
 * beyond AMIP_AREXX_MAX_FSPUT_DRAIN, is rejected outright here at
 * parse time (out->type forced to AMIP_AREXX_CMD_UNKNOWN) -- neither
 * is worth attempting to drain (a negative count has no recoverable
 * byte length at all; a huge one isn't worth the wait), an accepted,
 * documented wire-desync risk for that specific malformed/abusive
 * edge case, same "reject outright, don't guess" policy every other
 * oversized argument on this wire already gets. A count between the
 * REAL cap (AMIP_AREXX_MAX_FSPUT) and the drain ceiling sets
 * fsPutTooLarge instead -- an ordinary "tried to FSPUT something
 * bigger than the cap" mistake, not malformed input, so the wire
 * dispatch loops still drain (and discard) exactly that many bytes
 * before replying with the real error, keeping the connection in
 * sync. Then an optional trailing "TIMEOUT=<n>". Parsing FSPUT stops
 * at byte-count and TIMEOUT= -- it does NOT read the <byte-count> raw bytes that
 * follow on the wire; this parser has no transport to read from (it's
 * shared with the portable ARexx-message path, which carries no such
 * raw byte stream at all). The wire-transport dispatch loops
 * (server/src/amipilotserver/main.c) are what actually receive the
 * payload, immediately after calling this parser and before calling
 * HandleCommand() -- see server/WIRE.md's request-payload framing.
 * FSPUT parses successfully over ARexx too (one grammar, per this
 * project's own design principle, same as AUTH above), but
 * HandleCommand() rejects it there with a clear "requires a wire
 * transport" error, since there is genuinely no payload to receive.
 *
 * Returns 0 on success, -1 on an unknown command or a missing required
 * argument (map to AMIP_AREXX_RC_ERROR) -- out->type is
 * AMIP_AREXX_CMD_UNKNOWN on failure. */
int AmipArexxParse(const char *cmdline, AmipArexxParsed *out);

#endif /* AMIPILOT_AREXX_CMD_H */
