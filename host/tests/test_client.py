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
from amipilot.wire import WireClient, WireError  # noqa: E402


class FakeTransport:
    """Replies with one canned `RC ...` response per command, in
    order; records every command line sent."""

    def __init__(self, replies: list[bytes]):
        self._replies = list(replies)
        self.sent: list[bytes] = []
        self._buf = b""
        self.closed = False

    def sendall(self, data: bytes) -> None:
        self.sent.append(data)
        self._buf += self._replies.pop(0)

    def recv(self, _n: int) -> bytes:
        data, self._buf = self._buf, b""
        return data

    def close(self) -> None:
        self.closed = True


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

    def test_pattern_with_embedded_quote_is_escaped(self):
        # A real AmigaDOS name can legitimately contain a literal '"'
        # (e.g. a `12" disk` comment/name) -- the server's own
        # read_token() (arexx_cmd.c) now understands the same
        # backslash-escaping its reply side already emits, so this
        # must be escaped and quoted, not rejected.
        c = client_with(b"RC 0 0\n")
        c.fs_mkdir('Work:12" disk')
        self.assertEqual(c._wire._t.sent[0], b'FSMKDIR "Work:12\\" disk"\n')

    def test_pattern_with_backslash_is_escaped(self):
        c = client_with(b"RC 0 0\n")
        c.fs_mkdir("Work:back\\slash")
        self.assertEqual(c._wire._t.sent[0], b'FSMKDIR "Work:back\\\\slash"\n')

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
            'window "GadTools" screen="Workbench Screen" [0,0 10x10]\n'
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
            'window "GadTools" screen="Workbench Screen" [0,0 10x10]\n'
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

    def test_tree_with_screen_prepends_screen_prefix(self):
        payload = 'window "GadTools" screen="Second Screen" [0,0 10x10]\n'
        c = client_with(b"RC 0 %d\n%s" % (len(payload), payload.encode()))
        window = c.tree("GadTools", screen="Second Screen")
        self.assertEqual(c._wire._t.sent[0], b'TREE SCREEN="Second Screen" GadTools\n')
        self.assertEqual(window.screen, "Second Screen")

    def test_click_without_screen_omits_prefix(self):
        c = client_with(b"RC 0 0\n")
        c.click("GadTools", 1)
        self.assertEqual(c._wire._t.sent[0], b"CLICK GadTools 1\n")

    def test_menu_pick_with_screen_prepends_prefix(self):
        c = client_with(b"RC 0 0\n")
        c.menu_pick("GadTools", 0, 0, screen="Second")
        self.assertEqual(c._wire._t.sent[0], b"MENUPICK SCREEN=Second GadTools 0 0\n")

    def test_screens_parses_payload(self):
        payload = (
            'screen title="Second Screen" [0,0 320x256] frontmost=1\n'
            'screen title="Workbench Screen" [0,0 640x256] frontmost=0\n'
        )
        c = client_with(b"RC 0 %d\n%s" % (len(payload), payload.encode()))
        screens = c.screens()
        self.assertEqual(c._wire._t.sent[0], b"SCREENS\n")
        self.assertEqual(len(screens), 2)
        self.assertTrue(screens[0].frontmost)


