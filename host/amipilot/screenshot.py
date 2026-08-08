"""Parses AmiPilotServer's SCREENSHOT raw capture payload
(server/include/screenshot.h) and turns it into real image files.

The wire carries raw, uncompressed pixel bytes plus just enough
metadata to reconstruct an image -- no PNG/IFF/zlib encoding, and no
colour-space conversion, ever happens on the 68000 (server/include/
screenshot.h's own header comment explains why, including for the
Picasso96/RTG pixel-format variety below). All format work happens
here instead, where CPU is cheap -- the same "wire stays simple, host
does the rendering" split this project already uses for TREE/
`amipilot dump` (docs/implementation-plan.md's "Protocol and client",
the JSON-drop decision).

Two capture shapes (`Screenshot.pixel_format`), both handled
transparently by `to_ilbm()`/`to_png()`/`save()`:

- Classic PLANAR (`pixel_format == 0`, issue #41) -- a plain chip-mem
  bitmap's own bitplane bytes, `depth` planes.
- Picasso96/RTG CHUNKY (`pixel_format == 1`, issue #44) -- one buffer
  of `depth`-byte-wide pixels in P96's own native `rgb_format`
  (`amipilot.screenshot`'s own small RGBFTYPE table below), read only
  when a real P96 board is present AND the captured screen is
  genuinely P96-backed -- never assumed, never required (see
  `server/include/p96_compat.h`/`screenshot.h` for the server-side
  detection).

Two output formats, both from either capture shape, both stdlib-only
(no Pillow or other imaging dependency, matching `host/`'s own ethos):

- IFF ILBM (`.iff`) -- Amiga's own native raster format. For a planar
  capture this is a near-direct copy of the payload (BMHD/CMAP/CAMG/
  BODY chunks, uncompressed); for a P96 CLUT (palette) capture, a
  re-plane into a standard 8-bitplane/256-colour ILBM. **P96 truecolor/
  hicolor captures have no ILBM equivalent** (ILBM is fundamentally
  planar, and forcing 15-32 bits of true colour into a nonstandard
  "deep" ILBM most real viewers don't handle reliably isn't worth the
  false promise) -- `to_ilbm()` raises `ScreenshotParseError` for
  these; use `to_png()` or `to_rgb888()` instead.
- PNG (`.png`) -- indexed-colour (palette) for anything with a
  palette (planar or P96 CLUT), true-colour (24-bit RGB, no palette)
  for a P96 truecolor/hicolor capture -- either way, a straightforward
  hand-built PNG (`zlib.compress()` for IDAT).

A `window=` capture (`Amipilot.screenshot(window=...)`) always sends
the whole owning screen over the wire (there's no separate per-window
pixel buffer to grab on classic Intuition), but `to_ilbm()`/`to_png()`/
`save()` crop to just that window's rectangle BY DEFAULT (`crop=True`)
-- a real single-window screenshot, not the full screen with the
window merely noted in metadata. Pass `crop=False` to render the full
screen regardless.

Known simplification, not a bug: HAM/EHB PLANAR screens report only
their "real" ColorMap register count (16 for HAM6's 4 direct-select
bits, 32 for EHB) but this capture's own `num_colors` is always
`min(2**depth, 256)` (screenshot.h) -- entries beyond the screen's
real registers read back as black (GetRGB4() returns -1 for an unset
entry, screenshot.c). The CAMG chunk still correctly marks the image
as HAM/EHB for any viewer that wants to reconstruct the derived
colors properly; this module doesn't attempt that reconstruction
itself.
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

_HEADER_FMT = ">HHBBHHHHHHHHH"
_HEADER_LEN = struct.calcsize(_HEADER_FMT)  # 24 bytes -- see screenshot.h

# p96_compat.h's own RGBFTYPE subset (plain integer constants, the P96
# ABI itself -- see that file's own top comment for what's and isn't
# reproduced from the vendor SDK). Value 0 (P96's own RGBFB_NONE) never
# appears here; pixel_format == 0 (planar) is this module's own
# distinct sentinel for "not a P96 capture at all".
_P96_CLUT = 1
_P96_R8G8B8 = 2
_P96_B8G8R8 = 3
_P96_R5G6B5PC = 4
_P96_R5G5B5PC = 5
_P96_A8R8G8B8 = 6
_P96_A8B8G8R8 = 7
_P96_R8G8B8A8 = 8
_P96_B8G8R8A8 = 9
_P96_R5G6B5 = 10
_P96_R5G5B5 = 11
_P96_B5G6R5PC = 12
_P96_B5G5R5PC = 13

_P96_BYTES_PER_PIXEL = {
    _P96_CLUT: 1,
    _P96_R8G8B8: 3, _P96_B8G8R8: 3,
    _P96_R5G6B5PC: 2, _P96_R5G5B5PC: 2, _P96_R5G6B5: 2, _P96_R5G5B5: 2,
    _P96_B5G6R5PC: 2, _P96_B5G5R5PC: 2,
    _P96_A8R8G8B8: 4, _P96_A8B8G8R8: 4, _P96_R8G8B8A8: 4, _P96_B8G8R8A8: 4,
}


def _decode_p96_pixel(rgb_format: int, raw: bytes) -> tuple[int, int, int]:
    """Decodes one pixel's raw bytes into (r, g, b), 0-255 each, per
    Picasso96's own documented byte/bit layouts. PC-suffixed 16-bit
    formats are stored byte-swapped (little-endian) relative to their
    own bit-layout description -- confirmed by comparing each PC
    format's documented byte layout against its non-PC counterpart's
    (e.g. R5G5B5PC's "gggbbbbb0rrrrrgg" is exactly R5G5B5's
    "0rrrrrgggggbbbbb" with its two bytes swapped) -- never assume
    16-bit P96 pixels are uniformly one endianness (a real, documented
    P96 API pitfall, not a guess)."""
    if rgb_format == _P96_R8G8B8:
        return raw[0], raw[1], raw[2]
    if rgb_format == _P96_B8G8R8:
        return raw[2], raw[1], raw[0]
    if rgb_format == _P96_A8R8G8B8:
        return raw[1], raw[2], raw[3]
    if rgb_format == _P96_A8B8G8R8:
        return raw[3], raw[2], raw[1]
    if rgb_format == _P96_R8G8B8A8:
        return raw[0], raw[1], raw[2]
    if rgb_format == _P96_B8G8R8A8:
        return raw[2], raw[1], raw[0]
    if rgb_format in (_P96_R5G6B5, _P96_R5G6B5PC):
        v = struct.unpack("<H" if rgb_format == _P96_R5G6B5PC else ">H", raw)[0]
        r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        return r * 255 // 31, g * 255 // 63, b * 255 // 31
    if rgb_format in (_P96_R5G5B5, _P96_R5G5B5PC):
        v = struct.unpack("<H" if rgb_format == _P96_R5G5B5PC else ">H", raw)[0]
        r, g, b = (v >> 10) & 0x1F, (v >> 5) & 0x1F, v & 0x1F
        return r * 255 // 31, g * 255 // 31, b * 255 // 31
    if rgb_format == _P96_B5G6R5PC:
        v = struct.unpack("<H", raw)[0]
        b, g, r = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        return r * 255 // 31, g * 255 // 63, b * 255 // 31
    if rgb_format == _P96_B5G5R5PC:
        v = struct.unpack("<H", raw)[0]
        b, g, r = (v >> 10) & 0x1F, (v >> 5) & 0x1F, v & 0x1F
        return r * 255 // 31, g * 255 // 31, b * 255 // 31
    raise ScreenshotParseError(
        f"unsupported P96 rgb_format {rgb_format} (YUV formats are not "
        "supported -- see screenshot.h's own top comment)"
    )


class ScreenshotParseError(ValueError):
    """The raw capture payload was too short/malformed, or a requested
    conversion isn't supported for this capture's own pixel format
    (e.g. to_ilbm() on a P96 truecolor capture)."""


@dataclass
class Screenshot:
    """One SCREENSHOT capture, already parsed into its component
    fields -- see server/include/screenshot.h for the exact wire
    layout this mirrors."""

    width: int
    height: int
    pixel_format: int  # 0 = planar (issue #41), 1 = P96 chunky (issue #44)
    depth: int  # pixel_format 0: bitplane count; pixel_format 1: bytes/pixel
    bytes_per_row: int
    view_modes: int  # ViewPort->Modes -- IFF ILBM's CAMG value, verbatim; 0 for P96
    rgb_format: int  # P96 RGBFTYPE value; 0 (unused) for planar
    palette: list[tuple[int, int, int]]  # (r, g, b), 0-255 each; [] if none
    planes: list[bytes]  # pixel_format 0 only: `depth` entries
    pixel_data: bytes  # pixel_format 1 only: one bytes_per_row * height buffer
    crop: tuple[int, int, int, int] | None  # (x, y, w, h) of a WINDOW=, or None

    @classmethod
    def parse(cls, data: bytes) -> "Screenshot":
        if len(data) < _HEADER_LEN:
            raise ScreenshotParseError(
                f"capture too short ({len(data)} bytes, header alone needs {_HEADER_LEN})"
            )
        (width, height, pixel_format, depth, bytes_per_row, view_modes,
         num_colors, crop_x, crop_y, crop_w, crop_h, rgb_format,
         _reserved) = struct.unpack(_HEADER_FMT, data[:_HEADER_LEN])

        offset = _HEADER_LEN
        palette_end = offset + num_colors * 3
        if palette_end > len(data):
            raise ScreenshotParseError("capture truncated in its palette")
        palette = [
            (data[i], data[i + 1], data[i + 2])
            for i in range(offset, palette_end, 3)
        ]

        planes: list[bytes] = []
        pixel_data = b""
        offset = palette_end
        if pixel_format == 0:
            plane_size = bytes_per_row * height
            for _ in range(depth):
                end = offset + plane_size
                if end > len(data):
                    raise ScreenshotParseError("capture truncated in its plane data")
                planes.append(data[offset:end])
                offset = end
        else:
            end = offset + bytes_per_row * height
            if end > len(data):
                raise ScreenshotParseError("capture truncated in its pixel data")
            pixel_data = data[offset:end]

        crop = (crop_x, crop_y, crop_w, crop_h) if (crop_w and crop_h) else None
        return cls(width, height, pixel_format, depth, bytes_per_row, view_modes,
                    rgb_format, palette, planes, pixel_data, crop)

    def to_chunky(self) -> bytes:
        """De-planes (or de-pads) the FULL SCREEN into one packed-
        byte-per-pixel, row-major buffer of PALETTE INDICES -- works
        for a planar capture (any depth) or a P96 CLUT capture (already
        one byte/pixel, just row-padding to strip). Raises
        ScreenshotParseError for a P96 truecolor/hicolor capture, which
        has no palette to index into -- use to_rgb888() instead.
        Always the whole capture regardless of `self.crop`; see
        `_region()` for the window-cropping to_ilbm()/to_png() do by
        default."""
        if self.pixel_format == 1:
            if self.rgb_format != _P96_CLUT:
                raise ScreenshotParseError(
                    "to_chunky() needs a palette (planar or P96 CLUT capture); "
                    "this is a P96 truecolor/hicolor capture -- use to_rgb888()"
                )
            out = bytearray(self.width * self.height)
            for y in range(self.height):
                src = y * self.bytes_per_row
                dst = y * self.width
                out[dst:dst + self.width] = self.pixel_data[src:src + self.width]
            return bytes(out)

        out = bytearray(self.width * self.height)
        for y in range(self.height):
            row_base = y * self.width
            plane_row = y * self.bytes_per_row
            for x in range(self.width):
                byte_index = plane_row + (x >> 3)
                bit = 0x80 >> (x & 7)
                pixel = 0
                for plane_no in range(self.depth):
                    if self.planes[plane_no][byte_index] & bit:
                        pixel |= 1 << plane_no
                out[row_base + x] = pixel
        return bytes(out)

    def to_rgb888(self) -> bytes:
        """De-planes/decodes the FULL SCREEN into one packed 3-bytes-
        per-pixel (R,G,B), row-major buffer -- the universal
        representation every capture shape supports, unlike
        to_chunky() (palette-indexed captures only). A palette-backed
        capture (planar or P96 CLUT) is `to_chunky()` plus a palette
        lookup; a P96 truecolor/hicolor capture is decoded pixel-by-
        pixel via `rgb_format`'s own byte layout (this module's own
        `_decode_p96_pixel()`)."""
        if self.pixel_format == 0 or self.rgb_format == _P96_CLUT:
            indices = self.to_chunky()
            palette = self.palette
            out = bytearray(len(indices) * 3)
            for i, idx in enumerate(indices):
                out[i * 3:i * 3 + 3] = bytes(palette[idx])
            return bytes(out)

        bpp = self.depth
        out = bytearray(self.width * self.height * 3)
        oi = 0
        for y in range(self.height):
            row = y * self.bytes_per_row
            for x in range(self.width):
                off = row + x * bpp
                out[oi:oi + 3] = bytes(_decode_p96_pixel(self.rgb_format,
                                                          self.pixel_data[off:off + bpp]))
                oi += 3
        return bytes(out)

    def _region(self, crop: bool) -> tuple[int, int, int, int]:
        """(x, y, w, h) to actually render: `self.crop` if `crop` is
        True and a crop rect is present (a `window=` capture), else
        the full screen -- the single place to_ilbm()/to_png() decide
        this, so both stay consistent."""
        if crop and self.crop is not None:
            return self.crop
        return (0, 0, self.width, self.height)

    def _region_chunky(self, x: int, y: int, w: int, h: int) -> bytes:
        """The full-screen palette-index buffer, sliced down to one
        rectangle -- byte-per-pixel, so this is a plain row slice, no
        bit-level work needed (unlike planar data, see
        _chunky_to_planes() below for the ILBM path)."""
        if (x, y, w, h) == (0, 0, self.width, self.height):
            return self.to_chunky()
        full = self.to_chunky()
        out = bytearray(w * h)
        for row in range(h):
            src = (y + row) * self.width + x
            out[row * w:(row + 1) * w] = full[src:src + w]
        return bytes(out)

    def _region_rgb888(self, x: int, y: int, w: int, h: int) -> bytes:
        """Same idea as _region_chunky() but for the 3-bytes-per-pixel
        to_rgb888() buffer -- the P96 truecolor/hicolor PNG path."""
        if (x, y, w, h) == (0, 0, self.width, self.height):
            return self.to_rgb888()
        full = self.to_rgb888()
        out = bytearray(w * h * 3)
        for row in range(h):
            src = ((y + row) * self.width + x) * 3
            out[row * w * 3:(row + 1) * w * 3] = full[src:src + w * 3]
        return bytes(out)

    def _chunky_to_planes(self, chunky: bytes, width: int, height: int, nplanes: int) -> list[bytes]:
        """Re-planes a chunky (one-byte-per-pixel, palette-index)
        buffer into `nplanes` bitplanes -- needed for a CROPPED
        to_ilbm() (the crop rectangle's x/w aren't generally byte-
        aligned, so planes can't just be bit-sliced out of the
        originals) and for any P96 CLUT capture at all (P96 has no
        bitplane representation to start from, only this module's own
        to_chunky()). `nplanes` is `self.depth` for a planar capture,
        but always 8 for P96 CLUT (`self.depth` there is bytes-per-
        pixel, 1, not a bitplane count -- 8 planes is the standard,
        always-sufficient shape for a 256-colour ILBM)."""
        bytes_per_row = (width + 7) // 8
        planes = [bytearray(bytes_per_row * height) for _ in range(nplanes)]
        for row in range(height):
            row_base = row * width
            plane_row_base = row * bytes_per_row
            for col in range(width):
                pixel = chunky[row_base + col]
                byte_index = plane_row_base + (col >> 3)
                bit = 0x80 >> (col & 7)
                for plane_no in range(nplanes):
                    if pixel & (1 << plane_no):
                        planes[plane_no][byte_index] |= bit
        return [bytes(p) for p in planes]

    def to_ilbm(self, *, crop: bool = True) -> bytes:
        """A real `FORM ILBM` -- BMHD/CMAP/CAMG/BODY, uncompressed
        (BMHD's own compression field is 0). For a planar capture this
        is a near-direct copy of the payload; for a P96 CLUT capture,
        a re-plane into a standard 8-bitplane/256-colour ILBM. Raises
        ScreenshotParseError for a P96 truecolor/hicolor capture --
        ILBM is fundamentally planar, and there's no standard, widely-
        viewable way to carry 15-32 bits of true colour in one; use
        to_png() or to_rgb888() instead.

        `crop` (default True): if this Screenshot came from a
        `window=` capture (`self.crop` set), render JUST that
        window's rectangle -- a real single-window screenshot, not
        the whole screen with the window merely noted in metadata.
        Pass `crop=False` for the full screen regardless."""
        if self.pixel_format == 1 and self.rgb_format != _P96_CLUT:
            raise ScreenshotParseError(
                "to_ilbm() has no way to represent a P96 truecolor/hicolor "
                "capture (ILBM is fundamentally planar) -- use to_png() or to_rgb888()"
            )

        x, y, w, h = self._region(crop)
        if self.pixel_format == 0:
            nplanes = self.depth
            if (x, y, w, h) == (0, 0, self.width, self.height):
                planes = self.planes
            else:
                planes = self._chunky_to_planes(self._region_chunky(x, y, w, h), w, h, nplanes)
        else:
            nplanes = 8
            planes = self._chunky_to_planes(self._region_chunky(x, y, w, h), w, h, nplanes)

        def chunk(cid: bytes, payload: bytes) -> bytes:
            padded = payload + (b"\x00" if len(payload) % 2 else b"")
            return cid + struct.pack(">I", len(payload)) + padded

        bmhd = struct.pack(
            ">HHhhBBBBHBBhh",
            w, h, 0, 0,                            # w, h, x, y
            nplanes, 0, 0, 0,                     # nPlanes, masking, compression, pad1
            0,                                     # transparentColor
            10, 11,                                # xAspect, yAspect (square-ish default)
            w, h,                                  # pageWidth, pageHeight
        )
        cmap = b"".join(bytes(c) for c in self.palette)
        camg = struct.pack(">I", self.view_modes)
        body = b"".join(planes)

        form_body = (
            chunk(b"BMHD", bmhd)
            + chunk(b"CMAP", cmap)
            + chunk(b"CAMG", camg)
            + chunk(b"BODY", body)
        )
        return b"FORM" + struct.pack(">I", len(form_body) + 4) + b"ILBM" + form_body

    def to_png(self, *, crop: bool = True) -> bytes:
        """A real PNG -- indexed-colour (palette) for anything with a
        palette (planar or P96 CLUT), true-colour (24-bit RGB, no
        palette) for a P96 truecolor/hicolor capture. Bit depth 8
        unconditionally either way. zlib-compressed IDAT either way.

        `crop` (default True): see to_ilbm()'s own doc -- renders just
        a `window=` capture's own rectangle by default."""
        x, y, w, h = self._region(crop)
        indexed = self.pixel_format == 0 or self.rgb_format == _P96_CLUT

        def chunk(cid: bytes, payload: bytes) -> bytes:
            return (
                struct.pack(">I", len(payload))
                + cid
                + payload
                + struct.pack(">I", zlib.crc32(cid + payload) & 0xFFFFFFFF)
            )

        if indexed:
            ihdr = struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)  # color type 3: palette
            plte = b"".join(bytes(c) for c in self.palette)
            pixels = self._region_chunky(x, y, w, h)
            bytes_per_pixel = 1
            extra_chunks = [chunk(b"PLTE", plte)]
        else:
            ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)  # color type 2: truecolor
            pixels = self._region_rgb888(x, y, w, h)
            bytes_per_pixel = 3
            extra_chunks = []

        row_bytes = w * bytes_per_pixel
        raw = bytearray()
        for row in range(h):
            raw.append(0)  # per-scanline filter type 0 (None)
            raw.extend(pixels[row * row_bytes:(row + 1) * row_bytes])
        idat = zlib.compress(bytes(raw), 9)

        return (
            b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", ihdr)
            + b"".join(extra_chunks)
            + chunk(b"IDAT", idat)
            + chunk(b"IEND", b"")
        )

    def save(self, base_path: str, *, crop: bool = True) -> tuple[str, str]:
        """Writes both `<base_path>.iff` (IFF ILBM) and
        `<base_path>.png`, returning `(ilbm_path, png_path)`. `crop`
        (default True) is forwarded to to_ilbm()/to_png() -- a
        `window=` capture is saved as just that window by default.
        Raises ScreenshotParseError (from to_ilbm()) for a P96
        truecolor/hicolor capture -- write `to_png()` directly instead
        for those."""
        ilbm_path = f"{base_path}.iff"
        png_path = f"{base_path}.png"
        with open(ilbm_path, "wb") as f:
            f.write(self.to_ilbm(crop=crop))
        with open(png_path, "wb") as f:
            f.write(self.to_png(crop=crop))
        return ilbm_path, png_path
