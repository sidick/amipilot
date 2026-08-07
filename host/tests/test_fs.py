import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from amipilot.fs import FsParseError, parse_fs_entries, parse_fs_entry  # noqa: E402

ENTRY_TEXT = (
    'entry name="GTApp" type=file size=4096 prot=rwed '
    'date="06-Aug-26 12:00:00" comment=""\n'
    'entry name="fixtures" type=dir size=0 prot=rwed '
    'date="06-Aug-26 12:00:00" comment="test data"\n'
)


class ParseFsEntries(unittest.TestCase):
    def test_two_entries_in_order(self):
        entries = parse_fs_entries(ENTRY_TEXT)
        self.assertEqual([e.name for e in entries], ["GTApp", "fixtures"])

    def test_file_vs_dir(self):
        entries = parse_fs_entries(ENTRY_TEXT)
        self.assertFalse(entries[0].is_dir)
        self.assertTrue(entries[1].is_dir)

    def test_size_and_prot(self):
        entry = parse_fs_entries(ENTRY_TEXT)[0]
        self.assertEqual(entry.size, 4096)
        self.assertEqual(entry.prot, "rwed")

    def test_empty_payload_returns_empty_list(self):
        self.assertEqual(parse_fs_entries(""), [])

    def test_garbage_line_raises(self):
        with self.assertRaises(FsParseError):
            parse_fs_entries("not an entry line\n")

    def test_name_with_embedded_quote_parses(self):
        # A real AmigaDOS filename/comment can legitimately contain a
        # literal '"' (e.g. a `12" disk` comment) -- the server escapes
        # it (EscapeQuotesInto(), server/src/fs.c) and this must
        # unescape it back rather than choking on the embedded quote.
        text = (
            'entry name="drive" type=file size=1 prot=rwed '
            'date="06-Aug-26 12:00:00" comment="12\\" disk"\n'
        )
        entry = parse_fs_entries(text)[0]
        self.assertEqual(entry.comment, '12" disk')


class ParseFsEntry(unittest.TestCase):
    def test_single_entry(self):
        text = (
            'entry name="GTApp" type=file size=4096 prot=rwed '
            'date="06-Aug-26 12:00:00" comment=""\n'
        )
        entry = parse_fs_entry(text)
        self.assertEqual(entry.name, "GTApp")

    def test_zero_entries_raises(self):
        with self.assertRaises(FsParseError):
            parse_fs_entry("")

    def test_multiple_entries_raises(self):
        with self.assertRaises(FsParseError):
            parse_fs_entry(ENTRY_TEXT)


if __name__ == "__main__":
    unittest.main()
