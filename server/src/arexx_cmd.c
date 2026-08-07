/* arexx_cmd.c -- see arexx_cmd.h. */
#include <string.h>
#include <stdlib.h>

#include "arexx_cmd.h"

/* ASCII case-insensitive full-string compare -- same shape as
 * ../amiauth's arexx_cmd.c's own ci_streq, kept as a separate copy for
 * the same reason: this file stays a portable core module with no
 * dependency on anything Amiga-specific. */
static int ci_streq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

/* ASCII case-insensitive prefix compare: does `s` start with `prefix`? */
static int ci_streq_prefix(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++) {
        int cs = *s, cp = *prefix;
        if (cs >= 'a' && cs <= 'z') cs -= 32;
        if (cp >= 'a' && cp <= 'z') cp -= 32;
        if (cs != cp) return 0;
    }
    return 1;
}

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* Reads one token starting at p. A leading '"' reads a quoted token up
 * to the closing '"', with backslash-escaping: '\"' -> literal '"',
 * '\\' -> literal '\', matching the escaping the server's own reply
 * side already applies (EscapeQuotesInto()/EscapeQuotes() in fs.c/
 * amipilotserver/main.c) -- without this, a name/path the server can
 * safely REPORT (via FSLIST/TREE/etc.) could never be SENT back as an
 * argument, since a bare '"' inside it would end the token early. A
 * lone trailing backslash (no following char) is kept literal, same
 * as the host's own unescape() in model.py. Otherwise (no leading
 * '"') reads up to the next whitespace, no escaping (unquoted tokens
 * are for simple values -- window patterns rarely need one, and
 * quoting is always available when they do).
 *
 * Copies into dst (cap bytes, NUL-terminated). If the token as
 * written would not fit, dst still gets a truncated-but-valid
 * C string (never overflowed), but *truncated (if non-NULL) is set
 * to 1 so the caller can treat this as an explicit error rather than
 * silently acting on a chopped value -- see AmipArexxParse's use of
 * this. Returns a pointer just past the token in the source line. */
static const char *read_token(const char *p, char *dst, size_t cap, int *truncated)
{
    size_t n = 0;
    int trunc = 0;

    if (*p == '"') {
        p++;
        while (*p && *p != '"') {
            char c = *p;
            if (c == '\\' && (p[1] == '"' || p[1] == '\\')) {
                c = p[1];
                p++;
            }
            if (n + 1 < cap) { dst[n++] = c; } else { trunc = 1; }
            p++;
        }
        if (*p == '"') p++;
    } else {
        while (*p && *p != ' ' && *p != '\t') {
            if (n + 1 < cap) { dst[n++] = *p; } else { trunc = 1; }
            p++;
        }
    }
    dst[n] = '\0';
    if (truncated != NULL) *truncated = trunc;
    return p;
}

/* Consumes an optional "SCREEN=<value>" token at *pp -- same
 * KEYWORD=value idiom LAUNCH's own "STACK=<n>" already uses, but
 * <value> is a token (quotable the same way a window pattern is,
 * via read_token above) rather than a fixed-digit number. Writes
 * <value> into dst (cap bytes) and advances *pp past it plus any
 * following whitespace if the prefix is present; leaves *pp and dst
 * untouched otherwise (dst already reads as "" from AmipArexxParse's
 * own memset). *truncated is set the same way read_token's own is. */
static void parse_optional_screen_prefix(const char **pp, char *dst, size_t cap,
                                         int *truncated)
{
    const char *p = *pp;

    if (truncated != NULL) *truncated = 0;
    if (ci_streq_prefix(p, "SCREEN=")) {
        p += 7; /* strlen("SCREEN=") */
        p = read_token(p, dst, cap, truncated);
        *pp = skip_ws(p);
    }
}

/* Marks a truncated argument as an explicit parse failure (argTooLong)
 * rather than letting the caller silently proceed on a chopped value
 * -- see arexx_cmd.h's doc comment on argTooLong for why this matters
 * beyond cosmetics. Returns 1 (so callers can `if (fail_if_trunc(...))
 * return -1;`) when trunc is set, 0 otherwise. */
