"""The AmiPilot wire protocol, host side -- implements server/WIRE.md.

Requests are the server's command grammar, one LF-terminated line each;
every request gets exactly one length-prefixed response:

    RC <code> <byte-count>\n
    <byte-count bytes of payload>

Parsing is strictly by byte count -- payloads are raw bytes and may
contain anything, including text that looks like an ``RC`` header.

The client is transport-agnostic: it drives any object with
``sendall(bytes)`` and ``recv(n) -> bytes`` (a real socket to
Copperline's ``[serial] mode = "tcp"`` bridge, a pyserial wrapper, a
test double). ``WireClient.connect()`` is the TCP convenience
constructor; ``WireClient.connect_serial()`` is the real-serial-port
one (real Amiga hardware, or a Copperline config using a real/virtual
serial device instead of ``[serial] mode = "tcp"``) -- see its own
docstring. Server-side, ``AmiPilotServer``'s own ``SERIAL``/
``SERDEVICE``/``SERUNIT``/``BAUD`` startup arguments are the matching
half of this (server/README.md); both sides need to agree on device/
baud, same as any real serial link.
"""

from __future__ import annotations

import socket
from dataclasses import dataclass, field

#: The one protocol version this client speaks (WIRE.md "Versioning").
PROTOCOL = 1

#: Maximum request line, INCLUDING the trailing '\n' terminator --
#: server/WIRE.md's own cap (enforced server-side by AMIP_SER_LINE/
#: AMIP_TCP_LINE, both 512, serial.c/tcp.c). Checked here so an
#: oversized command fails fast, explicitly, on the host -- rather
#: than being silently truncated on the wire into a shorter, different,
#: unintended command (see AmipSerialLastLineOverflowed()'s doc
#: comment in server/include/serial.h for why that matters).
MAX_LINE = 512

# The server's RC policy (server/include/arexx_cmd.h).
RC_OK = 0
RC_WARN = 5
RC_ERROR = 10
RC_TIMEOUT = 15
RC_FAIL = 20


class WireError(Exception):
    """Framing violation or transport failure."""


class ProtocolMismatch(WireError):
    """The server speaks a protocol version this client does not."""


@dataclass
class Reply:
    rc: int
    payload: bytes

    @property
    def text(self) -> str:
        """The payload as text (the wire's payloads are Amiga-side
        Latin-1; never raises on stray bytes)."""
        return self.payload.decode("latin-1")

    @property
    def ok(self) -> bool:
        return self.rc == RC_OK


@dataclass
class ServerInfo:
    """Parsed VERSION handshake payload (WIRE.md "Handshake")."""

    server_version: str
    protocol: int
    stable: list[str] = field(default_factory=list)
    experimental: list[str] = field(default_factory=list)


class _SerialTransport:
    """Adapts a pyserial ``Serial`` object (``.write(bytes)``/
    ``.read(n)``) to the ``sendall(bytes)``/``recv(n) -> bytes``
    protocol ``WireClient`` drives -- the only transport-specific glue
    a real serial port needs; framing/RC handling is identical to TCP.

    ``recv()`` raises ``TimeoutError`` (not the empty ``b""`` pyserial
    itself returns) when the port's own read timeout elapses with
    nothing received. This matters: ``WireClient._recv()`` treats an
    empty ``recv()`` as "the peer closed the connection" (the correct
    reading for a TCP socket, where recv() truly can't return empty
    for any other reason) and raises ``WireError`` accordingly -- but
    for a serial port, empty almost always means "nothing arrived
    within the timeout," a different and more actionable condition
    (wrong device/baud, AmiPilotServer not running SERIAL, a dead
    cable) that deserves its own clear exception rather than being
    misreported as a graceful close that never actually happened."""

    def __init__(self, ser):
        self._ser = ser

    def sendall(self, data: bytes) -> None:
        self._ser.write(data)

    def recv(self, n: int) -> bytes:
        data = self._ser.read(n)
        if not data:
            raise TimeoutError(
                f"no data received on {self._ser.port} within "
                f"{self._ser.timeout}s -- check AmiPilotServer is running "
                "SERIAL on the Amiga side with a matching SERDEVICE/"
                "SERUNIT/BAUD (server/README.md)"
            )
        return data

    def close(self) -> None:
        self._ser.close()


