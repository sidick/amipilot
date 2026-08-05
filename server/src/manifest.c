/* manifest.c -- see manifest.h. Portable C, no Amiga types. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "manifest.h"

/* Same portable helpers as arexx_cmd.c's own (separate copies on
 * purpose -- each portable-core module stands alone). */
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

/* One whitespace-separated field, double-quoted form allowed (no
 * embedded-quote escaping), per SPEC.md. Stops at end of line. */
static const char *read_field(const char *p, char *dst, int cap)
{
    int n = 0;
    if (*p == '"') {
        p++;
        while (*p && *p != '"' && *p != '\n' && *p != '\r') {
            if (n + 1 < cap) dst[n++] = *p;
            p++;
        }
        if (*p == '"') p++;
    } else {
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            if (n + 1 < cap) dst[n++] = *p;
            p++;
        }
    }
    dst[n] = '\0';
    return p;
}

static int valid_logical_name(const char *s)
{
    if (*s == '\0') return 0;
    for (; *s; s++) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_')) {
            return 0;
        }
    }
    return 1;
}

static void fail(char *errBuf, int errBufCap, int line, const char *msg)
{
    if (errBuf != NULL && errBufCap > 0) {
        snprintf(errBuf, errBufCap, "line %d: %s", line, msg);
    }
}

static int find_window(const AmipManifest *m, const char *name)
{
    int i;
    for (i = 0; i < m->windowCount; i++) {
        if (ci_streq(m->windows[i].name, name)) return i;
    }
    return -1;
}

static int find_gadget(const AmipManifest *m, const char *name)
{
    int i;
    for (i = 0; i < m->gadgetCount; i++) {
        if (ci_streq(m->gadgets[i].name, name)) return i;
    }
    return -1;
}

