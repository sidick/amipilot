/* screenshot.h -- raw bitmap capture, planar or Picasso96/RTG (phase
 * 1.0, GitHub issues #41 "SCREENSHOT verb: raw pixel capture, host-
 * side PNG encoding" and #44 "SCREENSHOT: support Picasso96/CGX (RTG)
 * screens").
 *
 * Same "wire stays simple, host does the rendering" split this project
 * already uses for TREE/`amipilot dump` (docs/implementation-plan.md's
 * "Protocol and client", the JSON-drop decision): this captures a
 * screen's RAW, UNCOMPRESSED pixel bytes plus just enough metadata to
 * reconstruct a real image -- no PNG/zlib/IFF encoding, and no colour-
 * space conversion, happens on the 68000 at all, even for the P96
 * pixel-format variety (see below). `host/amipilot/screenshot.py`
 * turns the raw capture into both a real IFF ILBM and a PNG.
 *
 * PLANAR by default, P96 WHEN GENUINELY PRESENT AND IN USE -- optional,
 * never required. A classic chip-mem planar screen (this project's own
 * V37 floor, OCS/ECS/AGA) is captured exactly as before (issue #41):
 * `struct BitMap`'s own `Planes[]`/`BytesPerRow`/`Depth` fields, walked
 * directly. A REAL Picasso96/CGX (RTG) screen's `BitMap` looks the same
 * shape but its `Planes[]` are NOT real chip-mem bitplanes -- P96 uses
 * its own opaque, driver-private representation there, and walking
 * `Planes[]` on one risks reading garbage pointers, the same risk class
 * as the `WBPattern`/`GTYP_CUSTOMGADGET` hang this project already
 * found and fixed (issue #36). An earlier version of this module tried
 * to guard against exactly that via `BitMap->Flags & BMF_STANDARD`;
 * live testing against a real, completely ordinary Copperline
 * Workbench screen (issue #41's own on-target check) proved that check
 * WRONG, not just imperfect -- that flag is only ever set on a bitmap
 * explicitly allocated requesting it, and Intuition's own screen
 * bitmaps never set it, planar or not. It was removed rather than
 * replaced with another guess.
 *
 * The REAL distinguishing test, verified against Picasso96API.library's
 * own published autodocs and .fd interface table (SDK:
 * https://wiki.icomp.de/w/images/6/62/P96Develop.lha, see
 * `p96_compat.h`'s own top comment for exactly what was verified and
 * why this project doesn't redistribute the SDK's own header files):
 * `p96GetBitMapAttr(bm, AMIP_P96BMA_ISP96)` -- the attribute exists
 * specifically to answer "is this actually one of mine," documented as
 * safe to call on any `struct BitMap *` without locking it first. This
 * module opens `Picasso96API.library` OPTIONALLY at server startup
 * (`server/src/amipilotserver/main.c`, same graceful-degradation
 * pattern as `GadToolsBase`/`KeymapBase`/`GfxBase`) -- if it's not
 * present at all, or the target bitmap isn't a genuine P96 one, the
 * existing planar path runs completely unchanged, exactly as before
 * issue #44. Only when BOTH the library is open AND the specific
 * target screen is genuinely P96-backed does the chunky capture path
 * below engage.
 *
 * P96 pixel data is read via `p96LockBitMap()` (required by the SDK's
 * own docs before touching bitmap memory or its true `BytesPerRow` --
 * "Picasso96 could move the bitmap's image data away while you are
 * reading... you're likely to cause illegal memory accesses") into a
 * `RenderInfo` buffer giving `Memory`/`BytesPerRow`/`RGBFormat`
 * directly, held only for the duration of the raw memcpy (the SDK's
 * own docs: "Never hold the lock for longer than about one second...
 * all screen switching is disabled"), then `p96UnlockBitMap()`
 * immediately. Whatever native pixel format P96 reports (CLUT/8-bit
 * palette, or one of the RGB truecolor/hicolor byte orders --
 * `p96_compat.h`'s own enum) is sent RAW, verbatim, over the wire --
 * this module does not convert or interpret P96's pixel format at all,
 * matching the same "no work on the 68000" principle the planar path
 * already follows; `host/amipilot/screenshot.py` decodes the format
 * host-side. YUV formats are NOT supported -- the SDK's own docs mark
 * them "for use with a hardware window only (bitmap operations may be
 * implemented incompletely)", so excluding them isn't scope-trimming,
 * it's honoring the SDK's own stated limit.
 *
 * Palette precision for the CLASSIC PLANAR path: read via `GetRGB4()`
 * (V33+, "4 bits per gun right justified") rather than touching
 * `ColorMap->ColorTable` directly (an opaque `APTR` in the real
 * struct, not the plain array it might look like) or `GetRGB32()`
 * (V39+, above this project's V37 floor). Captured colour is
 * genuinely 4-bit-per-gun (12-bit RGB, expanded to 8-bit-per-channel
 * by the *17 replication trick), matching real OCS/ECS hardware
 * precision at this project's floor. A P96 CLUT screen's palette is
 * read the SAME way (it has a real `ColorMap` too) -- P96 truecolor/
 * hicolor screens carry no palette at all (`numColors` is 0).
 */
