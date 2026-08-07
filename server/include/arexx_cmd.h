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
    AMIP_AREXX_CMD_CLICK,    /* CLICK <window-pattern> <gadget-id> | CLICK @<name> */
    AMIP_AREXX_CMD_TYPE,     /* TYPE <window-pattern> <gadget-id> <text...> | TYPE @<name> <text...> */
    AMIP_AREXX_CMD_GETTEXT,  /* GETTEXT <window-pattern> <gadget-id> | GETTEXT @<name> */
    AMIP_AREXX_CMD_MANIFEST, /* MANIFEST <file-path> */
    AMIP_AREXX_CMD_VERSION,  /* VERSION -- the wire handshake (server/WIRE.md),
                              * also answerable over ARexx for feature tests */
    AMIP_AREXX_CMD_LAUNCH,   /* LAUNCH [STACK=n] <command-line...> */
    AMIP_AREXX_CMD_FSLIST,   /* FSLIST <path> */
    AMIP_AREXX_CMD_FSSTAT,   /* FSSTAT <path> */
    AMIP_AREXX_CMD_FSMKDIR,  /* FSMKDIR <path> */
    AMIP_AREXX_CMD_FSDELETE, /* FSDELETE <path> */
    AMIP_AREXX_CMD_FSGET,    /* FSGET <path> */
    AMIP_AREXX_CMD_MENU,     /* MENU <window-pattern> */
    AMIP_AREXX_CMD_MENUPICK, /* MENUPICK <window-pattern> <menu-num> <item-num> [<sub-num>] */
    AMIP_AREXX_CMD_SCREENS,  /* SCREENS */
    AMIP_AREXX_CMD_AUTH,     /* AUTH <password> */
    AMIP_AREXX_CMD_QUIT      /* QUIT */
} AmipArexxCmdType;

/* ARexx RC convention (matches ../amiauth's, a real prior-art pattern
 * for this project's sibling apps -- see its userdocs/ARexx-Port.md):
 * 0 success, 5 warning (window/gadget not found -- the command was
 * well-formed but had nothing to act on), 10 error (bad syntax/unknown
 * command), 20 failure (the action itself didn't deliver -- input.device
 * event injection failed). */
enum {
    AMIP_AREXX_RC_OK    =  0,
    AMIP_AREXX_RC_WARN  =  5,
    AMIP_AREXX_RC_ERROR = 10,
    AMIP_AREXX_RC_FAIL  = 20
};

#define AMIP_AREXX_MAX_WINDOW 128
#define AMIP_AREXX_MAX_TEXT   256
#define AMIP_AREXX_MAX_NAME   32   /* manifest logical names ("@name") */
#define AMIP_AREXX_MAX_PATH   256  /* MANIFEST file path */
#define AMIP_AREXX_MAX_COMMAND 256 /* LAUNCH command line */

typedef struct {
    AmipArexxCmdType type;
    char windowPattern[AMIP_AREXX_MAX_WINDOW]; /* TREE/CLICK/TYPE/GETTEXT (classic form) */
    char screenPattern[AMIP_AREXX_MAX_WINDOW]; /* optional "SCREEN=<substring>" prefix on
                                                * TREE/CLICK/TYPE/GETTEXT/MENU/MENUPICK's
                                                * classic form; empty = unset (search every
                                                * screen, today's unchanged behavior) */
    long gadgetId;                             /* CLICK/TYPE/GETTEXT (classic form) */
    char manifestName[AMIP_AREXX_MAX_NAME];    /* CLICK/TYPE/GETTEXT "@name" form;
                                                * empty = classic form was used */
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
    long menuNum, itemNum;                      /* MENUPICK */
    long subNum;                                /* MENUPICK; -1 = a top-level
                                                  * item, not a submenu entry */
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
 * SCREENS takes no arguments, like VERSION/QUIT.
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
 * Returns 0 on success, -1 on an unknown command or a missing required
 * argument (map to AMIP_AREXX_RC_ERROR) -- out->type is
 * AMIP_AREXX_CMD_UNKNOWN on failure. */
int AmipArexxParse(const char *cmdline, AmipArexxParsed *out);

#endif /* AMIPILOT_AREXX_CMD_H */
