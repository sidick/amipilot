import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from amipilot.model import TreeParseError, parse_tree, unescape  # noqa: E402

TREE_TEXT = (
    'window "GadTools" screen="Workbench Screen" [10,20 300x120]\n'
    '  gadget id=1 role=BUTTON class="" label="Connect" [10,90 80x14]\n'
    '  gadget id=2 role=STRING class="" label="Host" value="aminet.net" '
    "[10,20 200x14]\n"
)


class ParseTree(unittest.TestCase):
    def test_window_header(self):
        window = parse_tree(TREE_TEXT)
        self.assertEqual(window.title, "GadTools")
        self.assertEqual(window.screen, "Workbench Screen")
        self.assertEqual((window.left, window.top, window.width, window.height),
                          (10, 20, 300, 120))

    def test_gadgets_in_order(self):
        window = parse_tree(TREE_TEXT)
        self.assertEqual([g.gadget_id for g in window.gadgets], [1, 2])

    def test_gadget_without_value_falls_back_to_label(self):
        window = parse_tree(TREE_TEXT)
        connect = window.find(1)
        self.assertIsNone(connect.value)
        self.assertEqual(connect.text, "Connect")

    def test_gadget_with_value_prefers_it(self):
        window = parse_tree(TREE_TEXT)
        host = window.find(2)
        self.assertEqual(host.value, "aminet.net")
        self.assertEqual(host.text, "aminet.net")

    def test_find_missing_returns_none(self):
        self.assertIsNone(parse_tree(TREE_TEXT).find(99))

    def test_window_with_no_gadgets(self):
        window = parse_tree('window "Empty" screen="" [0,0 10x10]\n')
        self.assertEqual(window.gadgets, [])

    def test_empty_payload_raises(self):
        with self.assertRaises(TreeParseError):
            parse_tree("")

    def test_garbage_first_line_raises(self):
        with self.assertRaises(TreeParseError):
            parse_tree("not a window line\n")

    def test_garbage_gadget_line_raises(self):
        with self.assertRaises(TreeParseError):
            parse_tree('window "W" screen="S" [0,0 1x1]\n  not a gadget line\n')

    def test_label_with_embedded_quote_parses(self):
        # A real Amiga gadget label like `3.5" Drive` must round-trip --
        # the server escapes it as `3.5\" Drive` (EscapeQuotes(),
        # server/src/amipilotserver/main.c) and this must unescape it
        # back, not choke on the embedded quote.
        window = parse_tree(
            'window "GadTools" screen="Workbench Screen" [0,0 10x10]\n'
            '  gadget id=1 role=BUTTON class="" label="3.5\\" Drive" [0,0 1x1]\n'
        )
        self.assertEqual(window.gadgets[0].label, '3.5" Drive')

    def test_title_with_backslash_parses(self):
        window = parse_tree('window "C:\\\\Path" screen="" [0,0 1x1]\n')
        self.assertEqual(window.title, "C:\\Path")


class Unescape(unittest.TestCase):
    def test_no_escapes_unchanged(self):
        self.assertEqual(unescape("plain text"), "plain text")

    def test_escaped_quote(self):
        self.assertEqual(unescape('3.5\\" Drive'), '3.5" Drive')

    def test_escaped_backslash(self):
        self.assertEqual(unescape("C:\\\\Path"), "C:\\Path")

    def test_trailing_backslash_kept_literal(self):
        # A lone trailing backslash (not a valid escape sequence) is
        # passed through as-is rather than raising or dropping it --
        # matches the server's own EscapeQuotes(), which never emits
        # this, but the parser shouldn't crash on malformed input.
        self.assertEqual(unescape("abc\\"), "abc\\")


if __name__ == "__main__":
    unittest.main()