class WireClient:
    def __init__(self, transport):
        self._t = transport
        self._buf = b""

    @classmethod
    def connect(cls, host: str, port: int, timeout: float = 10.0) -> "WireClient":
        """Connect to a TCP byte-stream carrying the wire -- e.g.
        Copperline's serial bridge (default 127.0.0.1:1234)."""
        return cls(socket.create_connection((host, port), timeout=timeout))

    @classmethod
    def connect_serial(cls, device: str, baud: int = 19200, *, timeout: float = 10.0) -> "WireClient":
        """Connect over a real (or virtual) serial port -- for driving
        real Amiga hardware over a real cable, or a Copperline config
        using a real/virtual serial device instead of ``[serial] mode
        = "tcp"``. `device` is OS-specific (e.g. ``/dev/tty.usbserial-*``
        on macOS, ``/dev/ttyUSB0`` on Linux, ``COM3`` on Windows);
        `baud` must match the Amiga side's own `BAUD` (default 19200,
        the same default both here and `AmiPilotServer`'s own
        `SERDEVICE`/`SERUNIT`/`BAUD` startup arguments use --
        server/README.md). `timeout` is the per-read timeout (seconds)
        -- a real serial link has no TCP-style "connection", so unlike
        `connect()` this doesn't bound how long the OPEN itself takes
        (opening a local device file is effectively instant); it bounds
        how long each subsequent read waits before raising
        `TimeoutError` (see `_SerialTransport`).

        Requires `pyserial`, an optional dependency (`pip install
        amipilot[serial]`) -- this project is stdlib-only otherwise
        (pyproject.toml), so the import is deferred to here rather than
        module level, and everything else keeps working with no
        pyserial installed at all."""
        try:
            import serial
        except ImportError as e:
            raise RuntimeError(
                "connect_serial() requires pyserial -- install with "
                "'pip install amipilot[serial]' (or 'pip install pyserial')"
            ) from e
        ser = serial.Serial(device, baud, timeout=timeout)
        return cls(_SerialTransport(ser))

    def close(self) -> None:
        close = getattr(self._t, "close", None)
        if close is not None:
            close()

    def command(self, line: str | bytes, payload: bytes | None = None) -> Reply:
        """Send one command line, return its Reply. The terminator is
        added here; passing a line containing one is an error.

        `payload`, if given, is sent as raw bytes immediately after the
        line -- the request-side mirror of how a Reply's own `.payload`
        already carries a length-prefixed binary body back. The line
        itself must declare `payload`'s exact length up front (FSPUT's
        own `<path> <byte-count>` grammar, server/WIRE.md) -- this
        method has no idea what the line means and does no such
        checking; it just sends `len(payload)` raw bytes right after
        the terminator, matching what the server's FSPUT handler reads
        off the wire before even looking at the request further."""
        if isinstance(line, str):
            line = line.encode("latin-1")
        if b"\n" in line or b"\r" in line:
            raise ValueError("command line must not contain a line terminator")
        if len(line) + 1 > MAX_LINE:
            raise ValueError(
                f"command line too long ({len(line) + 1} bytes including "
                f"the terminator, server max {MAX_LINE}): {line[:64]!r}..."
            )
        self._t.sendall(line + b"\n" + payload if payload else line + b"\n")

        header = self._read_line()
        parts = header.split()
        if len(parts) != 3 or parts[0] != b"RC":
            raise WireError(f"malformed response header: {header!r}")
        try:
            rc, count = int(parts[1]), int(parts[2])
        except ValueError:
            raise WireError(f"malformed response header: {header!r}") from None
        if count < 0:
            raise WireError(f"negative byte count: {header!r}")
        return Reply(rc, self._read_exact(count))

    def handshake(self) -> ServerInfo:
        """VERSION exchange; raises ProtocolMismatch unless the server
        speaks exactly this client's protocol (a client that doesn't
        recognise the number MUST disconnect rather than guess)."""
        reply = self.command("VERSION")
        if not reply.ok:
            raise WireError(f"VERSION failed: RC {reply.rc}")

        lines = reply.text.splitlines()
        fields = lines[0].split() if lines else []
        # "AMIPILOT <major>.<minor> PROTOCOL <n>"
        if len(fields) != 4 or fields[0] != "AMIPILOT" or fields[2] != "PROTOCOL":
            raise WireError(f"malformed VERSION payload: {reply.text!r}")
        try:
            protocol = int(fields[3])
        except ValueError:
            raise WireError(f"malformed VERSION payload: {reply.text!r}") from None
        if protocol != PROTOCOL:
            self.close()
            raise ProtocolMismatch(
                f"server speaks protocol {protocol}, this client speaks {PROTOCOL}"
            )

        info = ServerInfo(server_version=fields[1], protocol=protocol)
        for line in lines[1:]:
            words = line.split()
            if not words:
                continue
            if words[0] == "STABLE":
                info.stable = words[1:]
            elif words[0] == "EXPERIMENTAL":
                info.experimental = words[1:]
        return info

    def _recv(self) -> bytes:
        data = self._t.recv(4096)
        if not data:
            raise WireError("connection closed mid-response")
        return data

    def _read_line(self) -> bytes:
        while b"\n" not in self._buf:
            self._buf += self._recv()
        line, self._buf = self._buf.split(b"\n", 1)
        return line

    def _read_exact(self, count: int) -> bytes:
        while len(self._buf) < count:
            self._buf += self._recv()
        data, self._buf = self._buf[:count], self._buf[count:]
        return data
