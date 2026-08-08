/* screenshot.c -- see screenshot.h. */
#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <exec/memory.h>

#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/view.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include "action_engine.h"
#include "arexx_cmd.h"
#include "screenshot.h"

extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase *GfxBase;

static UBYTE *g_buf = NULL;
static ULONG g_bufCap = 0;
static char g_resultBuf[160];

static void SetErr(const char **resultOut, ULONG *outLen, const char *msg)
{
    strncpy(g_resultBuf, msg, sizeof(g_resultBuf) - 1);
    g_resultBuf[sizeof(g_resultBuf) - 1] = '\0';
    *resultOut = g_resultBuf;
    *outLen = (ULONG)strlen(g_resultBuf);
}

/* Grow-only scratch buffer, reused across calls -- never pre-reserved
 * (0 bytes until SCREENSHOT is first used), only ever grown to the
 * largest capture actually requested so far, never shrunk. Returns
 * NULL on allocation failure, leaving any existing buffer intact. */
static UBYTE *EnsureBuf(ULONG needed)
{
    UBYTE *grown;

    if (needed <= g_bufCap) {
        return g_buf;
    }
    grown = AllocVec(needed, MEMF_PUBLIC);
    if (grown == NULL) {
        return NULL;
    }
    if (g_buf != NULL) {
        FreeVec(g_buf);
    }
    g_buf = grown;
    g_bufCap = needed;
    return g_buf;
}

static void PutU16(UBYTE **p, UWORD v)
{
    (*p)[0] = (UBYTE)(v >> 8);
    (*p)[1] = (UBYTE)v;
    *p += 2;
}