static int fail_if_trunc(int trunc, AmipArexxParsed *out)
{
    if (trunc) {
        out->argTooLong = 1;
        out->type = AMIP_AREXX_CMD_UNKNOWN;
        return 1;
    }
    return 0;
}

int AmipArexxParse(const char *cmdline, AmipArexxParsed *out)
{
    char kw[16];
    const char *p;

    if (cmdline == NULL || out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->type = AMIP_AREXX_CMD_UNKNOWN;

    p = skip_ws(cmdline);
    /* kw's own truncation isn't checked -- a keyword longer than the
     * longest real one (7 chars, "MANIFEST"/"FSDELETE") just fails
     * every ci_streq() below and falls through to the existing
     * "unknown command" path, which is already the correct outcome. */
    p = read_token(p, kw, sizeof(kw), NULL);

    if      (ci_streq(kw, "TREE"))     out->type = AMIP_AREXX_CMD_TREE;
    else if (ci_streq(kw, "CLICK"))    out->type = AMIP_AREXX_CMD_CLICK;
    else if (ci_streq(kw, "TYPE"))     out->type = AMIP_AREXX_CMD_TYPE;
    else if (ci_streq(kw, "GETTEXT"))  out->type = AMIP_AREXX_CMD_GETTEXT;
    else if (ci_streq(kw, "MANIFEST")) out->type = AMIP_AREXX_CMD_MANIFEST;
    else if (ci_streq(kw, "VERSION"))  out->type = AMIP_AREXX_CMD_VERSION;
    else if (ci_streq(kw, "LAUNCH"))   out->type = AMIP_AREXX_CMD_LAUNCH;
    else if (ci_streq(kw, "FSLIST"))   out->type = AMIP_AREXX_CMD_FSLIST;
    else if (ci_streq(kw, "FSSTAT"))   out->type = AMIP_AREXX_CMD_FSSTAT;
    else if (ci_streq(kw, "FSMKDIR"))  out->type = AMIP_AREXX_CMD_FSMKDIR;
    else if (ci_streq(kw, "FSDELETE")) out->type = AMIP_AREXX_CMD_FSDELETE;
    else if (ci_streq(kw, "FSGET"))    out->type = AMIP_AREXX_CMD_FSGET;
    else if (ci_streq(kw, "MENU"))     out->type = AMIP_AREXX_CMD_MENU;
    else if (ci_streq(kw, "MENUPICK")) out->type = AMIP_AREXX_CMD_MENUPICK;
    else if (ci_streq(kw, "SCREENS"))  out->type = AMIP_AREXX_CMD_SCREENS;
    else if (ci_streq(kw, "AUTH"))     out->type = AMIP_AREXX_CMD_AUTH;
    else if (ci_streq(kw, "QUIT"))     out->type = AMIP_AREXX_CMD_QUIT;
    else { out->type = AMIP_AREXX_CMD_UNKNOWN; return -1; }

    if (out->type == AMIP_AREXX_CMD_QUIT || out->type == AMIP_AREXX_CMD_VERSION
        || out->type == AMIP_AREXX_CMD_SCREENS) {
        return 0;
    }

    if (out->type == AMIP_AREXX_CMD_MANIFEST ||
        out->type == AMIP_AREXX_CMD_FSLIST ||
        out->type == AMIP_AREXX_CMD_FSSTAT ||
        out->type == AMIP_AREXX_CMD_FSMKDIR ||
        out->type == AMIP_AREXX_CMD_FSDELETE ||
        out->type == AMIP_AREXX_CMD_FSGET ||
        out->type == AMIP_AREXX_CMD_AUTH) {
        int trunc;
        p = skip_ws(p);
        if (*p == '\0') {
            out->type = AMIP_AREXX_CMD_UNKNOWN;
            return -1;
        }
        read_token(p, out->path, sizeof(out->path), &trunc);
        if (fail_if_trunc(trunc, out)) return -1;
        return 0;
    }

    if (out->type == AMIP_AREXX_CMD_MENUPICK) {
        char numbuf[16];
        int trunc;

        p = skip_ws(p);
        parse_optional_screen_prefix(&p, out->screenPattern, sizeof(out->screenPattern), &trunc);
        if (fail_if_trunc(trunc, out)) return -1;
        if (*p == '\0') {
            out->type = AMIP_AREXX_CMD_UNKNOWN;
            return -1;
        }
        p = read_token(p, out->windowPattern, sizeof(out->windowPattern), &trunc);
        if (fail_if_trunc(trunc, out)) return -1;

        p = skip_ws(p);
        if (*p == '\0') {
            out->type = AMIP_AREXX_CMD_UNKNOWN;
            return -1;
        }
        p = read_token(p, numbuf, sizeof(numbuf), &trunc);
        if (fail_if_trunc(trunc, out)) return -1;
        out->menuNum = strtol(numbuf, NULL, 10);

        p = skip_ws(p);
        if (*p == '\0') {
            out->type = AMIP_AREXX_CMD_UNKNOWN;
            return -1;
        }
        p = read_token(p, numbuf, sizeof(numbuf), &trunc);
        if (fail_if_trunc(trunc, out)) return -1;
        out->itemNum = strtol(numbuf, NULL, 10);

        out->subNum = -1;
        p = skip_ws(p);
        if (*p != '\0') {
            read_token(p, numbuf, sizeof(numbuf), &trunc);
            if (fail_if_trunc(trunc, out)) return -1;
            out->subNum = strtol(numbuf, NULL, 10);
        }
        return 0;
    }

    if (out->type == AMIP_AREXX_CMD_LAUNCH) {
        p = skip_ws(p);
        if (*p == '\0') {
            out->type = AMIP_AREXX_CMD_UNKNOWN;
            return -1;
        }
        if (ci_streq_prefix(p, "STACK=")) {
            char numbuf[16];
            const char *numStart = p + 6; /* strlen("STACK=") */
            const char *numEnd = numStart;
            while (*numEnd && *numEnd != ' ' && *numEnd != '\t') numEnd++;
            if (numEnd == numStart || (size_t)(numEnd - numStart) >= sizeof(numbuf)) {
                out->type = AMIP_AREXX_CMD_UNKNOWN;
                return -1;
            }
            memcpy(numbuf, numStart, (size_t)(numEnd - numStart));
            numbuf[numEnd - numStart] = '\0';
            out->stackSize = strtol(numbuf, NULL, 10);
            p = skip_ws(numEnd);
        }
        if (*p == '\0') {
            out->type = AMIP_AREXX_CMD_UNKNOWN;
            return -1;
        }
        /* Verbatim rest-of-line, same as TYPE's text -- the command
         * line is Shell syntax handed to SystemTagList() as-is, not
         * re-tokenized by this parser. Rejected outright (not
         * silently chopped) if it doesn't fit -- a truncated Shell
         * command line is a different, unintended command, not a
         * cosmetic loss. */
        if (strlen(p) >= sizeof(out->command)) {
            out->argTooLong = 1;
            out->type = AMIP_AREXX_CMD_UNKNOWN;
            return -1;
        }
        strncpy(out->command, p, sizeof(out->command) - 1);
        out->command[sizeof(out->command) - 1] = '\0';
        return 0;
    }

    /* Every other command starts with either a window-pattern argument
     * or a "@<logical-name>" manifest locator (see arexx_cmd.h), with
     * an optional leading "SCREEN=<substring>" ahead of the classic
     * form (a "SCREEN=x @name" combination is syntactically accepted
     * but the screen filter is simply not applied to the "@name"
     * form -- documented non-goal in arexx_cmd.h, not an error). */
    {
        int trunc;
        p = skip_ws(p);
        parse_optional_screen_prefix(&p, out->screenPattern, sizeof(out->screenPattern), &trunc);
        if (fail_if_trunc(trunc, out)) return -1;
        if (*p == '\0') {
            out->type = AMIP_AREXX_CMD_UNKNOWN;
            return -1;
        }
        if (*p == '@' && out->type != AMIP_AREXX_CMD_TREE && out->type != AMIP_AREXX_CMD_MENU) {
            p++;
            p = read_token(p, out->manifestName, sizeof(out->manifestName), &trunc);
            if (fail_if_trunc(trunc, out)) return -1;
            if (out->manifestName[0] == '\0') {
                out->type = AMIP_AREXX_CMD_UNKNOWN;
                return -1;
            }
        } else {
            p = read_token(p, out->windowPattern, sizeof(out->windowPattern), &trunc);
            if (fail_if_trunc(trunc, out)) return -1;
        }
    }

    if (out->type == AMIP_AREXX_CMD_TREE || out->type == AMIP_AREXX_CMD_MENU) {
        return 0;
    }

    /* Classic form: CLICK/TYPE/GETTEXT take a gadget locator next --
     * either a bare numeric <gadget-id> (today's original form) or a
     * tier-2 ROLE=/LABEL=/INDEX= locator (see arexx_cmd.h's doc
     * comment on AmipArexxParse). The "@name" form already carries
     * the whole locator, so this whole block is skipped for it. */
    if (out->manifestName[0] == '\0') {
        p = skip_ws(p);
        if (*p == '\0') {
            out->type = AMIP_AREXX_CMD_UNKNOWN;
            return -1;
        }
        if (ci_streq_prefix(p, "ROLE=") || ci_streq_prefix(p, "LABEL=") ||
            ci_streq_prefix(p, "INDEX=")) {
            out->gadgetLocatorMode = 1;
            for (;;) {
                int trunc;
                if (ci_streq_prefix(p, "ROLE=")) {
                    p += 5; /* strlen("ROLE=") */
                    p = read_token(p, out->roleName, sizeof(out->roleName), &trunc);
                    if (fail_if_trunc(trunc, out)) return -1;
                } else if (ci_streq_prefix(p, "LABEL=")) {
                    p += 6; /* strlen("LABEL=") */
                    p = read_token(p, out->labelSubstring, sizeof(out->labelSubstring), &trunc);
                    if (fail_if_trunc(trunc, out)) return -1;
                } else if (ci_streq_prefix(p, "INDEX=")) {
                    char numbuf[16];
                    p += 6; /* strlen("INDEX=") */
                    p = read_token(p, numbuf, sizeof(numbuf), &trunc);
                    if (fail_if_trunc(trunc, out)) return -1;
                    out->locatorIndex = strtol(numbuf, NULL, 10);
                } else {
                    break;
                }
                p = skip_ws(p);
            }
        } else {
            char idbuf[16];
            int trunc;
            p = read_token(p, idbuf, sizeof(idbuf), &trunc);
            if (fail_if_trunc(trunc, out)) return -1;
            out->gadgetId = strtol(idbuf, NULL, 10);
        }
    }

    if (out->type == AMIP_AREXX_CMD_TYPE) {
        p = skip_ws(p);
        if (*p == '\0') {
            out->type = AMIP_AREXX_CMD_UNKNOWN;
            return -1;
        }
        if (*p == '"') {
            int trunc;
            read_token(p, out->text, sizeof(out->text), &trunc);
            if (fail_if_trunc(trunc, out)) return -1;
        } else {
            /* Verbatim rest-of-line, not re-tokenized -- lets a plain
             * "TYPE GadTools 2 hello world" type the space without
             * needing to quote it. Rejected outright, not silently
             * chopped, same reasoning as LAUNCH's command line above
             * -- a truncated string is a different, unintended value
             * to type, not just a shorter one. */
            if (strlen(p) >= sizeof(out->text)) {
                out->argTooLong = 1;
                out->type = AMIP_AREXX_CMD_UNKNOWN;
                return -1;
            }
            strncpy(out->text, p, sizeof(out->text) - 1);
            out->text[sizeof(out->text) - 1] = '\0';
        }
    }

    return 0;
}
