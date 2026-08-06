import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from amipilot.menu import MenuParseError, parse_menu_strip  # noqa: E402

# Captured verbatim from a real AmiInspect run against fixtures/gadtools-app
# under Copperline (2026-08-06) -- see tests/copperline/fixtures/gadtools-app's
# menu strip and tests/copperline/menu-test.py.
MENU_TEXT = (
    'window "AmiPilot GadTools Fixture" [40,40 220x130]\n'
    'menu num=0 title="Project" enabled=1\n'
    '  item num=0/0 text="About" shortcut=A checkit=0 checked=0 enabled=1\n'
    '  item num=0/1 text="Toggle" shortcut=T checkit=1 checked=1 enabled=1\n'
    '  item num=0/2 text="Disabled" checkit=0 checked=0 enabled=0\n'
    '  item num=0/3 text="" checkit=0 checked=0 enabled=0\n'
    '  item num=0/4 text="More" checkit=0 checked=0 enabled=1\n'
    '    subitem num=0/4/0 text="Sub Item" shortcut=S checkit=0 checked=0 enabled=1\n'
)


class ParseMenuStrip(unittest.TestCase):
    def test_window_header(self):
        strip = parse_menu_strip(MENU_TEXT)
        self.assertEqual(strip.window_title, "AmiPilot GadTools Fixture")

    def test_one_menu_with_five_top_level_items(self):
        strip = parse_menu_strip(MENU_TEXT)
        self.assertEqual(len(strip.menus), 1)
        menu = strip.menus[0]
        self.assertEqual(menu.menu_num, 0)
        self.assertEqual(menu.title, "Project")
        self.assertTrue(menu.enabled)
        self.assertEqual(len(menu.items), 5)

    def test_item_with_shortcut(self):
        about = parse_menu_strip(MENU_TEXT).menus[0].items[0]
        self.assertEqual(about.text, "About")
        self.assertEqual(about.shortcut, "A")
        self.assertTrue(about.enabled)
        self.assertFalse(about.checkit)
        self.assertIsNone(about.sub_num)

    def test_checkit_item(self):
        toggle = parse_menu_strip(MENU_TEXT).menus[0].items[1]
        self.assertTrue(toggle.checkit)
        self.assertTrue(toggle.checked)

    def test_disabled_item_has_no_shortcut(self):
        disabled = parse_menu_strip(MENU_TEXT).menus[0].items[2]
        self.assertFalse(disabled.enabled)
        self.assertIsNone(disabled.shortcut)

    def test_separator_bar_is_a_disabled_blank_item(self):
        bar = parse_menu_strip(MENU_TEXT).menus[0].items[3]
        self.assertEqual(bar.text, "")
        self.assertFalse(bar.enabled)

    def test_submenu_item(self):
        more = parse_menu_strip(MENU_TEXT).menus[0].items[4]
        self.assertEqual(len(more.sub_items), 1)
        sub = more.sub_items[0]
        self.assertEqual(sub.text, "Sub Item")
        self.assertEqual(sub.shortcut, "S")
        self.assertEqual(sub.menu_num, 0)
        self.assertEqual(sub.item_num, 4)
        self.assertEqual(sub.sub_num, 0)

    def test_find_by_text_top_level(self):
        strip = parse_menu_strip(MENU_TEXT)
        found = strip.find("About")
        self.assertIsNotNone(found)
        self.assertEqual((found.menu_num, found.item_num), (0, 0))

    def test_find_by_text_submenu(self):
        strip = parse_menu_strip(MENU_TEXT)
        found = strip.find("Sub Item")
        self.assertIsNotNone(found)
        self.assertEqual((found.menu_num, found.item_num, found.sub_num), (0, 4, 0))

    def test_find_missing_returns_none(self):
        self.assertIsNone(parse_menu_strip(MENU_TEXT).find("Nonexistent"))

    def test_window_with_no_menus(self):
        strip = parse_menu_strip('window "Empty" [0,0 10x10]\n')
        self.assertEqual(strip.menus, [])

    def test_empty_payload_raises(self):
        with self.assertRaises(MenuParseError):
            parse_menu_strip("")

    def test_garbage_first_line_raises(self):
        with self.assertRaises(MenuParseError):
            parse_menu_strip("not a window line\n")

    def test_item_before_any_menu_raises(self):
        with self.assertRaises(MenuParseError):
            parse_menu_strip(
                'window "W" [0,0 1x1]\n'
                '  item num=0/0 text="X" checkit=0 checked=0 enabled=1\n'
            )

    def test_subitem_before_any_item_raises(self):
        with self.assertRaises(MenuParseError):
            parse_menu_strip(
                'window "W" [0,0 1x1]\n'
                'menu num=0 title="M" enabled=1\n'
                '    subitem num=0/0/0 text="X" checkit=0 checked=0 enabled=1\n'
            )

    def test_garbage_line_raises(self):
        with self.assertRaises(MenuParseError):
            parse_menu_strip('window "W" [0,0 1x1]\nnot a menu line\n')


if __name__ == "__main__":
    unittest.main()
