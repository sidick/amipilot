/* manifest.h -- portable parser for the AmiPilot manifest contract
 * (manifest/SPEC.md, format version 1). Pure C, no Amiga types, no file
 * I/O of its own (the caller reads the file and hands over the text) --
 * same portable-core split as arexx_cmd.h, and for the same reason: the
 * host Python client's tests can cross-check against this parser's
 * behavior, and nothing here needs an Amiga to run. */
#ifndef AMIPILOT_MANIFEST_H
#define AMIPILOT_MANIFEST_H

/* The format version this parser speaks. A file declaring any other
 * MANIFEST version is rejected whole, per the spec -- never skimmed for
 * recognisable records. */
#define AMIP_MANIFEST_FORMAT_VERSION 1

/* Fixed caps: static allocation suits both the 68000 target and the
 * actual shape of real manifests (a handful of windows, dozens of
 * gadgets). Overflow is a parse error, not a silent truncation. */
#define AMIP_MANIFEST_MAX_WINDOWS   16
#define AMIP_MANIFEST_MAX_GADGETS   96
#define AMIP_MANIFEST_MAX_NAME      32   /* logical names */
#define AMIP_MANIFEST_MAX_TITLE     96   /* window title substrings */
#define AMIP_MANIFEST_MAX_APPNAME   32

typedef struct {
    char name[AMIP_MANIFEST_MAX_NAME];
    char titleSubstring[AMIP_MANIFEST_MAX_TITLE];
} AmipManifestWindow;

typedef struct {
    char name[AMIP_MANIFEST_MAX_NAME];
    int  windowIndex;   /* into windows[] */
    long gadgetId;
} AmipManifestGadget;

typedef struct {
    char appName[AMIP_MANIFEST_MAX_APPNAME];
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

/* Resolves a logical gadget name (case-insensitive, per the spec) to its
 * window title substring + GA_ID. Returns 0 and fills both out
 * parameters on success; -1 if the name isn't in the manifest. */
int AmipManifestResolve(const AmipManifest *manifest, const char *gadgetName,
                        const char **outTitleSubstring, long *outGadgetId);

#endif /* AMIPILOT_MANIFEST_H */
