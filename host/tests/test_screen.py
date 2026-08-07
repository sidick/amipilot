import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from amipilot.screen import ScreenParseError, parse_screens  # noqa: E402

# Captured verbatim from a real SCREENS reply against fixtures/gadtools-app
# + fixtures/second-screen-app under Copperline (2026-08-07).
SCREENS_TEXT = (
    'screen title="AmiPilot Second Screen" [0,0 320x256] frontmost=1\n'
    'screen title="Workbench Screen" [0,0 640x256] frontmost=0\n'
)


class ParseScreens(unittest.TestCase):
    def test_two_screens_in_order(self):
        screens = parse_screens(SCREENS_TEXT)
        self.assertEqual([s.title for s in screens],
                          ["AmiPilot Second Screen", "Workbench Screen"])

    def test_frontmost_flag(self):
        screens = parse_screens(SCREENS_TEXT)
        self.assertTrue(screens[0].frontmost)
        self.assertFalse(screens[1].frontmost)

    def test_dimensions(self):
        screen = parse_screens(SCREENS_TEXT)[1]
        self.assertEqual((screen.left, screen.top, screen.width, screen.height),
                          (0, 0, 640, 256))

    def test_empty_payload_returns_empty_list(self):
        self.assertEqual(parse_screens(""), [])

    def test_garbage_line_raises(self):
        with self.assertRaises(ScreenParseError):
            parse_screens("not a screen line\n")


if __name__ == "__main__":
    unittest.main()
