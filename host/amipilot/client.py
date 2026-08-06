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
        client = cls(WireClient.connect(host, port, timeout=timeout))
        client.handshake()
        return client

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

    def quit(self) -> None:
        """QUIT -- shuts the server down cleanly. The connection is
        still open afterward (the server replies before exiting); call
        close() when done."""
        self._run("QUIT")
