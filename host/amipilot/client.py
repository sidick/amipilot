"""The AmiPilot object API: a Pythonic client over `WireClient`
(server/WIRE.md framing) matching the verb set `AmiPilotServer`
currently implements (TREE/CLICK/TYPE/GETTEXT/MANIFEST/VERSION/QUIT --
see server/README.md; windows/list, find, drag, menu-pick, wait-for,
launch, and the file API are 0.4 scope, not invented here ahead of the
server actually offering them).

Quoting matches the ARexx port's own command grammar (arexx_cmd.c):
window-pattern arguments containing a space must be double-quoted; a
pattern's own internal quotes are not supported (the server-side parser
has no escaping) -- same limitation, not a new one.
"""

from __future__ import annotations

import socket
import time

from .model import Window, parse_tree
from .wire import RC_ERROR, RC_FAIL, RC_OK, RC_WARN, Reply, ServerInfo, WireClient


class AmipilotError(Exception):
    """Base for all client-side errors surfaced from a non-OK RC."""

    def __init__(self, rc: int, command: str, message: str):
        super().__init__(f"{command!r} failed (RC {rc}): {message}")
        self.rc = rc
        self.command = command
        self.message = message


class NotFound(AmipilotError):
    """RC 5 -- well-formed command, nothing matched (window/gadget)."""


class CommandError(AmipilotError):
    """RC 10 -- bad syntax, unknown command, or bad locator."""


class ActionFailed(AmipilotError):
    """RC 20 -- the action itself didn't deliver."""


_ERROR_CLASSES = {RC_WARN: NotFound, RC_ERROR: CommandError, RC_FAIL: ActionFailed}


def _quote(s: str) -> str:
    if " " in s or "\t" in s:
        if '"' in s:
            raise ValueError(f"cannot quote a pattern containing '\"': {s!r}")
        return f'"{s}"'
    return s


