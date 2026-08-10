"""Unit tests for amipilot.wire against server/WIRE.md -- pure framing,
no emulator, no network: the transport is a scripted double. Runs under
plain stdlib unittest (`make test-host`); the pytest plugin arrives
later in phase 0.3 and will sit on top of, not replace, these."""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from amipilot.wire import (  # noqa: E402
    MAX_LINE,
    PROTOCOL,
    ProtocolMismatch,
    Reply,
    WireClient,
    WireError,
    _SerialTransport,
    stderr_progress,
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

    def test_command_at_max_line_is_sent(self):
        # Exactly MAX_LINE bytes including the '\n' terminator must
        # still go through -- this is a boundary check on the
        # too-long rejection below, not a truncation test.
        t = FakeTransport(b"RC 0 0\n")
        client = WireClient(t)
        command = "TREE " + "x" * (MAX_LINE - 1 - len("TREE "))
        self.assertEqual(len(command) + 1, MAX_LINE)
        client.command(command)
        self.assertEqual(t.sent, command.encode("latin-1") + b"\n")

    def test_command_over_max_line_rejected_locally(self):
        # Explicit, local rejection -- not a silent truncation that
        # the server would otherwise chop into a shorter, different,
        # unintended command (server/WIRE.md's 512-byte request-line
        # cap; see AmipSerialLastLineOverflowed()'s doc comment in
        # server/include/serial.h for the server-side half of this).
        client = WireClient(FakeTransport(b""))
        command = "TREE " + "x" * MAX_LINE
        with self.assertRaises(ValueError):
            client.command(command)


class ProgressCallback(unittest.TestCase):
    def test_called_once_per_chunk_and_covers_header_over_read(self):
        # chunk=3 means the header line and the start of the payload
        # can arrive in the same recv() -- the callback must still
        # report correctly from wherever the buffer already stands,
        # not assume progress starts at 0.
        t = FakeTransport(b"RC 0 5\nhello", chunk=3)
        calls = []
        reply = WireClient(t).command("GETTEXT W 1", on_progress=lambda d, n: calls.append((d, n)))
        self.assertEqual(reply, Reply(0, b"hello"))
        # Every call reports the true total (5) and non-decreasing progress.
        self.assertTrue(all(total == 5 for _, total in calls))
        self.assertEqual([d for d, _ in calls], sorted(d for d, _ in calls))
        self.assertEqual(calls[-1], (5, 5))

    def test_zero_length_payload_still_calls_once(self):
        calls = []
        WireClient(FakeTransport(b"RC 0 0\n")).command(
            "CLICK W 1", on_progress=lambda d, n: calls.append((d, n))
        )
        self.assertEqual(calls, [(0, 0)])

    def test_not_called_when_omitted(self):
        # No callback, no behavior change from before this existed.
        t = FakeTransport(b"RC 0 5\nhello", chunk=1)
        reply = WireClient(t).command("GETTEXT W 1")
        self.assertEqual(reply, Reply(0, b"hello"))

    def test_default_none_does_not_raise(self):
        WireClient(FakeTransport(b"RC 0 0\n")).command("CLICK W 1", on_progress=None)


class StderrProgress(unittest.TestCase):
    def test_prints_overwriting_lines_ending_in_newline(self):
        import io

        buf = io.StringIO()
        progress = stderr_progress("shot")
        real_stderr = sys.stderr
        sys.stderr = buf
        try:
            progress(0, 10)
            progress(5, 10)
            progress(10, 10)
        finally:
            sys.stderr = real_stderr
        output = buf.getvalue()
        self.assertIn("shot: 0/10 bytes (0%)", output)
        self.assertIn("shot: 10/10 bytes (100%)", output)
        # Only the final, completed line ends with a real newline.
        self.assertTrue(output.rstrip("\n").count("\n") == 0)
        self.assertTrue(output.endswith("\n"))

    def test_zero_total_does_not_divide_by_zero(self):
        import io

        buf = io.StringIO()
        real_stderr = sys.stderr
        sys.stderr = buf
        try:
            stderr_progress()(0, 0)
        finally:
            sys.stderr = real_stderr
        self.assertIn("0/0 bytes", buf.getvalue())


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


class FakeSerial:
    """Stands in for pyserial's Serial object -- just the surface
    _SerialTransport actually touches (.write/.read/.close/.port/
    .timeout), so these tests don't need pyserial installed at all
    (it's an optional dependency, deliberately absent from the
    make test-host environment -- see pyproject.toml)."""

    def __init__(self, read_queue=(), port="/dev/fake0", timeout=5.0):
        self.port = port
        self.timeout = timeout
        self.written = b""
        self._read_queue = list(read_queue)
        self.closed = False

    def write(self, data: bytes) -> None:
        self.written += data

    def read(self, _n: int) -> bytes:
        return self._read_queue.pop(0) if self._read_queue else b""

    def close(self) -> None:
        self.closed = True


class SerialTransport(unittest.TestCase):
    def test_sendall_writes_through(self):
        ser = FakeSerial()
        t = _SerialTransport(ser)
        t.sendall(b"TREE GadTools\n")
        self.assertEqual(ser.written, b"TREE GadTools\n")

    def test_recv_reads_through(self):
        ser = FakeSerial(read_queue=[b"RC 0 0\n"])
        t = _SerialTransport(ser)
        self.assertEqual(t.recv(4096), b"RC 0 0\n")

    def test_recv_empty_raises_timeout_not_silent_close(self):
        # pyserial's own read() returns b"" purely on ITS OWN timeout
        # elapsing, not because anything closed -- WireClient._recv()
        # would otherwise misdiagnose an empty read as "the peer closed
        # the connection" (correct for a real socket, wrong here), so
        # this must surface as an explicit TimeoutError instead.
        ser = FakeSerial(read_queue=[b""], port="/dev/fake0", timeout=3.0)
        t = _SerialTransport(ser)
        with self.assertRaises(TimeoutError) as ctx:
            t.recv(4096)
        self.assertIn("/dev/fake0", str(ctx.exception))

    def test_close_closes_through(self):
        ser = FakeSerial()
        t = _SerialTransport(ser)
        t.close()
        self.assertTrue(ser.closed)


class ConnectSerial(unittest.TestCase):
    def test_without_pyserial_raises_runtime_error(self):
        try:
            import serial  # noqa: F401
        except ImportError:
            pass
        else:
            self.skipTest("pyserial is installed in this environment")
        with self.assertRaises(RuntimeError) as ctx:
            WireClient.connect_serial("/dev/nonexistent", 19200)
        self.assertIn("pyserial", str(ctx.exception))


if __name__ == "__main__":
    unittest.main()
