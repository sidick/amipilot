"""Unit tests for amipilot.screenshot -- pure parsing/encoding logic
against a synthetic raw capture matching server/include/screenshot.h's
own wire layout exactly. No emulator needed: these tests decode what
to_ilbm()/to_png() themselves wrote, the same way a real IFF/PNG
reader would, so a corrupted chunk or wrong byte order fails loudly."""

import os
import struct
import sys
import unittest
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from amipilot.screenshot import Screenshot, ScreenshotParseError  # noqa: E402

# 8x2 pixels, depth=1 (2 colours), bytesPerRow=1 -- small enough to
# hand-verify every bit. Row 0 = 0b10100000 (pixels 0,2 set), row 1 =
# 0b00000001 (pixel 7 set). viewModes=0 (plain, non-interlaced).
_WIDTH, _HEIGHT, _DEPTH, _BPR = 8, 2, 1, 1
_PALETTE = [(0, 0, 0), (255, 255, 255)]
_PLANE0 = bytes([0b10100000, 0b00000001])


def _build_capture(*, crop=(0, 0, 0, 0)) -> bytes:
    header = struct.pack(
        ">HHBBHHHHHHH",
        _WIDTH, _HEIGHT, _DEPTH, 0, _BPR, 0, len(_PALETTE),
        *crop,
    )
    palette = b"".join(bytes(c) for c in _PALETTE)
    return header + palette + _PLANE0


class Parse(unittest.TestCase):
    def test_fields_roundtrip(self):
        shot = Screenshot.parse(_build_capture())
        self.assertEqual((shot.width, shot.height, shot.depth, shot.bytes_per_row),
                          (_WIDTH, _HEIGHT, _DEPTH, _BPR))
        self.assertEqual(shot.palette, _PALETTE)
        self.assertEqual(shot.planes, [_PLANE0])
        self.assertIsNone(shot.crop)

    def test_nonzero_crop_rect_parsed(self):
        shot = Screenshot.parse(_build_capture(crop=(10, 20, 30, 40)))
        self.assertEqual(shot.crop, (10, 20, 30, 40))

    def test_too_short_raises(self):
        with self.assertRaises(ScreenshotParseError):
            Screenshot.parse(b"\x00" * 10)

    def test_truncated_plane_raises(self):
        with self.assertRaises(ScreenshotParseError):
            Screenshot.parse(_build_capture()[:-1])


class Chunky(unittest.TestCase):
    def test_depaned_pixel_values(self):
        shot = Screenshot.parse(_build_capture())
        # row 0: 0b10100000 -> pixels 0,2 set (1-bit depth -> index 1)
        # row 1: 0b00000001 -> pixel 7 set
        expected = [1, 0, 1, 0, 0, 0, 0, 0,
                    0, 0, 0, 0, 0, 0, 0, 1]
        self.assertEqual(list(shot.to_chunky()), expected)


def _read_iff_chunks(data: bytes) -> dict:
    assert data[:4] == b"FORM"
    assert data[8:12] == b"ILBM"
    chunks = {}
    offset = 12
    while offset < len(data):
        cid = data[offset:offset + 4]
        length = struct.unpack(">I", data[offset + 4:offset + 8])[0]
        payload = data[offset + 8:offset + 8 + length]
        chunks[cid] = payload
        offset += 8 + length + (length % 2)
    return chunks


