/* screenshot.h -- raw planar bitmap capture (phase 1.0, GitHub issue
 * #41: "SCREENSHOT verb: raw pixel capture, host-side PNG encoding").
 *
 * Same "wire stays simple, host does the rendering" split this project
 * already uses for TREE/`amipilot dump` (docs/implementation-plan.md's
 * "Protocol and client", the JSON-drop decision): this captures a
 * screen's RAW, UNCOMPRESSED bitplane bytes plus just enough metadata
 * to reconstruct a real image -- no PNG/zlib/IFF encoding happens on
 * the 68000 at all. `host/amipilot/screenshot.py` turns the raw
 * capture into both a real IFF ILBM (a near-direct copy of what's
 * already here -- Amiga's own native format, viewable with
 * Multiview/any Amiga paint program, no host dependency needed at
 * all) and a PNG (de-plane to chunky RGB + `zlib.compress()`, cheap
 * host-side CPU this project's stdlib-only ethos for `host/` already
 * accepts).
 *
 * PLANAR ONLY -- by design intent, but with a REAL, UNRESOLVED gap:
 * RTG/Picasso96/CGX screens are still backed by a `struct BitMap`, but
 * their `Planes[]`/`BytesPerRow`/`Depth` fields are NOT real chip-mem
 * bitplanes -- P96 uses its own opaque, driver-private representation
 * there. Walking `Planes[]` on one wouldn't just give wrong colors, it
 * risks reading garbage pointers -- the same risk class as the
 * `WBPattern`/`GTYP_CUSTOMGADGET` hang this project already found and
 * fixed (issue #36). An earlier version of this module tried to guard
 * against this via `BitMap->Flags & BMF_STANDARD`; live testing
 * against a real, completely ordinary Copperline Workbench screen
 * (issue #41's own on-target check) proved that check WRONG, not just
 * imperfect -- `BMF_STANDARD` is documented under "Flags for
 * AllocBitMap()" and is only ever set on a bitmap explicitly allocated
 * requesting it; Intuition's own screen bitmaps never set it, planar
 * or not, so the check rejected the ordinary case it was meant to
 * allow. It was removed rather than replaced with another plausible-
 * looking guess (this project's own convention: real, verified
 * functions over guessed heuristics -- see `screenshot.c`'s own
 * comment at the removal site). **There is currently no guard against
 * a genuine RTG/P96 screen at all** -- targeting one is a real,
 * unverified risk, not a falsely-reassuring "checked and rejected"
 * one. Issue #44 tracks building real detection, most likely via
 * cybergraphics.library's own API (`IsCyberModeID()`/
 * `GetCyberMapAttr()`), a whole separate third-party SDK this
 * project's NDK doesn't carry -- the same kind of honest scope line
 * MUIREXX's own doc comment already draws elsewhere.
 *
 * Palette precision: read via `GetRGB4()` (V33+, "4 bits per gun right
 * justified") rather than touching `ColorMap->ColorTable` directly
 * (an opaque `APTR` in the real struct, not the plain array it might
 * look like -- exactly the kind of guessed-heuristic access this
 * project's conventions forbid) or `GetRGB32()` (V39+, above this
 * project's V37 floor). This means captured colour is genuinely
 * 4-bit-per-gun (12-bit RGB, expanded to 8-bit-per-channel for the
 * wire/PNG/ILBM by the *17 replication trick), matching real OCS/ECS
 * hardware precision at this project's floor -- not a corner cut, an
 * honest reflection of what V37-safe code can read. A real AGA 8-bit-
 * per-gun capture would need GetRGB32(), not attempted here.
 */
#ifndef AMIPILOT_SCREENSHOT_H
#define AMIPILOT_SCREENSHOT_H

#include <exec/types.h>

/* Hard cap on a single capture's total response size (header + palette
 * + plane data). 512KB comfortably covers everything from a plain
 * 320x256x5 (32-colour) screen (~51KB) up to a full AGA 640x512x8
 * (256-colour) interlaced screen (~327KB) -- generous for this
 * project's floor (~1MB RAM total) without reserving that much
 * memory permanently (the capture buffer, EnsureScratchBuf() in
 * screenshot.c, is allocated on first use and only grown, never
 * pre-reserved). A capture whose real size would exceed this is
 * rejected outright (AMIP_AREXX_RC_ERROR) before any capture is
 * attempted, not silently truncated. */
#define AMIP_SCREENSHOT_MAX_BYTES (512UL * 1024UL)

/* Captures a screen's raw bitplane bytes into this module's own
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
 * Python's `struct` module using '>'):
 *
 *   offset  0: UWORD width          -- screen width, pixels
 *   offset  2: UWORD height         -- screen height, pixels (rows)
 *   offset  4: UBYTE depth          -- number of bitplanes
 *   offset  5: UBYTE reserved       -- 0, reserved for future flags
 *   offset  6: UWORD bytesPerRow    -- per-plane row stride (may
 *                                      exceed ceil(width/8); alignment)
 *   offset  8: UWORD viewModes      -- ViewPort->Modes (HAM/EHB/LACE/
 *                                      HIRES bits) -- IFF ILBM's CAMG
 *                                      chunk value, verbatim
 *   offset 10: UWORD numColors      -- palette entries following,
 *                                      min(2^depth, 256)
 *   offset 12: UWORD cropX
 *   offset 14: UWORD cropY
 *   offset 16: UWORD cropW          -- cropW/cropH both 0 means "no
 *                                      window was given -- this is
 *                                      already a full-screen capture,
 *                                      nothing to crop"
 *   offset 18: UWORD cropH
 *   offset 20: numColors * 3 bytes  -- R,G,B, 8-bit-per-channel
 *                                      (expanded from the real 4-bit-
 *                                      per-gun precision, see this
 *                                      header's own top comment)
 *   then: depth * bytesPerRow * height bytes -- plane 0 first, each
 *         plane's rows in top-to-bottom order, matching IFF ILBM's
 *         own BODY chunk shape almost exactly (ByteRun1 compression,
 *         if the host chooses to apply it writing the .ilbm file, is
 *         the ONLY difference -- this wire payload itself is always
 *         uncompressed, per this module's own top comment).
 *
 * Returns AMIP_AREXX_RC_OK on success, AMIP_AREXX_RC_WARN if nothing
 * matched the screen/window locator, AMIP_AREXX_RC_ERROR for a screen
 * with no bitmap at all or a capture too large for AMIP_SCREENSHOT_
 * MAX_BYTES, AMIP_AREXX_RC_FAIL on allocation failure. `*resultOut`
 * gets a one-line reason on any non-OK return. NOTE: there is
 * currently no rejection of a genuine RTG/Picasso96 screen -- see
 * this header's own top comment for why, and issue #44. */
int AmipScreenshotCapture(const char *screenSubstring, const char *windowPattern,
                           const char **resultOut, ULONG *outLen);

#endif /* AMIPILOT_SCREENSHOT_H */
