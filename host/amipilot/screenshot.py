"""Parses AmiPilotServer's SCREENSHOT raw capture payload
(server/include/screenshot.h) and turns it into real image files.

The wire carries raw, uncompressed planar bitplane bytes plus just
enough metadata to reconstruct an image -- no PNG/IFF/zlib encoding
ever happens on the 68000 (server/include/screenshot.h's own header
comment explains why). All format work happens here instead, where
CPU is cheap -- the same "wire stays simple, host does the rendering"
split this project already uses for TREE/`amipilot dump`
(docs/implementation-plan.md's "Protocol and client", the JSON-drop
decision).

Two output formats, both from this one capture, both stdlib-only (no
Pillow or other imaging dependency, matching `host/`'s own ethos):

- IFF ILBM (`.iff`) -- Amiga's own native raster format. A near-direct
  copy of the already-planar payload (BMHD/CMAP/CAMG/BODY chunks,
  uncompressed) -- viewable with Multiview or any Amiga paint program,
  and preserves the exact view mode (HAM/EHB/LACE/HIRES, via the CAMG
  chunk) the capture came from.
- PNG (`.png`) -- de-planed into chunky, palette-indexed pixels
  first (`to_chunky()`), then a straightforward hand-built indexed-
  color PNG (`zlib.compress()` for IDAT) -- easier for modern tooling
  (browsers, CI artifact viewers, image diffing) to work with than
  ILBM, at the cost of the host doing real per-pixel unpacking work
  ILBM's near-direct copy doesn't need.

A `window=` capture (`Amipilot.screenshot(window=...)`) always sends
the whole owning screen over the wire (there's no separate per-window
pixel buffer to grab on classic Intuition), but `to_ilbm()`/`to_png()`/
`save()` crop to just that window's rectangle BY DEFAULT (`crop=True`)
-- a real single-window screenshot, not the full screen with the
window merely noted in metadata. Pass `crop=False` to render the full
screen regardless. `to_chunky()` itself is always the full screen
(the cropping happens in `to_ilbm()`/`to_png()`, which each go through
their own region helper) -- if you need cropped chunky pixels
directly, slice `to_chunky()` yourself using `.crop`, or read
`to_png()`'s own decoded pixels back.

Known simplification, not a bug: HAM/EHB screens report only their
"real" ColorMap register count (16 for HAM6's 4 direct-select bits,
32 for EHB) but this capture's own `numColors` is always
`min(2**depth, 256)` (screenshot.h) -- entries beyond the screen's
real registers read back as black (GetRGB4() returns -1 for an
unset entry, screenshot.c). The CAMG chunk still correctly marks the
image as HAM/EHB for any viewer that wants to reconstruct the derived
colors properly; this module doesn't attempt that reconstruction
itself.
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

_HEADER_FMT = ">HHBBHHHHHHH"
_HEADER_LEN = struct.calcsize(_HEADER_FMT)  # 20 bytes -- see screenshot.h


class ScreenshotParseError(ValueError):
    """The raw capture payload was too short or otherwise malformed."""


@dataclass
class Screenshot:
    """One SCREENSHOT capture, already parsed into its component
    fields -- see server/include/screenshot.h for the exact wire
    layout this mirrors."""

    width: int
    height: int
    depth: int
    bytes_per_row: int
    view_modes: int  # ViewPort->Modes -- IFF ILBM's CAMG value, verbatim
    palette: list[tuple[int, int, int]]  # (r, g, b), 0-255 each
    planes: list[bytes]  # `depth` entries, each bytes_per_row * height bytes
    crop: tuple[int, int, int, int] | None  # (x, y, w, h) of a WINDOW=, or None

    @classmethod
    def parse(cls, data: bytes) -> "Screenshot":
        if len(data) < _HEADER_LEN:
            raise ScreenshotParseError(
                f"capture too short ({len(data)} bytes, header alone needs {_HEADER_LEN})"
            )
        (width, height, depth, _reserved, bytes_per_row, view_modes,
         num_colors, crop_x, crop_y, crop_w, crop_h) = struct.unpack(
            _HEADER_FMT, data[:_HEADER_LEN]
        )

        offset = _HEADER_LEN
        palette_end = offset + num_colors * 3
        if palette_end > len(data):
            raise ScreenshotParseError("capture truncated in its palette")
        palette = [
            (data[i], data[i + 1], data[i + 2])
            for i in range(offset, palette_end, 3)
        ]

        plane_size = bytes_per_row * height
        planes = []
        offset = palette_end
        for _ in range(depth):
            end = offset + plane_size
            if end > len(data):
                raise ScreenshotParseError("capture truncated in its plane data")
            planes.append(data[offset:end])
            offset = end

        crop = (crop_x, crop_y, crop_w, crop_h) if (crop_w and crop_h) else None
        return cls(width, height, depth, bytes_per_row, view_modes, palette, planes, crop)

    def to_chunky(self) -> bytes:
        """De-planes the FULL SCREEN into one packed-byte-per-pixel,
        row-major buffer (one palette index per pixel) -- always the
        whole capture, regardless of `self.crop`; see `_region()` for
        the window-cropping to_ilbm()/to_png() do by default."""
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

    def _region(self, crop: bool) -> tuple[int, int, int, int]:
        """(x, y, w, h) to actually render: `self.crop` if `crop` is
        True and a crop rect is present (a `window=` capture), else
        the full screen -- the single place to_ilbm()/to_png() decide
        this, so both stay consistent."""
        if crop and self.crop is not None:
            return self.crop
        return (0, 0, self.width, self.height)

    def _region_chunky(self, x: int, y: int, w: int, h: int) -> bytes:
        """The full-screen chunky buffer, sliced down to one
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

    def _chunky_to_planes(self, chunky: bytes, width: int, height: int) -> list[bytes]:
        """Re-planes a chunky (one-byte-per-pixel) buffer back into
        `self.depth` bitplanes -- needed only for a CROPPED to_ilbm():
        the crop rectangle's x/w aren't generally byte-aligned, so the
        planes can't just be bit-sliced out of the originals the way
        the chunky path can be row-sliced; going through chunky once
        and re-planing is simpler and reuses _region_chunky()'s own
        cropping instead of duplicating bit-level crop math."""
        bytes_per_row = (width + 7) // 8
        planes = [bytearray(bytes_per_row * height) for _ in range(self.depth)]
        for row in range(height):
            row_base = row * width
            plane_row_base = row * bytes_per_row
            for col in range(width):
                pixel = chunky[row_base + col]
                byte_index = plane_row_base + (col >> 3)
                bit = 0x80 >> (col & 7)
                for plane_no in range(self.depth):
                    if pixel & (1 << plane_no):
                        planes[plane_no][byte_index] |= bit
        return [bytes(p) for p in planes]

    def to_ilbm(self, *, crop: bool = True) -> bytes:
        """A real `FORM ILBM` -- BMHD/CMAP/CAMG/BODY, uncompressed
        (BMHD's own compression field is 0) since the capture already
        arrived uncompressed and there's no need to spend host CPU on
        ByteRun1 just to shrink a local file.

        `crop` (default True): if this Screenshot came from a
        `window=` capture (`self.crop` set), render JUST that
        window's rectangle -- a real single-window screenshot, not
        the whole screen with the window merely noted in metadata.
        Pass `crop=False` for the full screen regardless."""
        x, y, w, h = self._region(crop)
        if (x, y, w, h) == (0, 0, self.width, self.height):
            planes = self.planes
        else:
            planes = self._chunky_to_planes(self._region_chunky(x, y, w, h), w, h)

        def chunk(cid: bytes, payload: bytes) -> bytes:
            padded = payload + (b"\x00" if len(payload) % 2 else b"")
            return cid + struct.pack(">I", len(payload)) + padded

        bmhd = struct.pack(
            ">HHhhBBBBHBBhh",
            w, h, 0, 0,                            # w, h, x, y
            self.depth, 0, 0, 0,                  # nPlanes, masking, compression, pad1
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
        """A real indexed-color (palette) PNG -- de-planed chunky
        pixels plus this capture's own palette, zlib-compressed. Bit
        depth 8 unconditionally: this module's own depth cap
        (screenshot.h: PLANEPTR[8], so `depth` is never above 8)
        always fits one byte per pixel index.

        `crop` (default True): see to_ilbm()'s own doc -- renders just
        a `window=` capture's own rectangle by default."""
        x, y, w, h = self._region(crop)
        chunky = self._region_chunky(x, y, w, h)

        def chunk(cid: bytes, payload: bytes) -> bytes:
            return (
                struct.pack(">I", len(payload))
                + cid
                + payload
                + struct.pack(">I", zlib.crc32(cid + payload) & 0xFFFFFFFF)
            )

        ihdr = struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)
        plte = b"".join(bytes(c) for c in self.palette)

        raw = bytearray()
        for row in range(h):
            raw.append(0)  # per-scanline filter type 0 (None)
            raw.extend(chunky[row * w:(row + 1) * w])
        idat = zlib.compress(bytes(raw), 9)

        return (
            b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", ihdr)
            + chunk(b"PLTE", plte)
            + chunk(b"IDAT", idat)
            + chunk(b"IEND", b"")
        )

    def save(self, base_path: str, *, crop: bool = True) -> tuple[str, str]:
        """Writes both `<base_path>.iff` (IFF ILBM) and
        `<base_path>.png`, returning `(ilbm_path, png_path)`. `crop`
        (default True) is forwarded to to_ilbm()/to_png() -- a
        `window=` capture is saved as just that window by default."""
        ilbm_path = f"{base_path}.iff"
        png_path = f"{base_path}.png"
        with open(ilbm_path, "wb") as f:
            f.write(self.to_ilbm(crop=crop))
        with open(png_path, "wb") as f:
            f.write(self.to_png(crop=crop))
        return ilbm_path, png_path