class Ilbm(unittest.TestCase):
    def test_bmhd_fields(self):
        shot = Screenshot.parse(_build_capture())
        chunks = _read_iff_chunks(shot.to_ilbm())
        w, h, x, y, nplanes, masking, compression = struct.unpack(">HHhhBBB", chunks[b"BMHD"][:11])
        self.assertEqual((w, h, x, y, nplanes), (_WIDTH, _HEIGHT, 0, 0, _DEPTH))
        self.assertEqual(compression, 0, "capture is uncompressed -- BMHD must say so")

    def test_window_crop_shrinks_bmhd_and_body_by_default(self):
        # crop=(2, 0, 4, 1): row 0, columns 2..5 -- chunky[2:6] of
        # [1,0,1,0,0,0,0,0] is [1,0,0,0], i.e. only pixel 2 (the
        # window-relative pixel 0) is set.
        shot = Screenshot.parse(_build_capture(crop=(2, 0, 4, 1)))
        chunks = _read_iff_chunks(shot.to_ilbm())
        w, h = struct.unpack(">HH", chunks[b"BMHD"][:4])
        self.assertEqual((w, h), (4, 1))
        self.assertEqual(chunks[b"BODY"], bytes([0b10000000]))

    def test_crop_false_keeps_full_screen(self):
        shot = Screenshot.parse(_build_capture(crop=(2, 0, 4, 1)))
        chunks = _read_iff_chunks(shot.to_ilbm(crop=False))
        w, h = struct.unpack(">HH", chunks[b"BMHD"][:4])
        self.assertEqual((w, h), (_WIDTH, _HEIGHT))
        self.assertEqual(chunks[b"BODY"], _PLANE0)

    def test_cmap_matches_palette(self):
        shot = Screenshot.parse(_build_capture())
        chunks = _read_iff_chunks(shot.to_ilbm())
        self.assertEqual(chunks[b"CMAP"], b"".join(bytes(c) for c in _PALETTE))

    def test_camg_matches_view_modes(self):
        shot = Screenshot.parse(_build_capture())
        chunks = _read_iff_chunks(shot.to_ilbm())
        self.assertEqual(struct.unpack(">I", chunks[b"CAMG"])[0], 0)

    def test_body_matches_planes(self):
        shot = Screenshot.parse(_build_capture())
        chunks = _read_iff_chunks(shot.to_ilbm())
        self.assertEqual(chunks[b"BODY"], _PLANE0)


class Png(unittest.TestCase):
    def test_signature_and_ihdr(self):
        shot = Screenshot.parse(_build_capture())
        data = shot.to_png()
        self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")

        length, cid = struct.unpack(">I4s", data[8:16])
        self.assertEqual(cid, b"IHDR")
        w, h, bitdepth, colortype = struct.unpack(">IIBB", data[16:26])
        self.assertEqual((w, h, bitdepth, colortype), (_WIDTH, _HEIGHT, 8, 3))

    def test_window_crop_shrinks_ihdr_by_default(self):
        shot = Screenshot.parse(_build_capture(crop=(2, 0, 4, 1)))
        w, h = struct.unpack(">II", shot.to_png()[16:24])
        self.assertEqual((w, h), (4, 1))

    def test_crop_false_keeps_full_screen(self):
        shot = Screenshot.parse(_build_capture(crop=(2, 0, 4, 1)))
        w, h = struct.unpack(">II", shot.to_png(crop=False)[16:24])
        self.assertEqual((w, h), (_WIDTH, _HEIGHT))

    def test_idat_decompresses_to_expected_scanlines(self):
        shot = Screenshot.parse(_build_capture())
        data = shot.to_png()

        # Walk chunks generically to find IDAT/PLTE regardless of order.
        chunks = {}
        offset = 8
        while offset < len(data):
            length = struct.unpack(">I", data[offset:offset + 4])[0]
            cid = data[offset + 4:offset + 8]
            payload = data[offset + 8:offset + 8 + length]
            crc = struct.unpack(">I", data[offset + 8 + length:offset + 12 + length])[0]
            self.assertEqual(crc, zlib.crc32(cid + payload) & 0xFFFFFFFF, f"bad CRC for {cid!r}")
            chunks[cid] = payload
            offset += 12 + length

        self.assertEqual(chunks[b"PLTE"], b"".join(bytes(c) for c in _PALETTE))

        raw = zlib.decompress(chunks[b"IDAT"])
        chunky = shot.to_chunky()
        expected = bytearray()
        for row in range(_HEIGHT):
            expected.append(0)  # filter type None
            expected.extend(chunky[row * _WIDTH:(row + 1) * _WIDTH])
        self.assertEqual(raw, bytes(expected))


if __name__ == "__main__":
    unittest.main()