class FakeClock:
    """A controllable fake clock for wait_for_window()/wait_for_screen()'s
    poll loops -- patches time.monotonic/time.sleep so each simulated
    sleep deterministically advances the same clock time.monotonic()
    reads, instead of racing a real wall clock against however many
    canned replies FakeTransport has queued (mock.patch.object(time,
    "sleep", lambda _s: None), the pattern ConnectWithRetry's tests use
    below, would spin through every queued reply near-instantly with
    no way to control iteration count)."""

    def __init__(self):
        self.now = 0.0

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class WaitFor(unittest.TestCase):
    def test_wait_for_window_succeeds_after_polling(self):
        payload = 'window "GadTools" screen="" [0,0 10x10]\n'
        c = client_with(
            b"RC 5 11\nnot matched", b"RC 5 11\nnot matched",
            b"RC 0 %d\n%s" % (len(payload), payload.encode()),
        )
        clock = FakeClock()
        with mock.patch.object(time, "monotonic", clock.monotonic), \
             mock.patch.object(time, "sleep", clock.sleep):
            window = c.wait_for_window("GadTools", timeout=20.0, poll_interval=0.5)
        self.assertEqual(window.title, "GadTools")
        self.assertEqual(len(c._wire._t.sent), 3)

    def test_wait_for_window_passes_screen_through(self):
        payload = 'window "GadTools" screen="Second" [0,0 10x10]\n'
        c = client_with(b"RC 0 %d\n%s" % (len(payload), payload.encode()))
        clock = FakeClock()
        with mock.patch.object(time, "monotonic", clock.monotonic), \
             mock.patch.object(time, "sleep", clock.sleep):
            c.wait_for_window("GadTools", screen="Second", timeout=20.0)
        self.assertEqual(c._wire._t.sent[0], b'TREE SCREEN=Second GadTools\n')

    def test_wait_for_window_raises_timeout(self):
        c = client_with(
            b"RC 5 11\nnot matched", b"RC 5 11\nnot matched", b"RC 5 11\nnot matched",
        )
        clock = FakeClock()
        with mock.patch.object(time, "monotonic", clock.monotonic), \
             mock.patch.object(time, "sleep", clock.sleep):
            with self.assertRaisesRegex(TimeoutError, "no window matching"):
                c.wait_for_window("GadTools", timeout=1.0, poll_interval=0.5)

    def test_wait_for_screen_succeeds_after_polling(self):
        miss = 'screen title="Workbench Screen" [0,0 640x256] frontmost=1\n'
        hit = (
            'screen title="Second Screen" [0,0 320x256] frontmost=1\n'
            'screen title="Workbench Screen" [0,0 640x256] frontmost=0\n'
        )
        c = client_with(
            b"RC 0 %d\n%s" % (len(miss), miss.encode()),
            b"RC 0 %d\n%s" % (len(hit), hit.encode()),
        )
        clock = FakeClock()
        with mock.patch.object(time, "monotonic", clock.monotonic), \
             mock.patch.object(time, "sleep", clock.sleep):
            screen = c.wait_for_screen("Second", timeout=20.0, poll_interval=0.5)
        self.assertEqual(screen.title, "Second Screen")

    def test_wait_for_screen_raises_timeout(self):
        miss = 'screen title="Workbench Screen" [0,0 640x256] frontmost=1\n'
        c = client_with(
            b"RC 0 %d\n%s" % (len(miss), miss.encode()),
            b"RC 0 %d\n%s" % (len(miss), miss.encode()),
        )
        clock = FakeClock()
        with mock.patch.object(time, "monotonic", clock.monotonic), \
             mock.patch.object(time, "sleep", clock.sleep):
            with self.assertRaisesRegex(TimeoutError, "no screen matching"):
                c.wait_for_screen("Second", timeout=1.0, poll_interval=0.5)


