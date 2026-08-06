"""Unit tests for amipilot.wire against server/WIRE.md -- pure framing,
no emulator, no network: the transport is a scripted double. Runs under
plain stdlib unittest (`make test-host`); the pytest plugin arrives
later in phase 0.3 and will sit on top of, not replace, these."""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from amipilot.wire import (  # noqa: E402
    PROTOCOL,
    ProtocolMismatch,
    Reply,
    WireClient,
    WireError,
)


class FakeTransport:
    """Feeds canned response bytes in deliberately awkward chunks (1
    byte at a time by default) and records everything sent."""

    def __init__(self, response: bytes, chunk: int = 1):
        self._response = response
        self._chunk = chunk
        self.sent = b""
        self.closed = False

    def sendall(self, data: bytes) -> None:
        self.sent += data

    def recv(self, _n: int) -> bytes:
        data, self._response = (
            self._response[: self._chunk],
            self._response[self._chunk :],
        )
        return data

    def close(self) -> None:
        self.closed = True


class CommandFraming(unittest.TestCase):
    def test_basic_reply(self):
        t = FakeTransport(b"RC 0 5\nhello")
        reply = WireClient(t).command("GETTEXT GadTools 2")
        self.assertEqual(t.sent, b"GETTEXT GadTools 2\n")
        self.assertEqual(reply, Reply(0, b"hello"))
        self.assertTrue(reply.ok)

    def test_zero_length_payload(self):
        reply = WireClient(FakeTransport(b"RC 5 0\n")).command("CLICK Gone 1")
        self.assertEqual(reply, Reply(5, b""))
        self.assertFalse(reply.ok)

    def test_payload_read_by_count_not_delimiters(self):
        # A payload that contains newlines and an RC-header lookalike
        # must be consumed strictly by byte count.
        payload = b"line one\nRC 0 99\nline three"
        t = FakeTransport(b"RC 0 %d\n%s" % (len(payload), payload))
        client = WireClient(t)
        self.assertEqual(client.command("TREE GadTools").payload, payload)

    def test_back_to_back_replies_in_one_stream(self):
        t = FakeTransport(b"RC 0 2\nabRC 10 3\nboo", chunk=64)
        client = WireClient(t)
        self.assertEqual(client.command("GETTEXT W 1"), Reply(0, b"ab"))
        self.assertEqual(client.command("NONSENSE"), Reply(10, b"boo"))

    def test_malformed_header_raises(self):
        for bad in (b"OK 0 0\n", b"RC 0\n", b"RC x 0\n", b"RC 0 x\n"):
            with self.assertRaises(WireError):
                WireClient(FakeTransport(bad)).command("VERSION")

    def test_connection_closed_mid_payload_raises(self):
        with self.assertRaises(WireError):
            WireClient(FakeTransport(b"RC 0 10\nshort")).command("TREE W")

    def test_terminator_in_command_rejected_locally(self):
        client = WireClient(FakeTransport(b""))
        with self.assertRaises(ValueError):
            client.command("TREE W\nQUIT")


class Handshake(unittest.TestCase):
    PAYLOAD = (
        b"AMIPILOT 0.3 PROTOCOL 1\n"
        b"STABLE VERSION\n"
        b"EXPERIMENTAL TREE CLICK TYPE GETTEXT MANIFEST QUIT\n"
    )

    def test_parses_version_payload(self):
        t = FakeTransport(b"RC 0 %d\n%s" % (len(self.PAYLOAD), self.PAYLOAD))
        info = WireClient(t).handshake()
        self.assertEqual(t.sent, b"VERSION\n")
        self.assertEqual(info.server_version, "0.3")
        self.assertEqual(info.protocol, PROTOCOL)
        self.assertEqual(info.stable, ["VERSION"])
        self.assertIn("TREE", info.experimental)
        self.assertIn("QUIT", info.experimental)

    def test_protocol_mismatch_disconnects(self):
        payload = b"AMIPILOT 9.9 PROTOCOL 2\nSTABLE VERSION\nEXPERIMENTAL\n"
        t = FakeTransport(b"RC 0 %d\n%s" % (len(payload), payload))
        with self.assertRaises(ProtocolMismatch):
            WireClient(t).handshake()
        self.assertTrue(t.closed)

    def test_error_rc_raises(self):
        with self.assertRaises(WireError):
            WireClient(FakeTransport(b"RC 10 0\n")).handshake()


if __name__ == "__main__":
    unittest.main()
