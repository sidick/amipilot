/* manifest.h -- portable parser for the AmiPilot manifest contract
 * (manifest/SPEC.md, format versions 1 and 2). Pure C, no Amiga types,
 * no file I/O of its own (the caller reads the file and hands over the
 * text) -- same portable-core split as arexx_cmd.h, and for the same
 * reason: the host Python client's tests can cross-check against this
 * parser's behavior, and nothing here needs an Amiga to run. */
#ifndef AMIPILOT_MANIFEST_H
#define AMIPILOT_MANIFEST_H

/* Format versions this parser speaks. A file declaring any other
 * MANIFEST version is rejected whole, per the spec -- never skimmed for
 * recognisable records. Version 2 adds WHEREPORT/WHEREGADGET (the
 * cooperative geometry port, manifest/SPEC.md's "WHERE port" section);
 * a version-1 file may not use them. */
#define AMIP_MANIFEST_FORMAT_VERSION_MIN 1
#define AMIP_MANIFEST_FORMAT_VERSION_MAX 2

/* Fixed caps: static allocation suits both the 68000 target and the
 * actual shape of real manifests (a handful of windows, dozens of
 * gadgets). Overflow is a parse error, not a silent truncation. */
#define AMIP_MANIFEST_MAX_WINDOWS   16
#define AMIP_MANIFEST_MAX_GADGETS   96
#define AMIP_MANIFEST_MAX_NAME      32   /* logical names */
#define AMIP_MANIFEST_MAX_TITLE     96   /* window title substrings */
#define AMIP_MANIFEST_MAX_APPNAME   32
#define AMIP_MANIFEST_MAX_PORT      32   /* WHEREPORT ARexx port name */

typedef struct {
    char name[AMIP_MANIFEST_MAX_NAME];
    char titleSubstring[AMIP_MANIFEST_MAX_TITLE];
} AmipManifestWindow;

typedef struct {
    char name[AMIP_MANIFEST_MAX_NAME];
    int  windowIndex;   /* into windows[] */
    long gadgetId;       /* unused when viaWherePort is set */
    int  viaWherePort;   /* WHEREGADGET, not GADGET -- resolve geometry
                           * via wherePort at action time, not GA_ID */
} AmipManifestGadget;

typedef struct {
    char appName[AMIP_MANIFEST_MAX_APPNAME];
    int  declaredVersion;
    char wherePort[AMIP_MANIFEST_MAX_PORT];   /* '\0' if no WHEREPORT */
    int  windowCount;
    int  gadgetCount;
    AmipManifestWindow windows[AMIP_MANIFEST_MAX_WINDOWS];
    AmipManifestGadget gadgets[AMIP_MANIFEST_MAX_GADGETS];
} AmipManifest;

/* Parses manifest text (the whole file's contents, NUL-terminated) into
 * out. Returns 0 on success; on failure returns -1 and, if errBuf is
 * non-NULL, writes a one-line human-readable reason (including the line
 * number) into it -- a rejected manifest must say why, per the spec's
 * "reject with a clear error". out is zeroed first either way. */
int AmipManifestParse(const char *text, AmipManifest *out,
                      char *errBuf, int errBufCap);

/* Resolves a logical gadget name (case-insensitive, per the spec). For
 * a plain GADGET entry, fills outTitleSubstring/outGadgetId and clears
 * *outViaWherePort. For a WHEREGADGET entry, fills outTitleSubstring
 * and sets *outViaWherePort nonzero; outGadgetId is untouched (the
 * caller must consult manifest->wherePort instead). Any out pointer may
 * be NULL. Returns 0 on success; -1 if the name isn't in the manifest. */
int AmipManifestResolve(const AmipManifest *manifest, const char *gadgetName,
                        const char **outTitleSubstring, long *outGadgetId,
                        int *outViaWherePort);

#endif /* AMIPILOT_MANIFEST_H */
