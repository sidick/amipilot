"""Unit tests for amipilot.client.Amipilot against a scripted transport
-- verifies command construction (quoting, verbatim TYPE text) and RC-
to-exception mapping per server/WIRE.md's RC policy. No emulator."""

import os
import sys
import unittest

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


if __name__ == "__main__":
    unittest.main()