class Amipilot:
    """Wraps a connected WireClient. Construct via `Amipilot.connect()`
    or wrap an already-open `WireClient` directly."""

    def __init__(self, wire: WireClient):
        self._wire = wire
        self.info: ServerInfo | None = None

    @classmethod
    def connect(cls, host: str, port: int, timeout: float = 10.0) -> "Amipilot":
        """A single connection attempt -- raises immediately on
        failure. Use `connect_with_retry()` instead for a link that
        may still be settling (a guest mid-boot, a fresh emulator
        bridge, a real cable with noise): raw connect/recv errors
        there are common and expected, not exceptional."""
        client = cls(WireClient.connect(host, port, timeout=timeout))
        client.handshake()
        return client

    @classmethod
    def connect_with_retry(
        cls,
        host: str,
        port: int,
        deadline_seconds: float = 60.0,
        connect_timeout: float = 5.0,
    ) -> "Amipilot":
        """Tolerates two real, confirmed-live failure modes a fresh
        or flaky link can show (server/WIRE.md's transport is young
        enough that callers still hit these by hand rather than
        through a purpose-built retry path -- this is that path):

        1. The TCP connect itself may be transiently refused or reset
           while the far end is still starting up (a guest mid-boot,
           an emulator bridge settling). Retried with a short pause
           between attempts.
        2. Once connected, the first command sent may go unanswered
           -- if the far end's own transport wasn't fully ready the
           instant the socket connected, the bytes can be silently
           dropped rather than buffered (confirmed against a real
           Copperline hostsocket bridge, 2026-08-06). The fix is to
           keep re-sending VERSION on the SAME held connection rather
           than reconnecting: reconnecting risks a genuine in-flight
           reply landing on a socket nothing is reading from anymore.

        Raises TimeoutError with a clear, single-line reason once
        `deadline_seconds` elapses -- never a raw socket traceback --
        chained from the last underlying error via `__cause__` for
        anyone who wants the detail.
        """
        deadline = time.monotonic() + deadline_seconds
        sock: socket.socket | None = None
        last_error: Exception | None = None

        while time.monotonic() < deadline:
            try:
                sock = socket.create_connection((host, port), timeout=connect_timeout)
                break
            except OSError as e:
                last_error = e
                time.sleep(0.5)
        if sock is None:
            raise TimeoutError(
                f"could not reach the wire transport at {host}:{port} "
                f"within {deadline_seconds:.0f}s -- is the server running "
                f"and the address/port correct?"
            ) from last_error

        client = cls(WireClient(sock))
        while time.monotonic() < deadline:
            sock.settimeout(min(max(deadline - time.monotonic(), 0.1), 3.0))
            try:
                client.handshake()
                return client
            except OSError as e:
                last_error = e

        sock.close()
        raise TimeoutError(
            f"connected to {host}:{port} but the server never answered "
            f"VERSION within {deadline_seconds:.0f}s -- it may still be "
            f"starting up, or something upstream is dropping traffic"
        ) from last_error

    def close(self) -> None:
        self._wire.close()

    def __enter__(self) -> "Amipilot":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def handshake(self) -> ServerInfo:
        self.info = self._wire.handshake()
        return self.info

    def _run(self, command: str, *, allow: tuple[int, ...] = (RC_OK,)) -> Reply:
        reply = self._wire.command(command)
        if reply.rc not in allow:
            raise _ERROR_CLASSES.get(reply.rc, AmipilotError)(
                reply.rc, command, reply.text.strip() or "(no message)"
            )
        return reply

    # -- verbs ------------------------------------------------------

    def tree(self, window_pattern: str) -> Window:
        """TREE <window-pattern> -- the matched window's full gadget
        model. Raises NotFound if no window matches."""
        reply = self._run(f"TREE {_quote(window_pattern)}")
        return parse_tree(reply.text)

    def click(self, window_pattern: str, gadget_id: int) -> None:
        """CLICK <window-pattern> <gadget-id> -- a genuine input.device
        click. Raises NotFound (no such window/gadget) or ActionFailed
        (event injection didn't deliver)."""
        self._run(f"CLICK {_quote(window_pattern)} {gadget_id}")

    def click_by_name(self, name: str) -> None:
        """CLICK @<name> -- the manifest-locator form; requires a
        manifest loaded first via `manifest()`."""
        self._run(f"CLICK @{name}")

    def type(self, window_pattern: str, gadget_id: int, text: str) -> None:
        """TYPE <window-pattern> <gadget-id> <text> -- clicks the
        gadget to focus it, then types via real IECLASS_RAWKEY events.
        `text` is sent verbatim after the gadget ID (server/README.md's
        rest-of-line rule); it must not contain a newline."""
        if "\n" in text or "\r" in text:
            raise ValueError("TYPE text must not contain a line terminator")
        self._run(f"TYPE {_quote(window_pattern)} {gadget_id} {text}")

    def type_by_name(self, name: str, text: str) -> None:
        if "\n" in text or "\r" in text:
            raise ValueError("TYPE text must not contain a line terminator")
        self._run(f"TYPE @{name} {text}")

    def get_text(self, window_pattern: str, gadget_id: int) -> str:
        """GETTEXT <window-pattern> <gadget-id> -- a string/integer
        gadget's live value, else its label."""
        return self._run(f"GETTEXT {_quote(window_pattern)} {gadget_id}").text

    def get_text_by_name(self, name: str) -> str:
        return self._run(f"GETTEXT @{name}").text

    def manifest(self, path: str) -> str:
        """MANIFEST <path> -- loads (replacing any previous) manifest
        on the server, enabling the @name locator forms. Returns the
        server's load-report text."""
        return self._run(f"MANIFEST {path}").text

    def launch(self, command: str, *, stack: int | None = None) -> None:
        """LAUNCH [STACK=n] <command-line> -- starts `command` as an
        AmigaDOS process (SystemTagList(), asynchronous), so it's
        useful for test setup: connect, then launch the fixture under
        test over the same session instead of pre-staging it via
        S:User-Startup. `command` is Shell syntax handed to the server
        verbatim (server/WIRE.md/server/README.md), so it must not
        itself contain a line terminator.

        `stack` sets the new process's stack size in bytes (AmigaDOS's
        own CreateNewProc() default is 4000 if omitted) -- pass this
        for anything that needs more than the default, e.g. most
        Intuition/ReAction GUI apps.

        Raises ActionFailed if the launch itself couldn't happen (out
        of memory, no process slot). This does NOT confirm the command
        was found or ran successfully -- an asynchronous launch
        returns before the shell has resolved the command name, and
        AmiPilotServer doesn't capture its output (yet); assert on the
        expected effect instead (e.g. `tree()` finding its window)."""
        if "\n" in command or "\r" in command:
            raise ValueError("LAUNCH command must not contain a line terminator")
        if stack is not None:
            self._run(f"LAUNCH STACK={stack} {command}")
        else:
            self._run(f"LAUNCH {command}")

    def quit(self) -> None:
        """QUIT -- shuts the server down cleanly. The connection is
        still open afterward (the server replies before exiting); call
        close() when done."""
        self._run("QUIT")
