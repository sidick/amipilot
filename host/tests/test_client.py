"""Unit tests for amipilot.client.Amipilot against a scripted transport
-- verifies command construction (quoting, verbatim TYPE text) and RC-
to-exception mapping per server/WIRE.md's RC policy. No emulator."""

import os
import socket
import sys
import time
import unittest
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from amipilot.client import ActionFailed, Amipilot, CommandError, NotFound  # noqa: E402
from amipilot.wire import WireClient  # noqa: E402


class FakeTransport:
    """Replies with one canned `RC ...` response per command, in
    order; records every command line sent."""

    def __init__(self, replies: list[bytes]):
        self._replies = list(replies)
        self.sent: list[bytes] = []
        self._buf = b""

    def sendall(self, data: bytes) -> None:
        self.sent.append(data)
        self._buf += self._replies.pop(0)

    def recv(self, _n: int) -> bytes:
        data, self._buf = self._buf, b""
        return data

    def close(self) -> None:
        pass


def client_with(*replies: bytes) -> Amipilot:
    return Amipilot(WireClient(FakeTransport(list(replies))))


class Quoting(unittest.TestCase):
    def test_plain_pattern_unquoted(self):
        c = client_with(b"RC 0 0\n")
        c.click("GadTools", 1)
        self.assertEqual(c._wire._t.sent[0], b"CLICK GadTools 1\n")

    def test_spaced_pattern_quoted(self):
        c = client_with(b"RC 0 0\n")
        c.click("My App", 1)
        self.assertEqual(c._wire._t.sent[0], b'CLICK "My App" 1\n')

    def test_type_text_sent_verbatim_unquoted(self):
        c = client_with(b"RC 0 0\n")
        c.type("GadTools", 2, "hello amipilot")
        self.assertEqual(c._wire._t.sent[0], b"TYPE GadTools 2 hello amipilot\n")

    def test_type_text_with_newline_rejected_locally(self):
        c = client_with()
        with self.assertRaises(ValueError):
            c.type("GadTools", 2, "hello\namipilot")

    def test_manifest_locator_forms(self):
        c = client_with(b"RC 0 0\n", b"RC 0 5\nhello")
        c.click_by_name("connect_button")
        c.type_by_name("host_field", "aminet.net")
        self.assertEqual(c._wire._t.sent[0], b"CLICK @connect_button\n")
        self.assertEqual(c._wire._t.sent[1], b"TYPE @host_field aminet.net\n")

    def test_launch_without_stack(self):
        c = client_with(b"RC 0 0\n")
        c.launch("SRC:build/fixtures/GTApp")
        self.assertEqual(c._wire._t.sent[0], b"LAUNCH SRC:build/fixtures/GTApp\n")

    def test_launch_with_stack(self):
        c = client_with(b"RC 0 0\n")
        c.launch("SRC:build/fixtures/GTApp", stack=8192)
        self.assertEqual(c._wire._t.sent[0],
                         b"LAUNCH STACK=8192 SRC:build/fixtures/GTApp\n")

    def test_launch_command_with_newline_rejected_locally(self):
        c = client_with()
        with self.assertRaises(ValueError):
            c.launch("SRC:build/fixtures/GTApp\nQUIT")

    def test_fs_verbs_quote_spaced_paths(self):
        c = client_with(b"RC 0 0\n", b"RC 0 0\n", b"RC 0 0\n")
        c.fs_mkdir("Work:My Dir")
        c.fs_delete("Work:My Dir")
        c.fs_get("Work:My Dir/file")
        self.assertEqual(c._wire._t.sent[0], b'FSMKDIR "Work:My Dir"\n')
        self.assertEqual(c._wire._t.sent[1], b'FSDELETE "Work:My Dir"\n')
        self.assertEqual(c._wire._t.sent[2], b'FSGET "Work:My Dir/file"\n')


class RcMapping(unittest.TestCase):
    def test_ok_returns_normally(self):
        c = client_with(b"RC 0 0\n")
        c.click("GadTools", 1)  # must not raise

    def test_warn_raises_not_found(self):
        c = client_with(b"RC 5 11\nnot matched")
        with self.assertRaises(NotFound):
            c.click("Gone", 1)

    def test_error_raises_command_error(self):
        c = client_with(b"RC 10 13\nbad arguments")
        with self.assertRaises(CommandError):
            c.click("GadTools", 1)

    def test_fail_raises_action_failed(self):
        c = client_with(b"RC 20 0\n")
        with self.assertRaises(ActionFailed):
            c.click("GadTools", 1)

    def test_exception_carries_rc_and_message(self):
        c = client_with(b"RC 5 9\nno window")
        with self.assertRaises(NotFound) as ctx:
            c.tree("Gone")
        self.assertEqual(ctx.exception.rc, 5)
        self.assertIn("no window", str(ctx.exception))

    def test_launch_fail_raises_action_failed(self):
        payload = b"launch failed (out of memory or no process slot)"
        c = client_with(b"RC 20 %d\n%s" % (len(payload), payload))
        with self.assertRaises(ActionFailed):
            c.launch("SRC:build/fixtures/GTApp")


