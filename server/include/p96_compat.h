/* p96_compat.h -- minimal Picasso96API.library interface for
 * SCREENSHOT's optional RTG/Picasso96 capture path (issue #44).
 *
 * This is NOT a copy of Picasso96Develop's own headers (Picasso96.h
 * etc, (C) Alexander Kneer & Tobias Abt, "All Rights Reserved" --
 * not redistributable here). It declares only the handful of
 * functions/constants screenshot.c actually calls, independently
 * written from the library's PUBLISHED, factual interface data every
 * client needs to interoperate at all -- register/LVO assignments
 * from Picasso96API_lib.fd's own table (bias 30; GetBitMapAttr/
 * LockBitMap/UnlockBitMap are respectively the 5th/6th/7th entries,
 * giving LVOs 0x2a/0x30/0x36), cross-checked against the SDK's own
 * generated inline/Picasso96.h for the identical offsets. LVO
 * numbers and register assignments are the ABI itself -- a
 * necessary fact for any independent client, not the vendor's
 * creative expression, the same category of information every
 * `proto/foo.h` for a third-party library legitimately reproduces.
 * SDK referenced: https://wiki.icomp.de/w/images/6/62/P96Develop.lha
 *
 * Uses the toolchain's own generic `<inline/macros.h>` (the same
 * LP2/LP3/LP2NR register-call macros every OTHER library in this
 * project's includes already goes through via its own proto header)
 * -- not P96-specific, ordinary NDK machinery.
 */
#ifndef AMIPILOT_P96_COMPAT_H
#define AMIPILOT_P96_COMPAT_H

#include <exec/types.h>
#include <inline/macros.h>

#ifndef PICASSO96API_BASE_NAME
#define PICASSO96API_BASE_NAME P96Base
#endif

extern struct Library *P96Base;

/* RGBFTYPE values this module actually distinguishes (Picasso96.h's
 * own enum -- plain integer constants, not copyrightable expression).
 * P96_RGBFB_NONE (0) is P96's own "planar" sentinel, never produced
 * by a real chunky capture; reused here as this module's own wire
 * header's "not a P96 capture" value. YUV formats (hardware-window-
 * only per the SDK's own docs, "bitmap operations may be implemented
 * incompletely") are deliberately not included -- see screenshot.h's
 * own top comment for the honest scope line. */
enum {
    AMIP_P96_RGBFB_NONE      = 0,
    AMIP_P96_RGBFB_CLUT      = 1,
    AMIP_P96_RGBFB_R8G8B8    = 2,
    AMIP_P96_RGBFB_B8G8R8    = 3,
    AMIP_P96_RGBFB_R5G6B5PC  = 4,
    AMIP_P96_RGBFB_R5G5B5PC  = 5,
    AMIP_P96_RGBFB_A8R8G8B8  = 6,
    AMIP_P96_RGBFB_A8B8G8R8  = 7,
    AMIP_P96_RGBFB_R8G8B8A8  = 8,
    AMIP_P96_RGBFB_B8G8R8A8  = 9,
    AMIP_P96_RGBFB_R5G6B5    = 10,
    AMIP_P96_RGBFB_R5G5B5    = 11,
    AMIP_P96_RGBFB_B5G6R5PC  = 12,
    AMIP_P96_RGBFB_B5G5R5PC  = 13
};

/* p96GetBitMapAttr()'s attribute_number values actually used here --
 * WIDTH/HEIGHT/BYTESPERPIXEL/RGBFORMAT/ISP96 are all safe to query
 * WITHOUT locking the bitmap first (Picasso96API.doc's own
 * "--bitmaps--" background section and p96GetBitMapAttr's own
 * INPUTS list are explicit that only BYTESPERROW/MEMORY/ISONBOARD/
 * board-address attributes require a prior p96LockBitMap() --
 * screenshot.c gets those instead via the RenderInfo buffer
 * p96LockBitMap() itself fills in, one call covering all three). */
enum {
    AMIP_P96BMA_WIDTH         = 0,
    AMIP_P96BMA_HEIGHT        = 1,
    AMIP_P96BMA_DEPTH         = 2,
    AMIP_P96BMA_MEMORY        = 3,
    AMIP_P96BMA_BYTESPERROW   = 4,
    AMIP_P96BMA_BYTESPERPIXEL = 5,
    AMIP_P96BMA_BITSPERPIXEL  = 6,
    AMIP_P96BMA_RGBFORMAT     = 7,
    AMIP_P96BMA_ISP96         = 8
};

/* Filled in by p96LockBitMap() -- Memory/BytesPerRow/RGBFormat of the
 * locked bitmap, valid only between a matching LockBitMap/
 * UnlockBitMap pair (the framebuffer may otherwise move). Field
 * order/types match struct RenderInfo's real, documented shape
 * (Picasso96API.doc's own "Structure to describe graphics data"
 * section) -- Memory (APTR), BytesPerRow (WORD), pad (WORD, unused),
 * RGBFormat (an RGBFTYPE, ULONG-sized here as this file declares
 * its own enum backing type). */
struct AmipP96RenderInfo {
    APTR  Memory;
    WORD  BytesPerRow;
    WORD  pad;
    ULONG RGBFormat;
};

#define AmipP96GetBitMapAttr(BitMap_, Attribute_) \
    LP2(0x2a, ULONG, p96GetBitMapAttr, struct BitMap *, BitMap_, a0, ULONG, Attribute_, d0, \
    , PICASSO96API_BASE_NAME)

/* Returns a lock handle (opaque, "do not try to interpret it in any
 * way" per the SDK's own docs), or 0 on failure. MUST be matched by
 * AmipP96UnlockBitMap() -- the docs are explicit that skipping this
 * blocks the whole system indefinitely. Never hold longer than
 * about a second (screen switching is disabled system-wide for as
 * long as this lock is held). */
#define AmipP96LockBitMap(BitMap_, Buffer_, Size_) \
    LP3(0x30, LONG, p96LockBitMap, struct BitMap *, BitMap_, a0, UBYTE *, Buffer_, a1, ULONG, Size_, d0, \
    , PICASSO96API_BASE_NAME)

#define AmipP96UnlockBitMap(BitMap_, Lock_) \
    LP2NR(0x36, p96UnlockBitMap, struct BitMap *, BitMap_, a0, LONG, Lock_, d0, \
    , PICASSO96API_BASE_NAME)

#endif /* AMIPILOT_P96_COMPAT_H */