class Connect(unittest.TestCase):
    """Amipilot.connect() (the single-attempt path, distinct from
    connect_with_retry()'s own retry loop) sends AUTH the same way."""

    def test_malformed_handshake_closes_socket(self):
        # A WireError from handshake() itself (not the AUTH step) must
        # still close the just-opened socket rather than leaking it.
        garbage_payload = b"not a valid VERSION payload\n"
        c = client_with(b"RC 0 %d\n%s" % (len(garbage_payload), garbage_payload))
        with mock.patch.object(WireClient, "connect", lambda *a, **kw: c._wire):
            with self.assertRaises(WireError):
                Amipilot.connect("127.0.0.1", 1234)
        self.assertTrue(c._wire._t.closed)

    def test_sends_auth_with_default_password(self):
        version_payload = (
            b"AMIPILOT 0.3 PROTOCOL 1\n"
            b"STABLE VERSION\n"
            b"EXPERIMENTAL TREE CLICK TYPE GETTEXT MANIFEST QUIT\n"
        )
        c = client_with(b"RC 0 %d\n%s" % (len(version_payload), version_payload),
                         b"RC 0 0\n")  # VERSION, then AUTH
        with mock.patch.object(WireClient, "connect", lambda *a, **kw: c._wire):
            Amipilot.connect("127.0.0.1", 1234)
        self.assertEqual(c._wire._t.sent[0], b"VERSION\n")
        self.assertEqual(c._wire._t.sent[1], b"AUTH amipilot\n")

    def test_wrong_password_raises_and_closes(self):
        version_payload = (
            b"AMIPILOT 0.3 PROTOCOL 1\n"
            b"STABLE VERSION\n"
            b"EXPERIMENTAL TREE CLICK TYPE GETTEXT MANIFEST QUIT\n"
        )
        auth_payload = b"authentication failed: wrong password"
        c = client_with(b"RC 0 %d\n%s" % (len(version_payload), version_payload),
                         b"RC 10 %d\n%s" % (len(auth_payload), auth_payload))
        with mock.patch.object(WireClient, "connect", lambda *a, **kw: c._wire):
            with self.assertRaises(CommandError):
                Amipilot.connect("127.0.0.1", 1234, password="wrong")


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
        self.sent: list[bytes] = []

    def settimeout(self, _t):
        pass

    def sendall(self, data):
        self.sent.append(data)

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
        fake_sock = FakeSocket([
            b"RC 0 %d\n%s" % (len(VERSION_PAYLOAD), VERSION_PAYLOAD),
            b"RC 0 0\n",  # AUTH, sent automatically by connect_with_retry()
        ])

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
        fake_sock = FakeSocket([
            b"RC 0 %d\n%s" % (len(VERSION_PAYLOAD), VERSION_PAYLOAD),
            b"RC 0 0\n",  # AUTH, sent automatically by connect_with_retry()
        ])

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

    def test_sends_auth_with_default_password_after_handshake(self):
        fake_sock = FakeSocket([
            b"RC 0 %d\n%s" % (len(VERSION_PAYLOAD), VERSION_PAYLOAD),
            b"RC 0 0\n",
        ])

        with mock.patch.object(socket, "create_connection",
                                lambda addr, timeout=10.0: fake_sock), \
             mock.patch.object(time, "sleep", lambda _s: None):
            Amipilot.connect_with_retry("127.0.0.1", 1234, deadline_seconds=5)

        self.assertEqual(fake_sock.sent[0], b"VERSION\n")
        self.assertEqual(fake_sock.sent[1], b"AUTH amipilot\n")

    def test_sends_auth_with_custom_password(self):
        fake_sock = FakeSocket([
            b"RC 0 %d\n%s" % (len(VERSION_PAYLOAD), VERSION_PAYLOAD),
            b"RC 0 0\n",
        ])

        with mock.patch.object(socket, "create_connection",
                                lambda addr, timeout=10.0: fake_sock), \
             mock.patch.object(time, "sleep", lambda _s: None):
            Amipilot.connect_with_retry("127.0.0.1", 1234, deadline_seconds=5,
                                         password="hunter2")

        self.assertEqual(fake_sock.sent[1], b"AUTH hunter2\n")

    def test_wrong_password_raises_command_error_and_closes_socket(self):
        payload = b"authentication failed: wrong password"
        fake_sock = FakeSocket([
            b"RC 0 %d\n%s" % (len(VERSION_PAYLOAD), VERSION_PAYLOAD),
            b"RC 10 %d\n%s" % (len(payload), payload),
        ])

        with mock.patch.object(socket, "create_connection",
                                lambda addr, timeout=10.0: fake_sock), \
             mock.patch.object(time, "sleep", lambda _s: None):
            with self.assertRaises(CommandError):
                Amipilot.connect_with_retry("127.0.0.1", 1234, deadline_seconds=5,
                                             password="wrong")

        self.assertTrue(fake_sock.closed)

    def test_malformed_handshake_closes_socket_without_retrying(self):
        # A WireError (malformed VERSION payload) is not the "far end
        # not ready yet" condition this retry loop exists for -- it
        # must close the socket and propagate immediately, not spin
        # until deadline_seconds like a genuine transient failure would.
        garbage_payload = b"not a valid VERSION payload\n"
        fake_sock = FakeSocket([b"RC 0 %d\n%s" % (len(garbage_payload), garbage_payload)])

        with mock.patch.object(socket, "create_connection",
                                lambda addr, timeout=10.0: fake_sock), \
             mock.patch.object(time, "sleep", lambda _s: None):
            with self.assertRaises(WireError):
                Amipilot.connect_with_retry("127.0.0.1", 1234, deadline_seconds=5)

        self.assertTrue(fake_sock.closed)

    def test_transient_oserror_during_auth_is_retried(self):
        # AUTH's own round trip is just as exposed to the "bytes
        # silently dropped, not buffered" failure mode as VERSION was
        # -- confirm it gets the same retry-on-the-same-connection
        # treatment, not an uncaught crash. FakeSocket.recv() raises
        # TimeoutError (an OSError) once its queued chunks run out, so
        # queuing only the first attempt's VERSION reply (no AUTH
        # reply behind it) simulates AUTH's response never arriving
        # that attempt, matching real socket.settimeout() expiry.
        fake_sock = FakeSocket([
            b"RC 0 %d\n%s" % (len(VERSION_PAYLOAD), VERSION_PAYLOAD),  # attempt 1: VERSION ok
            # (attempt 1's AUTH read times out here -- no chunk queued)
            b"RC 0 %d\n%s" % (len(VERSION_PAYLOAD), VERSION_PAYLOAD),  # attempt 2: VERSION ok
            b"RC 0 0\n",                                               # attempt 2: AUTH ok
        ])

        with mock.patch.object(socket, "create_connection",
                                lambda addr, timeout=10.0: fake_sock), \
             mock.patch.object(time, "sleep", lambda _s: None):
            client = Amipilot.connect_with_retry("127.0.0.1", 1234, deadline_seconds=5)

        self.assertEqual(client.info.protocol, 1)
        self.assertFalse(fake_sock.closed)


if __name__ == "__main__":
    unittest.main()