#ifndef AMIPILOT_SCREENSHOT_H
#define AMIPILOT_SCREENSHOT_H

#include <exec/types.h>

/* Hard cap on a single capture's total response size (header + palette
 * + pixel data). 512KB comfortably covers everything from a plain
 * 320x256x5 (32-colour) planar screen (~51KB) up to a full AGA
 * 640x512x8 (256-colour) interlaced screen (~327KB), or a modest P96
 * truecolor mode (e.g. 320x240x16bpp is ~154KB) -- generous for this
 * project's floor (~1MB RAM total) without reserving that much memory
 * permanently (the capture buffer, EnsureBuf() in screenshot.c, is
 * allocated on first use and only grown, never pre-reserved). A
 * capture whose real size would exceed this is rejected outright
 * (AMIP_AREXX_RC_ERROR) before any capture is attempted, not silently
 * truncated -- a large P96 truecolor desktop can easily exceed this,
 * an honest, documented limit, not a bug. */
#define AMIP_SCREENSHOT_MAX_BYTES (512UL * 1024UL)

/* Captures a screen's raw pixel bytes into this module's own
 * (grow-only, reused across calls) scratch buffer and points
 * `*resultOut`/`*outLen` at it -- same binary-payload convention
 * AmipFsGet() already uses, framed by the wire's ordinary
 * "RC <code> <byte-count>\n<payload>" response, no new mechanism
 * needed (unlike FSPUT's request-side addition).
 *
 * `screenSubstring` (may be NULL/"" for "no filter") selects a screen
 * by DefaultTitle substring, same convention as every other SCREEN=
 * verb here; with no match AMIP_AREXX_RC_WARN. With no `windowPattern`
 * either, the frontmost/default public screen is captured instead.
 *
 * `windowPattern` (may be NULL/"" for "not given"), if given, is
 * resolved via AmipFindWindow(screenSubstring, windowPattern) exactly
 * like CLICK/TREE's own classic form; AMIP_AREXX_RC_WARN if nothing
 * matches. There is no separate per-window pixel buffer to capture on
 * classic Intuition (overlapping windows all share one screen
 * bitmap), so this always captures the window's OWNING SCREEN in
 * full, with that window's rectangle recorded in the response header
 * (`cropX`/`cropY`/`cropW`/`cropH`) for the client to crop -- exactly
 * what a real screenshot utility does on any windowing system.
 *
 * Response payload layout (all multi-byte fields big-endian, native
 * 68k byte order -- no conversion needed on-Amiga, host unpacks with
 * Python's `struct` module using '>'). 24-byte header, EXPERIMENTAL
 * (server/WIRE.md's "Handshake" -- this shape changed once already,
 * growing 4 bytes to carry pixelFormat/rgbFormat when P96 support
 * landed, issue #44):
 *
 *   offset  0: UWORD width          -- pixels
 *   offset  2: UWORD height         -- pixels (rows)
 *   offset  4: UBYTE pixelFormat    -- 0 = planar (classic capture,
 *                                      issue #41), 1 = P96 chunky
 *                                      (issue #44)
 *   offset  5: UBYTE depth          -- pixelFormat 0: number of
 *                                      bitplanes; pixelFormat 1: bytes
 *                                      per pixel (1/2/3/4)
 *   offset  6: UWORD bytesPerRow    -- row stride; pixelFormat 0: per-
 *                                      PLANE stride (may exceed
 *                                      ceil(width/8), alignment);
 *                                      pixelFormat 1: per-ROW stride
 *                                      of the single chunky buffer
 *                                      (may exceed width * depth,
 *                                      board alignment)
 *   offset  8: UWORD viewModes      -- ViewPort->Modes (HAM/EHB/LACE/
 *                                      HIRES bits) -- IFF ILBM's CAMG
 *                                      chunk value, verbatim.
 *                                      pixelFormat 1: always 0 (P96
 *                                      view modes aren't classic
 *                                      Amiga CAMG bits)
 *   offset 10: UWORD numColors      -- palette entries following;
 *                                      pixelFormat 0: min(2^depth,
 *                                      256); pixelFormat 1: same, but
 *                                      only for CLUT (rgbFormat 1) --
 *                                      0 for any truecolor/hicolor
 *                                      rgbFormat, which carries no
 *                                      palette at all
 *   offset 12: UWORD cropX
 *   offset 14: UWORD cropY
 *   offset 16: UWORD cropW          -- cropW/cropH both 0 means "no
 *                                      window was given -- this is
 *                                      already a full-screen capture,
 *                                      nothing to crop"
 *   offset 18: UWORD cropH
 *   offset 20: UWORD rgbFormat      -- pixelFormat 1 only: the P96
 *                                      RGBFTYPE value verbatim
 *                                      (p96_compat.h's own enum --
 *                                      1=CLUT, 2=R8G8B8, etc; host
 *                                      decodes by this value). 0 for
 *                                      pixelFormat 0 (unused).
 *   offset 22: UWORD reserved       -- 0, keeps the header an even 24
 *                                      bytes
 *   offset 24: numColors * 3 bytes  -- R,G,B, 8-bit-per-channel (see
 *                                      this header's own top comment
 *                                      on palette precision)
 *   then, pixelFormat 0: depth * bytesPerRow * height bytes -- plane 0
 *         first, each plane's rows top-to-bottom, matching IFF ILBM's
 *         own BODY chunk shape almost exactly (ByteRun1 compression,
 *         if the host applies it writing the .ilbm file, is the ONLY
 *         difference -- this wire payload itself is always
 *         uncompressed);
 *   or,   pixelFormat 1: bytesPerRow * height bytes -- one chunky
 *         buffer, rows top-to-bottom, `depth`-byte pixels in
 *         `rgbFormat`'s own native byte order, verbatim off the
 *         board -- no interpretation attempted on-Amiga.
 *
 * Returns AMIP_AREXX_RC_OK on success, AMIP_AREXX_RC_WARN if nothing
 * matched the screen/window locator, AMIP_AREXX_RC_ERROR for a screen
 * with no bitmap at all, an unsupported P96 pixel format (YUV -- see
 * this header's own top comment), or a capture too large for
 * AMIP_SCREENSHOT_MAX_BYTES, AMIP_AREXX_RC_FAIL on allocation failure
 * or a failed p96LockBitMap(). `*resultOut` gets a one-line reason on
 * any non-OK return. */
int AmipScreenshotCapture(const char *screenSubstring, const char *windowPattern,
                           const char **resultOut, ULONG *outLen);

#endif /* AMIPILOT_SCREENSHOT_H */