class Verbs(unittest.TestCase):
    def test_get_text_returns_payload(self):
        c = client_with(b"RC 0 10\naminet.net")
        self.assertEqual(c.get_text("GadTools", 2), "aminet.net")

    def test_tree_parses_payload(self):
        payload = (
            'window "GadTools" [0,0 10x10]\n'
            '  gadget id=1 role=BUTTON class="" label="Connect" [0,0 1x1]\n'
        )
        c = client_with(b"RC 0 %d\n%s" % (len(payload), payload.encode()))
        window = c.tree("GadTools")
        self.assertEqual(window.title, "GadTools")
        self.assertEqual(window.gadgets[0].label, "Connect")

    def test_manifest_returns_load_report(self):
        payload = b"loaded GTApp: 1 windows, 3 gadgets"
        c = client_with(b"RC 0 %d\n%s" % (len(payload), payload))
        self.assertEqual(c.manifest("Prog:GTApp.manifest"),
                         "loaded GTApp: 1 windows, 3 gadgets")

    def test_quit_does_not_raise_on_ok(self):
        c = client_with(b"RC 0 0\n")
        c.quit()

    def test_fs_list_parses_entries(self):
        payload = (
            'entry name="GTApp" type=file size=4096 prot=rwed '
            'date="06-Aug-26 12:00:00" comment=""\n'
            'entry name="fixtures" type=dir size=0 prot=rwed '
            'date="06-Aug-26 12:00:00" comment="test data"\n'
        )
        c = client_with(b"RC 0 %d\n%s" % (len(payload), payload.encode()))
        entries = c.fs_list("Work:build")
        self.assertEqual(c._wire._t.sent[0], b"FSLIST Work:build\n")
        self.assertEqual(len(entries), 2)
        self.assertEqual(entries[0].name, "GTApp")
        self.assertFalse(entries[0].is_dir)
        self.assertEqual(entries[0].size, 4096)
        self.assertTrue(entries[1].is_dir)
        self.assertEqual(entries[1].comment, "test data")

    def test_fs_stat_parses_single_entry(self):
        payload = (
            'entry name="GTApp" type=file size=4096 prot=rwed '
            'date="06-Aug-26 12:00:00" comment=""\n'
        )
        c = client_with(b"RC 0 %d\n%s" % (len(payload), payload.encode()))
        entry = c.fs_stat("Work:build/GTApp")
        self.assertEqual(c._wire._t.sent[0], b"FSSTAT Work:build/GTApp\n")
        self.assertEqual(entry.name, "GTApp")
        self.assertEqual(entry.prot, "rwed")

    def test_fs_get_returns_raw_bytes_with_embedded_nul(self):
        payload = b"hello\x00world"
        c = client_with(b"RC 0 %d\n%s" % (len(payload), payload))
        self.assertEqual(c.fs_get("Work:build/data"), payload)

    def test_fs_mkdir_and_delete_do_not_raise_on_ok(self):
        c = client_with(b"RC 0 0\n", b"RC 0 0\n")
        c.fs_mkdir("Work:newdir")
        c.fs_delete("Work:newdir")

    def test_fs_list_outside_allowlist_raises_command_error(self):
        payload = b"path not under any granted root (granted: Work:)"
        c = client_with(b"RC 10 %d\n%s" % (len(payload), payload))
        with self.assertRaises(CommandError):
            c.fs_list("SYS:")

    def test_menu_parses_payload(self):
        payload = (
            'window "GadTools" [0,0 10x10]\n'
            'menu num=0 title="Project" enabled=1\n'
            '  item num=0/0 text="About" shortcut=A checkit=0 checked=0 enabled=1\n'
        )
        c = client_with(b"RC 0 %d\n%s" % (len(payload), payload.encode()))
        strip = c.menu("GadTools")
        self.assertEqual(c._wire._t.sent[0], b"MENU GadTools\n")
        self.assertEqual(strip.menus[0].items[0].text, "About")

    def test_menu_pick_without_subnum(self):
        c = client_with(b"RC 0 0\n")
        c.menu_pick("GadTools", 0, 0)
        self.assertEqual(c._wire._t.sent[0], b"MENUPICK GadTools 0 0\n")

    def test_menu_pick_with_subnum(self):
        c = client_with(b"RC 0 0\n")
        c.menu_pick("GadTools", 0, 4, 0)
        self.assertEqual(c._wire._t.sent[0], b"MENUPICK GadTools 0 4 0\n")

    def test_menu_pick_disabled_raises_action_failed(self):
        payload = b"menu item is disabled"
        c = client_with(b"RC 20 %d\n%s" % (len(payload), payload))
        with self.assertRaises(ActionFailed):
            c.menu_pick("GadTools", 0, 2)

    def test_menu_pick_no_shortcut_raises_action_failed(self):
        payload = b"item has no keyboard shortcut"
        c = client_with(b"RC 20 %d\n%s" % (len(payload), payload))
        with self.assertRaises(ActionFailed):
            c.menu_pick("GadTools", 0, 1)

    def test_menu_pick_missing_item_raises_not_found(self):
        c = client_with(b"RC 5 11\nnot matched")
        with self.assertRaises(NotFound):
            c.menu_pick("GadTools", 9, 9)


