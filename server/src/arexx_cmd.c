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

    if      (ci_streq(kw, "TREE"))    out->type = AMIP_AREXX_CMD_TREE;
    else if (ci_streq(kw, "CLICK"))   out->type = AMIP_AREXX_CMD_CLICK;
    else if (ci_streq(kw, "TYPE"))    out->type = AMIP_AREXX_CMD_TYPE;
    else if (ci_streq(kw, "GETTEXT")) out->type = AMIP_AREXX_CMD_GETTEXT;
    else if (ci_streq(kw, "QUIT"))    out->type = AMIP_AREXX_CMD_QUIT;
    else { out->type = AMIP_AREXX_CMD_UNKNOWN; return -1; }

    if (out->type == AMIP_AREXX_CMD_QUIT) {
        return 0;
    }

    /* Every other command starts with a window-pattern argument. */
    p = skip_ws(p);
    if (*p == '\0') {
        out->type = AMIP_AREXX_CMD_UNKNOWN;
        return -1;
    }
    p = read_token(p, out->windowPattern, sizeof(out->windowPattern));

    if (out->type == AMIP_AREXX_CMD_TREE) {
        return 0;
    }

    /* CLICK/TYPE/GETTEXT all take a gadget ID next. */
    {
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
