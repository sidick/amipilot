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
 * to the closing '"' (no embedded-quote escaping -- not needed for this
 * command set's simple templates); otherwise reads up to the next
 * whitespace. Copies into dst (cap bytes, NUL-terminated, silently
 * truncates if needed) and returns a pointer just past the token. */
static const char *read_token(const char *p, char *dst, size_t cap)
{
    size_t n = 0;
    if (*p == '"') {
        p++;
        while (*p && *p != '"') {
            if (n + 1 < cap) dst[n++] = *p;
            p++;
        }
        if (*p == '"') p++;
    } else {
        while (*p && *p != ' ' && *p != '\t') {
            if (n + 1 < cap) dst[n++] = *p;
            p++;
        }
    }
    dst[n] = '\0';
    return p;
}

/* Consumes an optional "SCREEN=<value>" token at *pp -- same
 * KEYWORD=value idiom LAUNCH's own "STACK=<n>" already uses, but
 * <value> is a token (quotable the same way a window pattern is,
 * via read_token above) rather than a fixed-digit number. Writes
 * <value> into dst (cap bytes) and advances *pp past it plus any
 * following whitespace if the prefix is present; leaves *pp and dst
 * untouched otherwise (dst already reads as "" from AmipArexxParse's
 * own memset). */
static void parse_optional_screen_prefix(const char **pp, char *dst, size_t cap)
{
    const char *p = *pp;

    if (ci_streq_prefix(p, "SCREEN=")) {
        p += 7; /* strlen("SCREEN=") */
        p = read_token(p, dst, cap);
        *pp = skip_ws(p);
    }
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
    p = read_token(p, kw, sizeof(kw));

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
        p = skip_ws(p);
        if (*p == '\0') {
            out->type = AMIP_AREXX_CMD_UNKNOWN;
            return -1;
        }
        read_token(p, out->path, sizeof(out->path));
        return 0;
    }

    if (out->type == AMIP_AREXX_CMD_MENUPICK) {
        char numbuf[16];

        p = skip_ws(p);
        parse_optional_screen_prefix(&p, out->screenPattern, sizeof(out->screenPattern));
        if (*p == '\0') {
            out->type = AMIP_AREXX_CMD_UNKNOWN;
            return -1;
        }
        p = read_token(p, out->windowPattern, sizeof(out->windowPattern));

        p = skip_ws(p);
        if (*p == '\0') {
            out->type = AMIP_AREXX_CMD_UNKNOWN;
            return -1;
        }
        p = read_token(p, numbuf, sizeof(numbuf));
        out->menuNum = strtol(numbuf, NULL, 10);

        p = skip_ws(p);
        if (*p == '\0') {
            out->type = AMIP_AREXX_CMD_UNKNOWN;
            return -1;
        }
        p = read_token(p, numbuf, sizeof(numbuf));
        out->itemNum = strtol(numbuf, NULL, 10);

        out->subNum = -1;
        p = skip_ws(p);
        if (*p != '\0') {
            read_token(p, numbuf, sizeof(numbuf));
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
         * re-tokenized by this parser. */
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
    p = skip_ws(p);
    parse_optional_screen_prefix(&p, out->screenPattern, sizeof(out->screenPattern));
    if (*p == '\0') {
        out->type = AMIP_AREXX_CMD_UNKNOWN;
        return -1;
    }
    if (*p == '@' && out->type != AMIP_AREXX_CMD_TREE && out->type != AMIP_AREXX_CMD_MENU) {
        p++;
        p = read_token(p, out->manifestName, sizeof(out->manifestName));
        if (out->manifestName[0] == '\0') {
            out->type = AMIP_AREXX_CMD_UNKNOWN;
            return -1;
        }
    } else {
        p = read_token(p, out->windowPattern, sizeof(out->windowPattern));
    }

    if (out->type == AMIP_AREXX_CMD_TREE || out->type == AMIP_AREXX_CMD_MENU) {
        return 0;
    }

    /* Classic form: CLICK/TYPE/GETTEXT take a gadget ID next. The
     * "@name" form already carries the whole locator. */
    if (out->manifestName[0] == '\0') {
        char idbuf[16];
        p = skip_ws(p);
        if (*p == '\0') {
            out->type = AMIP_AREXX_CMD_UNKNOWN;
            return -1;
        }
        p = read_token(p, idbuf, sizeof(idbuf));
        out->gadgetId = strtol(idbuf, NULL, 10);
    }

    if (out->type == AMIP_AREXX_CMD_TYPE) {
        p = skip_ws(p);
        if (*p == '\0') {
            out->type = AMIP_AREXX_CMD_UNKNOWN;
            return -1;
        }
        if (*p == '"') {
            read_token(p, out->text, sizeof(out->text));
        } else {
            /* Verbatim rest-of-line, not re-tokenized -- lets a plain
             * "TYPE GadTools 2 hello world" type the space without
             * needing to quote it. */
            strncpy(out->text, p, sizeof(out->text) - 1);
            out->text[sizeof(out->text) - 1] = '\0';
        }
    }

    return 0;
}