int AmipManifestParse(const char *text, AmipManifest *out,
                      char *errBuf, int errBufCap)
{
    const char *p;
    int line = 0;
    int sawVersion = 0, sawApp = 0;

    if (out == NULL) return -1;
    memset(out, 0, sizeof(*out));
    if (errBuf != NULL && errBufCap > 0) errBuf[0] = '\0';
    if (text == NULL) {
        fail(errBuf, errBufCap, 0, "no manifest text");
        return -1;
    }

    p = text;
    while (*p != '\0') {
        char kw[16];
        line++;

        p = skip_ws(p);
        if (*p == '\0') break;
        if (*p == '\n' || *p == '\r' || *p == ';' || *p == '#') {
            /* blank or comment -- skip to next line */
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }

        p = read_field(p, kw, sizeof(kw));

        if (ci_streq(kw, "MANIFEST")) {
            char ver[8];
            p = skip_ws(p);
            p = read_field(p, ver, sizeof(ver));
            if (sawVersion) {
                fail(errBuf, errBufCap, line, "duplicate MANIFEST record");
                return -1;
            }
            if (atoi(ver) != AMIP_MANIFEST_FORMAT_VERSION) {
                fail(errBuf, errBufCap, line, "unsupported manifest format version");
                return -1;
            }
            sawVersion = 1;
        } else if (!sawVersion) {
            /* Spec: MANIFEST must be the first record; anything else
             * before it (including an unknown keyword) is a hard reject,
             * never a skim. */
            fail(errBuf, errBufCap, line, "first record must be MANIFEST <version>");
            return -1;
        } else if (ci_streq(kw, "APP")) {
            if (sawApp) {
                fail(errBuf, errBufCap, line, "duplicate APP record");
                return -1;
            }
            p = skip_ws(p);
            p = read_field(p, out->appName, sizeof(out->appName));
            if (out->appName[0] == '\0') {
                fail(errBuf, errBufCap, line, "APP needs a name");
                return -1;
            }
            sawApp = 1;
        } else if (ci_streq(kw, "WINDOW")) {
            AmipManifestWindow *w;
            if (out->windowCount >= AMIP_MANIFEST_MAX_WINDOWS) {
                fail(errBuf, errBufCap, line, "too many WINDOW records");
                return -1;
            }
            w = &out->windows[out->windowCount];
            p = skip_ws(p);
            p = read_field(p, w->name, sizeof(w->name));
            p = skip_ws(p);
            p = read_field(p, w->titleSubstring, sizeof(w->titleSubstring));
            if (!valid_logical_name(w->name)) {
                fail(errBuf, errBufCap, line, "WINDOW logical name must be [a-z0-9_]+");
                return -1;
            }
            if (w->titleSubstring[0] == '\0') {
                fail(errBuf, errBufCap, line, "WINDOW needs a title substring");
                return -1;
            }
            if (find_window(out, w->name) >= 0) {
                fail(errBuf, errBufCap, line, "duplicate WINDOW logical name");
                return -1;
            }
            out->windowCount++;
        } else if (ci_streq(kw, "GADGET")) {
            AmipManifestGadget *g;
            char winName[AMIP_MANIFEST_MAX_NAME];
            char idField[16];
            int winIndex;
            if (out->gadgetCount >= AMIP_MANIFEST_MAX_GADGETS) {
                fail(errBuf, errBufCap, line, "too many GADGET records");
                return -1;
            }
            g = &out->gadgets[out->gadgetCount];
            p = skip_ws(p);
            p = read_field(p, g->name, sizeof(g->name));
            p = skip_ws(p);
            p = read_field(p, winName, sizeof(winName));
            p = skip_ws(p);
            p = read_field(p, idField, sizeof(idField));
            if (!valid_logical_name(g->name)) {
                fail(errBuf, errBufCap, line, "GADGET logical name must be [a-z0-9_]+");
                return -1;
            }
            winIndex = find_window(out, winName);
            if (winIndex < 0) {
                fail(errBuf, errBufCap, line, "GADGET names a WINDOW not declared above it");
                return -1;
            }
            if (idField[0] == '\0') {
                fail(errBuf, errBufCap, line, "GADGET needs a GA_ID");
                return -1;
            }
            if (find_gadget(out, g->name) >= 0) {
                fail(errBuf, errBufCap, line, "duplicate GADGET logical name");
                return -1;
            }
            g->windowIndex = winIndex;
            g->gadgetId = strtol(idField, NULL, 10);
            out->gadgetCount++;
        } else {
            /* Unknown record type: version-1 consumers must reject, not
             * skip -- see SPEC.md's versioning policy. */
            fail(errBuf, errBufCap, line, "unknown record type");
            return -1;
        }

        /* consume the rest of the line (trailing junk is tolerated only
         * as whitespace; anything else is an error) */
        p = skip_ws(p);
        if (*p != '\0' && *p != '\n' && *p != '\r' && *p != ';' && *p != '#') {
            fail(errBuf, errBufCap, line, "unexpected trailing text");
            return -1;
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    if (!sawVersion) {
        fail(errBuf, errBufCap, line, "empty manifest (no MANIFEST record)");
        return -1;
    }
    if (!sawApp) {
        fail(errBuf, errBufCap, line, "missing APP record");
        return -1;
    }
    if (out->windowCount == 0) {
        fail(errBuf, errBufCap, line, "no WINDOW records");
        return -1;
    }

    return 0;
}

int AmipManifestResolve(const AmipManifest *manifest, const char *gadgetName,
                        const char **outTitleSubstring, long *outGadgetId)
{
    int gi;

    if (manifest == NULL || gadgetName == NULL) return -1;
    gi = find_gadget(manifest, gadgetName);
    if (gi < 0) return -1;

    if (outTitleSubstring != NULL) {
        *outTitleSubstring = manifest->windows[manifest->gadgets[gi].windowIndex].titleSubstring;
    }
    if (outGadgetId != NULL) {
        *outGadgetId = manifest->gadgets[gi].gadgetId;
    }
    return 0;
}
