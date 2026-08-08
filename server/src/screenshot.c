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
#include "p96_compat.h"
#include "screenshot.h"

extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase *GfxBase;
extern struct Library *P96Base; /* optional -- see p96_compat.h/screenshot.h */

#define AMIP_SCREENSHOT_HEADER_SIZE 24
#define AMIP_SCREENSHOT_FMT_PLANAR 0
#define AMIP_SCREENSHOT_FMT_P96CHUNKY 1

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

/* Writes this module's shared 24-byte header (screenshot.h's own
 * layout doc has the full field-by-field story) -- both capture paths
 * below fill the same shape, just with different pixelFormat/depth/
 * rgbFormat/viewModes meanings. Returns the write position immediately
 * after the header (where the palette, if any, starts). */
static UBYTE *WriteHeader(UBYTE *buf, UWORD width, UWORD height,
                          UBYTE pixelFormat, UBYTE depth, UWORD bytesPerRow,
                          UWORD viewModes, UWORD numColors,
                          UWORD cropX, UWORD cropY, UWORD cropW, UWORD cropH,
                          UWORD rgbFormat)
{
    UBYTE *p = buf;

    PutU16(&p, width);
    PutU16(&p, height);
    *p++ = pixelFormat;
    *p++ = depth;
    PutU16(&p, bytesPerRow);
    PutU16(&p, viewModes);
    PutU16(&p, numColors);
    PutU16(&p, cropX);
    PutU16(&p, cropY);
    PutU16(&p, cropW);
    PutU16(&p, cropH);
    PutU16(&p, rgbFormat);
    PutU16(&p, 0); /* reserved */
    return p;
}

/* GetRGB4() (V33+) -- "4 bits per gun right justified", 0x0RGB. Not
 * ColorMap->ColorTable directly (an opaque APTR in the real struct,
 * not a plain array -- see screenshot.h's own top comment). Expanded
 * to 8-bit-per-channel via the standard *17 replication (15*17==255)
 * so 0xF maps to a full 0xFF, not 0xF0. Shared by the planar path and
 * a P96 CLUT capture -- both have a real ColorMap to read this way. */
static UBYTE *WritePalette(UBYTE *p, struct ColorMap *cm, int numColors)
{
    int i;

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
    return p;
}

/* Classic planar capture (issue #41) -- unchanged from before P96
 * support (issue #44) landed except for being split out into its own
 * function. `planes`/`depth`/`bytesPerRow` were already copied out
 * under a brief LockIBase hold by the caller (same "narrows but
 * doesn't eliminate the gap" precedent intuition-model's own walker
 * documents, rather than holding a system-wide Intuition lock for a
 * potentially large memcpy). */
static int CapturePlanar(PLANEPTR *planes, UBYTE depth, UWORD width, UWORD height,
                          UWORD bytesPerRow, UWORD viewModes, struct ColorMap *cm,
                          UWORD cropX, UWORD cropY, UWORD cropW, UWORD cropH,
                          const char **resultOut, ULONG *outLen)
{
    int numColors;
    ULONG totalSize;
    UBYTE *buf, *p;
    int i;

    numColors = 1 << depth;
    if (numColors > 256) {
        numColors = 256;
    }

    totalSize = AMIP_SCREENSHOT_HEADER_SIZE + (ULONG)numColors * 3
        + (ULONG)depth * bytesPerRow * height;
    if (totalSize > AMIP_SCREENSHOT_MAX_BYTES) {
        SetErr(resultOut, outLen, "capture exceeds this server's size cap");
        return AMIP_AREXX_RC_ERROR;
    }

    buf = EnsureBuf(totalSize);
    if (buf == NULL) {
        SetErr(resultOut, outLen, "out of memory building the capture");
        return AMIP_AREXX_RC_FAIL;
    }

    p = WriteHeader(buf, width, height, AMIP_SCREENSHOT_FMT_PLANAR, depth,
                    bytesPerRow, viewModes, (UWORD)numColors,
                    cropX, cropY, cropW, cropH, 0);
    p = WritePalette(p, cm, numColors);
    for (i = 0; i < depth; i++) {
        memcpy(p, planes[i], (size_t)bytesPerRow * height);
        p += (size_t)bytesPerRow * height;
    }

    *resultOut = (const char *)buf;
    *outLen = totalSize;
    return AMIP_AREXX_RC_OK;
}

/* Genuine Picasso96/RTG capture (issue #44) -- only reached when
 * P96Base is open AND AMIP_P96BMA_ISP96 confirmed `bm` is actually a
 * P96 bitmap (screenshot.h's own top comment has the full story on
 * why that check, not BMF_STANDARD, is the real one). `bm` itself
 * (not its Planes[]/BytesPerRow -- those are P96-opaque, per the
 * SDK's own docs) is the only thing the caller hands in; everything
 * else is read fresh here via p96GetBitMapAttr()/p96LockBitMap(),
 * matching the SDK's own explicit warning never to assume prior
 * geometry/memory-location info stays valid. */