int AmipScreenshotCapture(const char *screenSubstring, const char *windowPattern,
                           const char **resultOut, ULONG *outLen)
{
    struct Screen *screen;
    struct BitMap *bm;
    UWORD width, height, bytesPerRow, cropX = 0, cropY = 0, cropW = 0, cropH = 0;
    UBYTE depth;
    UWORD viewModes;
    struct ColorMap *cm;
    PLANEPTR planes[8];
    int numColors;
    ULONG planesSize, totalSize;
    UBYTE *buf, *p;
    int i;

    if (IntuitionBase == NULL) {
        SetErr(resultOut, outLen, "intuition.library unavailable");
        return AMIP_AREXX_RC_ERROR;
    }
    if (GfxBase == NULL) {
        SetErr(resultOut, outLen, "graphics.library unavailable");
        return AMIP_AREXX_RC_ERROR;
    }

    if (windowPattern != NULL && windowPattern[0] != '\0') {
        struct Window *w = AmipFindWindow((CONST_STRPTR)screenSubstring,
                                          (CONST_STRPTR)windowPattern);
        if (w == NULL) {
            SetErr(resultOut, outLen, "no window matched");
            return AMIP_AREXX_RC_WARN;
        }
        screen = w->WScreen;
        cropX = (UWORD)w->LeftEdge;
        cropY = (UWORD)w->TopEdge;
        cropW = (UWORD)w->Width;
        cropH = (UWORD)w->Height;
    } else {
        screen = AmipFindScreen((CONST_STRPTR)screenSubstring);
        if (screen == NULL) {
            SetErr(resultOut, outLen, "no screen matched");
            return AMIP_AREXX_RC_WARN;
        }
    }

    /* Brief LockIBase hold, same shape AmipIsWindowOpen/the SCREENS
     * verb already use -- copy out exactly what's needed (scalar
     * geometry, the ColorMap pointer, and each plane pointer) and
     * release before doing any real work (palette reads, the
     * possibly-large plane copy below), same "narrows but doesn't
     * eliminate the gap" precedent intuition-model's own walker
     * documents, rather than holding a system-wide Intuition lock for
     * a potentially large memcpy. */
    LockIBase(0);
    bm = screen->RastPort.BitMap;
    if (bm == NULL) {
        UnlockIBase(0);
        SetErr(resultOut, outLen, "screen has no bitmap");
        return AMIP_AREXX_RC_ERROR;
    }
    /* NOT gated on BMF_STANDARD: an earlier version of this code
     * rejected any bitmap without that flag set, on the assumption it
     * distinguished plain chip-mem planar bitmaps from RTG/P96 ones.
     * Live testing against a real, completely ordinary Copperline
     * Workbench screen (issue #41's own on-target check) proved that
     * assumption wrong -- BMF_STANDARD is documented under "Flags for
     * AllocBitMap()" and is only ever set on a bitmap YOU allocate
     * yourself requesting it; Intuition's own screen bitmaps never
     * set it, planar or not, so the check rejected the ordinary case
     * it was meant to allow. Removed rather than replaced with a
     * plausible-looking guess -- this project's own convention is
     * real, verified functions over guessed heuristics, and no
     * verified V37-safe way to positively identify an RTG/Picasso96
     * bitmap was found (see issue #44's own updated notes). Genuine
     * RTG/P96 screens are therefore an ACTUAL, unguarded risk today,
     * not a falsely-reassuring one -- #44 tracks building real
     * detection (likely via cybergraphics.library's own API, not
     * carried by this project's NDK) before this can be closed. */
    width = screen->Width;
    height = screen->Height;
    depth = bm->Depth;
    bytesPerRow = bm->BytesPerRow;
    viewModes = screen->ViewPort.Modes;
    cm = screen->ViewPort.ColorMap;
    if (depth > 8) {
        depth = 8; /* PLANEPTR array itself only has 8 slots */
    }
    for (i = 0; i < depth; i++) {
        planes[i] = bm->Planes[i];
    }
    UnlockIBase(0);

    numColors = 1 << depth;
    if (numColors > 256) {
        numColors = 256;
    }

    planesSize = (ULONG)depth * bytesPerRow * height;
    totalSize = 20 + (ULONG)numColors * 3 + planesSize;
    if (totalSize > AMIP_SCREENSHOT_MAX_BYTES) {
        SetErr(resultOut, outLen, "capture exceeds this server's size cap");
        return AMIP_AREXX_RC_ERROR;
    }

    buf = EnsureBuf(totalSize);
    if (buf == NULL) {
        SetErr(resultOut, outLen, "out of memory building the capture");
        return AMIP_AREXX_RC_FAIL;
    }

    p = buf;
    PutU16(&p, width);
    PutU16(&p, height);
    *p++ = depth;
    *p++ = 0; /* reserved */
    PutU16(&p, bytesPerRow);
    PutU16(&p, viewModes);
    PutU16(&p, (UWORD)numColors);
    PutU16(&p, cropX);
    PutU16(&p, cropY);
    PutU16(&p, cropW);
    PutU16(&p, cropH);

    /* GetRGB4() (V33+) -- "4 bits per gun right justified", 0x0RGB.
     * Not ColorMap->ColorTable directly (an opaque APTR in the real
     * struct, not a plain array -- see this file's header comment).
     * Expanded to 8-bit-per-channel via the standard *17 replication
     * (15*17 == 255) so 0xF maps to a full 0xFF, not 0xF0. */
    for (i = 0; i < numColors; i++) {
        LONG rgb4 = (LONG)GetRGB4(cm, i);
        UBYTE r, g, b;

        if (rgb4 < 0) {
            r = g = b = 0;
        } else {
            r = (UBYTE)(((rgb4 >> 8) & 0xF) * 17);
            g = (UBYTE)(((rgb4 >> 4) & 0xF) * 17);
            b = (UBYTE)((rgb4 & 0xF) * 17);
        }
        *p++ = r;
        *p++ = g;
        *p++ = b;
    }

    for (i = 0; i < depth; i++) {
        memcpy(p, planes[i], (size_t)bytesPerRow * height);
        p += (size_t)bytesPerRow * height;
    }

    *resultOut = (const char *)buf;
    *outLen = totalSize;
    return AMIP_AREXX_RC_OK;
}
