"""Tests for golden.py's assert_golden() -- pure-Python, no wire/emulator
needed (unittest.TestCase, matching this test suite's stdlib-first
convention -- see conftest.py)."""

from __future__ import annotations

import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from amipilot.golden import GoldenMismatch, assert_golden
from amipilot.model import Gadget, Window


def make_window(label="_Connect"):
    return Window(
        title="AmiPilot GadTools Fixture",
        screen="Workbench",
        left=0, top=11, width=400, height=150,
        gadgets=[
            Gadget(
                gadget_id=1, role="button", class_name="", label=label,
                value=None, left=10, top=10, width=80, height=20,
            ),
        ],
    )


class AssertGolden(unittest.TestCase):
    def test_missing_file_is_created(self):
        with TemporaryDirectory() as d:
            path = Path(d) / "sub" / "GTApp.golden"
            assert_golden(make_window(), path)
            self.assertTrue(path.exists())
            self.assertIn("_Connect", path.read_text(encoding="latin-1"))

    def test_matching_tree_passes_silently(self):
        with TemporaryDirectory() as d:
            path = Path(d) / "GTApp.golden"
            assert_golden(make_window(), path)
            assert_golden(make_window(), path)  # second call: no exception

    def test_mismatch_raises_with_diff(self):
        with TemporaryDirectory() as d:
            path = Path(d) / "GTApp.golden"
            assert_golden(make_window(label="_Connect"), path)
            with self.assertRaises(GoldenMismatch) as ctx:
                assert_golden(make_window(label="_Disconnect"), path)
            self.assertIn("_Connect", ctx.exception.diff)
            self.assertIn("_Disconnect", ctx.exception.diff)
            self.assertEqual(ctx.exception.path, path)

    def test_update_overwrites_existing_mismatch(self):
        with TemporaryDirectory() as d:
            path = Path(d) / "GTApp.golden"
            assert_golden(make_window(label="_Connect"), path)
            assert_golden(make_window(label="_Disconnect"), path, update=True)
            self.assertIn("_Disconnect", path.read_text(encoding="latin-1"))
            # now matches without update=True
            assert_golden(make_window(label="_Disconnect"), path)


if __name__ == "__main__":
    unittest.main()