static int CaptureP96Chunky(struct BitMap *bm, UWORD cropX, UWORD cropY,
                             UWORD cropW, UWORD cropH,
                             struct ColorMap *cm,
                             const char **resultOut, ULONG *outLen)
{
    UWORD width, height, bytesPerRow, rgbFormat, numColors;
    UBYTE bytesPerPixel;
    struct AmipP96RenderInfo ri;
    LONG lock;
    ULONG totalSize;
    UBYTE *buf, *p;

    width = (UWORD)AmipP96GetBitMapAttr(bm, AMIP_P96BMA_WIDTH);
    height = (UWORD)AmipP96GetBitMapAttr(bm, AMIP_P96BMA_HEIGHT);
    bytesPerPixel = (UBYTE)AmipP96GetBitMapAttr(bm, AMIP_P96BMA_BYTESPERPIXEL);
    rgbFormat = (UWORD)AmipP96GetBitMapAttr(bm, AMIP_P96BMA_RGBFORMAT);

    /* p96_compat.h's own enum only covers CLUT + the RGB truecolor/
     * hicolor formats (values 1-13, mirroring the real RGBFTYPE's own
     * ordering) -- anything outside that range is a YUV format (14+
     * in the real enum) or the RGBFB_NONE planar sentinel (0, which
     * shouldn't occur for a bitmap AMIP_P96BMA_ISP96 already
     * confirmed is genuinely P96's), both rejected the same way. */
    if (rgbFormat < AMIP_P96_RGBFB_CLUT || rgbFormat > AMIP_P96_RGBFB_B5G5R5PC) {
        SetErr(resultOut, outLen,
               "P96 screen uses an unsupported pixel format (YUV -- hardware-window-only per the P96 SDK's own docs)");
        return AMIP_AREXX_RC_ERROR;
    }

    numColors = 0;
    if (rgbFormat == AMIP_P96_RGBFB_CLUT) {
        numColors = 256; /* CLUT is always 8bpp chunky -- 256 possible entries */
    }

    /* p96LockBitMap() (SDK docs: "Never hold the lock for longer than
     * about one second... all screen switching is disabled") --
     * filled RenderInfo gives Memory/BytesPerRow/RGBFormat in one
     * call, exactly the "make life easier" convenience the SDK's own
     * docs describe, rather than separate locked GetBitMapAttr()
     * calls for each. */
    lock = AmipP96LockBitMap(bm, (UBYTE *)&ri, sizeof(ri));
    if (lock == 0) {
        SetErr(resultOut, outLen, "p96LockBitMap failed");
        return AMIP_AREXX_RC_FAIL;
    }
    bytesPerRow = (UWORD)ri.BytesPerRow;

    totalSize = AMIP_SCREENSHOT_HEADER_SIZE + (ULONG)numColors * 3
        + (ULONG)bytesPerRow * height;
    if (totalSize > AMIP_SCREENSHOT_MAX_BYTES) {
        AmipP96UnlockBitMap(bm, lock);
        SetErr(resultOut, outLen, "capture exceeds this server's size cap");
        return AMIP_AREXX_RC_ERROR;
    }
    buf = EnsureBuf(totalSize);
    if (buf == NULL) {
        AmipP96UnlockBitMap(bm, lock);
        SetErr(resultOut, outLen, "out of memory building the capture");
        return AMIP_AREXX_RC_FAIL;
    }

    p = WriteHeader(buf, width, height, AMIP_SCREENSHOT_FMT_P96CHUNKY,
                    bytesPerPixel, bytesPerRow, 0, numColors,
                    cropX, cropY, cropW, cropH, rgbFormat);
    if (numColors > 0) {
        p = WritePalette(p, cm, numColors);
    }
    /* The actual memcpy happens INSIDE the p96 lock -- exactly what
     * it exists to protect ("use this function only to protect direct
     * accesses to the BitMap memory"). Kept as short as a straight
     * memcpy of already-known size can be. */
    memcpy(p, ri.Memory, (size_t)bytesPerRow * height);
    AmipP96UnlockBitMap(bm, lock);

    *resultOut = (const char *)buf;
    *outLen = totalSize;
    return AMIP_AREXX_RC_OK;
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
    BOOL isP96 = FALSE;
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
     * geometry, the ColorMap pointer, the P96-or-not verdict, and
     * each plane pointer for the planar case) and release before
     * doing any real work (palette reads, the possibly-large pixel
     * copy below), same "narrows but doesn't eliminate the gap"
     * precedent intuition-model's own walker documents, rather than
     * holding a system-wide Intuition lock for a potentially large
     * memcpy -- P96's own p96LockBitMap() below is a SEPARATE lock
     * for a separate concern (the framebuffer moving), acquired only
     * after this one is released, not nested with it. */
    LockIBase(0);
    bm = screen->RastPort.BitMap;
    if (bm == NULL) {
        UnlockIBase(0);
        SetErr(resultOut, outLen, "screen has no bitmap");
        return AMIP_AREXX_RC_ERROR;
    }
    /* AMIP_P96BMA_ISP96 -- see screenshot.h's own top comment for why
     * this, not BitMap->Flags & BMF_STANDARD (removed, was actively
     * wrong), is the real, documented-safe-to-call-unlocked way to
     * tell a genuine P96 bitmap apart from a classic planar one.
     * P96Base == NULL (the common case on this project's own tested
     * floor, no P96 installed) always takes the planar path -- P96
     * support is optional and never required. */
    if (P96Base != NULL) {
        isP96 = (BOOL)AmipP96GetBitMapAttr(bm, AMIP_P96BMA_ISP96);
    }
    width = screen->Width;
    height = screen->Height;
    depth = bm->Depth;
    bytesPerRow = bm->BytesPerRow;
    viewModes = screen->ViewPort.Modes;
    cm = screen->ViewPort.ColorMap;
    if (!isP96) {
        if (depth > 8) {
            depth = 8; /* PLANEPTR array itself only has 8 slots */
        }
        for (i = 0; i < depth; i++) {
            planes[i] = bm->Planes[i];
        }
    }
    UnlockIBase(0);

    if (isP96) {
        return CaptureP96Chunky(bm, cropX, cropY, cropW, cropH, cm, resultOut, outLen);
    }
    return CapturePlanar(planes, depth, width, height, bytesPerRow, viewModes, cm,
                          cropX, cropY, cropW, cropH, resultOut, outLen);
}