class FakeSocket:
    """A connected-socket stand-in for connect_with_retry's post-
    connect path: settimeout/sendall/recv/close, no real TCP. `chunks`
    are handed out one per recv() call; once exhausted, recv() raises
    a TimeoutError (matching real socket.settimeout() expiry -- the
    actual failure mode when the far end hasn't answered yet, not a
    closed-connection EOF)."""

    def __init__(self, chunks=()):
        self._chunks = list(chunks)
        self.closed = False

    def settimeout(self, _t):
        pass

    def sendall(self, _data):
        pass

    def recv(self, _n):
        if not self._chunks:
            raise TimeoutError("timed out")
        return self._chunks.pop(0)

    def close(self):
        self.closed = True


VERSION_PAYLOAD = (
    b"AMIPILOT 0.3 PROTOCOL 1\n"
    b"STABLE VERSION\n"
    b"EXPERIMENTAL TREE CLICK TYPE GETTEXT MANIFEST QUIT\n"
)


class ConnectWithRetry(unittest.TestCase):
    """Covers the two real, confirmed-live failure modes
    connect_with_retry() tolerates (see its own docstring): transient
    connect refusals, and a held connection needing VERSION resent
    until the far end is truly ready -- not a fresh reconnect per
    attempt, which risks stranding a genuine reply. No real sockets;
    socket.create_connection is patched directly."""

    def test_succeeds_after_transient_connect_refusals(self):
        attempts = {"n": 0}
        fake_sock = FakeSocket([b"RC 0 %d\n%s" % (len(VERSION_PAYLOAD), VERSION_PAYLOAD)])

        def fake_create_connection(addr, timeout=10.0):
            attempts["n"] += 1
            if attempts["n"] < 3:
                raise OSError("connection refused")
            return fake_sock

        with mock.patch.object(socket, "create_connection", fake_create_connection), \
             mock.patch.object(time, "sleep", lambda _s: None):
            client = Amipilot.connect_with_retry("127.0.0.1", 1234, deadline_seconds=5)

        self.assertEqual(attempts["n"], 3)
        self.assertEqual(client.info.protocol, 1)
        self.assertFalse(fake_sock.closed)

    def test_reuses_the_same_socket_not_a_new_one_per_attempt(self):
        calls = {"n": 0}
        fake_sock = FakeSocket([b"RC 0 %d\n%s" % (len(VERSION_PAYLOAD), VERSION_PAYLOAD)])

        def fake_create_connection(addr, timeout=10.0):
            calls["n"] += 1
            return fake_sock

        with mock.patch.object(socket, "create_connection", fake_create_connection), \
             mock.patch.object(time, "sleep", lambda _s: None):
            Amipilot.connect_with_retry("127.0.0.1", 1234, deadline_seconds=5)

        self.assertEqual(calls["n"], 1)

    def test_raises_timeout_when_connect_never_succeeds(self):
        def always_refuses(addr, timeout=10.0):
            raise OSError("connection refused")

        with mock.patch.object(socket, "create_connection", always_refuses), \
             mock.patch.object(time, "sleep", lambda _s: None):
            with self.assertRaisesRegex(TimeoutError, "could not reach the wire transport"):
                Amipilot.connect_with_retry("127.0.0.1", 1234, deadline_seconds=0)

    def test_raises_timeout_when_handshake_never_completes(self):
        fake_sock = FakeSocket([])  # recv() always times out

        with mock.patch.object(socket, "create_connection",
                                lambda addr, timeout=10.0: fake_sock), \
             mock.patch.object(time, "sleep", lambda _s: None):
            with self.assertRaisesRegex(TimeoutError, "never answered VERSION"):
                Amipilot.connect_with_retry("127.0.0.1", 1234, deadline_seconds=0.1)
        self.assertTrue(fake_sock.closed)


if __name__ == "__main__":
    unittest.main()
