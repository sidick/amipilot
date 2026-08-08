"""The AmiPilot object API: a Pythonic client over `WireClient`
(server/WIRE.md framing) matching the verb set `AmiPilotServer`
currently implements (TREE/CLICK/TYPE/GETTEXT/MANIFEST/LAUNCH/
FSLIST/FSSTAT/FSMKDIR/FSDELETE/FSGET/MENU/MENUPICK/DRAG/WAITFOR/
SCREENS/VERSION/QUIT -- see server/README.md; windows/list, find, and
fs-put are still future scope, not invented here ahead of the server
actually offering them).

Quoting matches the ARexx port's own command grammar (arexx_cmd.c):
window-pattern/path arguments containing a space, a literal '"', or a
literal '\' are double-quoted with '"' -> '\"' and '\' -> '\\' escaped
first -- the same backslash-escaping convention the server's own reply
side already uses (EscapeQuotesInto()/EscapeQuotes() in fs.c/
amipilotserver/main.c, unescaped host-side by model.py's unescape()),
now matched by the input side (arexx_cmd.c's read_token()) so a name
the server can safely REPORT can also be safely SENT back as an
argument.
"""

from __future__ import annotations

import socket
import time
from pathlib import Path

from .fs import FsEntry, parse_fs_entries, parse_fs_entry
from .golden import assert_golden
from .menu import MenuStrip, parse_menu_strip
from .model import Window, parse_tree
from .screen import Screen, parse_screens
from .screenshot import Screenshot
from .wire import RC_ERROR, RC_FAIL, RC_OK, RC_TIMEOUT, RC_WARN, Reply, ServerInfo, WireClient


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


class Timeout(AmipilotError):
    """RC 15 -- a WAITFOR/CLICK(expect=...) condition never became true
    within its timeout. Distinct from NotFound/ActionFailed: for
    CLICK(expect=...) specifically, the click itself already happened
    (see click()'s own docstring) -- this means the expected FOLLOW-ON
    effect didn't show up in time, not that the click failed to
    deliver or that a locator didn't match anything at all."""


class ActionFailed(AmipilotError):
    """RC 20 -- the action itself didn't deliver."""


_ERROR_CLASSES = {
    RC_WARN: NotFound,
    RC_ERROR: CommandError,
    RC_TIMEOUT: Timeout,
    RC_FAIL: ActionFailed,
}


def _quote(s: str) -> str:
    """Wraps `s` in double quotes if it needs them (contains a space,
    tab, '"', or '\\'), escaping '\\' -> '\\\\' and '"' -> '\\"' first
    so the server's read_token() (arexx_cmd.c) can decode it back to
    the original value exactly -- see this module's docstring."""
    if not any(c in s for c in ' \t"\\'):
        return s
    escaped = s.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def _screen_prefix(screen: str | None) -> str:
    """Renders the optional "SCREEN=<substring> " token every window-
    targeting verb accepts right after its own keyword (server/
    include/arexx_cmd.h) -- empty string when `screen` is None, so
    callers can just prepend the result unconditionally."""
    return f"SCREEN={_quote(screen)} " if screen is not None else ""


def _render_condition(condition: str) -> str:
    """Translates the "window:<pattern>" / "nowindow"[:<pattern>]
    mini-syntax (docs/implementation-plan.md's own `click(button,
    expect="window:Settings")` example) into the wire's own
    "WINDOW=<pattern>" / "NOWINDOW"[=<pattern>] token -- shared by
    click()/click_by_name()/click_by_role()'s `expect=` and
    `wait_for()`'s `condition`, since both ultimately compose the same
    server-side vocabulary (arexx_cmd.h's WAITFOR/CLICK EXPECT= doc
    comment). "window:<pattern>" always needs a pattern (a NEW window
    to find). "nowindow" alone (no pattern) is only valid for
    click()'s own `expect=` -- it means the window the click itself
    just acted on, checked by identity server-side, not a pattern
    search; wait_for()'s own standalone WAITFOR has no prior action to
    anchor an identity to, so its "nowindow:<pattern>" always needs
    one (enforced server-side, not here -- an omitted pattern on
    wait_for() reaches the server as bare NOWINDOW, which WAITFOR's
    own parser rejects as a syntax error, arexx_cmd.c's
    patternRequired)."""
    kind, _, pattern = condition.partition(":")
    kind = kind.strip().lower()
    if kind == "window":
        if not pattern:
            raise ValueError(f'expect="window:<pattern>" needs a pattern: {condition!r}')
        return f"WINDOW={_quote(pattern)}"
    if kind == "nowindow":
        return f"NOWINDOW={_quote(pattern)}" if pattern else "NOWINDOW"
    raise ValueError(
        f'unrecognised condition {condition!r} -- expected "window:<pattern>" '
        f'or "nowindow"[:<pattern>]'
    )


def _role_locator(role: str | None, label: str | None, index: int) -> str:
    """Renders the tier-2 "ROLE=<r> [LABEL=<l>] [INDEX=<n>]" gadget
    locator (server/include/arexx_cmd.h) in place of a bare numeric
    <gadget-id>. `label` is matched case-sensitively as a substring by
    the server (strstr, same convention as window/screen patterns --
    NOT case-insensitive). At least one of role/label is required --
    an empty locator is nonsensical and rejected here, client-side,
    rather than sent as a malformed request. `index` is only emitted
    when non-zero (0 is the server's own default -- the first match),
    keeping the wire line minimal for the common case."""
    if role is None and label is None:
        raise ValueError("click_by_role/type_by_role/get_text_by_role need at least one of role= or label=")
    parts = []
    if role is not None:
        parts.append(f"ROLE={_quote(role)}")
    if label is not None:
        parts.append(f"LABEL={_quote(label)}")
    if index:
        parts.append(f"INDEX={index}")
    return " ".join(parts)


# SECURITY: this is a PUBLIC default (it's in this open-source repo,
# and matches AmiPilotServer's own AMIP_TCP_DEFAULT_PASSWORD) -- it
# exists only so TCP works out of the box with zero config changes
# ("a token amount of security" against a blind/naive scan of an open
# port), not to actually protect anything. There is no TLS on this
# wire, so even a custom password crosses it in cleartext. Set a real
# TCPPASSWORD server-side (and pass the matching `password=` here) for
# any deployment that matters, and never expose AmiPilotServer's TCP
# transport on an open/internet-facing port regardless -- see
# server/README.md's TCP section.
DEFAULT_TCP_PASSWORD = "amipilot"


class Amipilot:
    """Wraps a connected WireClient. Construct via `Amipilot.connect()`
    or wrap an already-open `WireClient` directly."""

    def __init__(self, wire: WireClient):
        self._wire = wire
        self.info: ServerInfo | None = None

    @classmethod
    def connect(
        cls, host: str, port: int, timeout: float = 10.0, *, password: str = DEFAULT_TCP_PASSWORD
    ) -> "Amipilot":
        """A single connection attempt -- raises immediately on
        failure. Use `connect_with_retry()` instead for a link that
        may still be settling (a guest mid-boot, a fresh emulator
        bridge, a real cable with noise): raw connect/recv errors
        there are common and expected, not exceptional.

        Sends `AUTH {password}` right after the VERSION handshake, for
        every transport -- a harmless extra round trip on ARexx/
        serial.device, where nothing gates on it, but required for
        TCP unless the server was started without a TCPPASSWORD gate
        (it isn't, by default -- see DEFAULT_TCP_PASSWORD's own
        docstring: this is not real security, just a sane non-empty
        starting point). Raises CommandError (RC 10) if the password
        is wrong. Closes the socket before propagating any failure from
        the handshake or AUTH step (WireError, OSError, CommandError)
        -- a single connection attempt that fails must not leak the
        socket it just opened."""
        client = cls(WireClient.connect(host, port, timeout=timeout))
        try:
            client.handshake()
            client._run(f"AUTH {_quote(password)}")
        except BaseException:
            client.close()
            raise
        return client

    @classmethod
    def connect_serial(
        cls,
        device: str,
        baud: int = 19200,
        *,
        timeout: float = 10.0,
        password: str = DEFAULT_TCP_PASSWORD,
    ) -> "Amipilot":
        """The serial-port analog of `connect()` -- see
        `WireClient.connect_serial()` for `device`/`baud`/`timeout`
        (an OS-specific device path, matching the Amiga side's own
        `BAUD`, and a per-read timeout respectively). Sends `AUTH
        {password}` the same as `connect()` does for every transport
        -- harmless here since serial.device never gates on it
        server-side (see `connect()`'s own docstring), kept for one
        consistent connect sequence regardless of transport. Same
        close-on-failure guarantee as `connect()`: a failed handshake
        or AUTH never leaks the port it just opened.

        Requires `pyserial` (`pip install amipilot[serial]`) --
        raises `RuntimeError` immediately if it isn't installed, via
        `WireClient.connect_serial()`."""
        client = cls(WireClient.connect_serial(device, baud, timeout=timeout))
        try:
            client.handshake()
            client._run(f"AUTH {_quote(password)}")
        except BaseException:
            client.close()
            raise
        return client

    @classmethod
    def connect_with_retry(
        cls,
        host: str,
        port: int,
        deadline_seconds: float = 60.0,
        connect_timeout: float = 15.0,
        *,
        password: str = DEFAULT_TCP_PASSWORD,
    ) -> "Amipilot":
        """Tolerates two real, confirmed-live failure modes a fresh
        or flaky link can show (server/WIRE.md's transport is young
        enough that callers still hit these by hand rather than
        through a purpose-built retry path -- this is that path):

        `connect_timeout` does double duty, matching `WireClient.
        connect()`'s own `timeout` parameter: it bounds each individual
        connection attempt below, AND becomes the returned client's
        ongoing per-command socket read timeout for its whole
        lifetime (reset right before returning -- see the comment at
        that reset for why). Pass something comfortably larger than
        the longest `WAITFOR`/`CLICK(expect=...)` `TIMEOUT=` (or
        `wait_for()`/`click(expect=...)`'s own `timeout=`) any caller
        on this connection intends to use -- the default of 15s has
        headroom over the wire's own 10s default for both, but a
        caller setting `TIMEOUT=30` needs a matching `connect_timeout`
        or a legitimate server-side wait will surface as a raw socket
        `TimeoutError` instead of the clean RC-15 `Timeout` exception
        -- confirmed the hard way while building the stock-app
        conformance check (tests/copperline/stock-app-test.py): a
        `CLICK ... EXPECT=NOWINDOW TIMEOUT=10` genuinely answered by
        the server in ~10s still failed client-side, because this
        method's retry loop (below) leaves the socket's timeout
        wherever it last shrank it to (down to 0.1s near
        `deadline_seconds`) if this reset isn't applied.

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
                client._run(f"AUTH {_quote(password)}")
            except OSError as e:
                # Covers both steps -- the AUTH round trip is just as
                # exposed to the "bytes silently dropped, not buffered,
                # if the far end's transport wasn't fully ready yet"
                # failure mode described above as VERSION was, so it
                # gets the same retry-on-the-same-held-connection
                # treatment, not just the handshake.
                last_error = e
                continue
            except BaseException:
                # A wrong password (CommandError) or a malformed
                # handshake response (WireError) is a real,
                # non-retryable error, not a transient connection
                # hiccup -- but the socket is still open at this point
                # and would otherwise leak.
                client.close()
                raise
            # The loop above shrinks the socket's read timeout on every
            # iteration (down to as little as 0.1s near the deadline) so
            # a slow-to-answer handshake attempt can't itself blow past
            # `deadline_seconds` -- but that timeout is a SOCKET-level
            # setting, not a per-call one, and was still in effect on
            # this successful iteration. Left alone, a client returned
            # here would carry that leftover (<=3s) timeout into every
            # future command, silently breaking anything that
            # legitimately takes longer to answer -- a WAITFOR/
            # CLICK(expect=...) TIMEOUT= greater than a few seconds,
            # answered correctly by the server, would still surface as
            # a raw socket TimeoutError here instead of the clean RC-15
            # Timeout exception. Reset it to what the caller actually
            # asked for before handing the client back.
            sock.settimeout(connect_timeout)
            return client

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

    def _run(
        self,
        command: str,
        *,
        allow: tuple[int, ...] = (RC_OK,),
        payload: bytes | None = None,
    ) -> Reply:
        reply = self._wire.command(command, payload)
        if reply.rc not in allow:
            raise _ERROR_CLASSES.get(reply.rc, AmipilotError)(
                reply.rc, command, reply.text.strip() or "(no message)"
            )
        return reply

    # -- verbs ------------------------------------------------------

    def tree(self, window_pattern: str, *, screen: str | None = None) -> Window:
        """TREE <window-pattern> -- the matched window's full gadget
        model. Raises NotFound if no window matches.

        `screen`, if given, narrows the search to screens whose own
        name (DefaultTitle) contains it -- for disambiguating two
        same-titled windows on different screens; see `screens()`.
        Omitted searches every screen, same as before this parameter
        existed."""
        reply = self._run(f"TREE {_screen_prefix(screen)}{_quote(window_pattern)}")
        return parse_tree(reply.text)

    def assert_tree_matches(
        self,
        window_pattern: str,
        golden_path: str | Path,
        *,
        screen: str | None = None,
        update: bool = False,
    ) -> None:
        """One-line golden-tree assertion (docs/implementation-plan.md,
        "The inspector": "a saved dump doubles as a structural fixture
        -- 'this app's UI still has this shape'"): fetches the live
        tree() and compares it against the file at `golden_path`
        (created automatically the first time), raising
        golden.GoldenMismatch with a unified diff on any drift.
        `update=True` regenerates the file instead of comparing --
        use once a UI change is confirmed intentional, matching
        `amipilot dump --golden ... --update-golden`'s own CLI form
        of this same check."""
        window = self.tree(window_pattern, screen=screen)
        assert_golden(window, golden_path, update=update)

    def click(
        self,
        window_pattern: str,
        gadget_id: int,
        *,
        screen: str | None = None,
        expect: str | None = None,
        timeout: float = 10.0,
    ) -> None:
        """CLICK <window-pattern> <gadget-id> -- a genuine input.device
        click. Raises NotFound (no such window/gadget) or ActionFailed
        (event injection didn't deliver). `screen` narrows the window
        search the same way `tree()`'s does; the target screen is
        brought to front as part of the click regardless.

        `expect`, if given, composes the click atomically with a
        server-side wait -- "window:<pattern>" (a new window matching
        <pattern> appears) or "nowindow" (the window this click just
        acted on closes, checked by identity, not a pattern re-search
        -- see `_render_condition()`'s own docstring for why that's
        the precise "snapshot the delta" guarantee a naive
        click-then-poll can't give you). The click itself always still
        happens; a timeout waiting for `expect` raises `Timeout` (RC
        15), distinct from the click's own injection failing outright
        (`ActionFailed`, RC 20). `timeout` (seconds, default 10) only
        matters when `expect` is given."""
        suffix = ""
        if expect is not None:
            suffix = f" EXPECT={_render_condition(expect)} TIMEOUT={int(timeout)}"
        self._run(f"CLICK {_screen_prefix(screen)}{_quote(window_pattern)} {gadget_id}{suffix}")

    def click_by_name(
        self, name: str, *, expect: str | None = None, timeout: float = 10.0
    ) -> None:
        """CLICK @<name> -- the manifest-locator form; requires a
        manifest loaded first via `manifest()`. `expect`/`timeout` --
        see `click()`'s own docstring."""
        suffix = ""
        if expect is not None:
            suffix = f" EXPECT={_render_condition(expect)} TIMEOUT={int(timeout)}"
        self._run(f"CLICK @{name}{suffix}")

    def click_by_role(
        self,
        window_pattern: str,
        *,
        role: str | None = None,
        label: str | None = None,
        index: int = 0,
        screen: str | None = None,
        expect: str | None = None,
        timeout: float = 10.0,
    ) -> None:
        """CLICK <window-pattern> ROLE=<r> [LABEL=<l>] [INDEX=<n>] --
        the tier-2 semantic locator (docs/implementation-plan.md's
        "Locator tiers"): finds the gadget by role and/or label text
        instead of a numeric GA_ID, resolved live against the window
        at action time. At least one of `role`/`label` is required.
        `index` (0-based, default the first match) disambiguates when
        more than one gadget matches. Raises NotFound if the window or
        no matching gadget exists, ActionFailed if the click itself
        didn't deliver. `expect`/`timeout` -- see `click()`'s own
        docstring."""
        suffix = ""
        if expect is not None:
            suffix = f" EXPECT={_render_condition(expect)} TIMEOUT={int(timeout)}"
        self._run(
            f"CLICK {_screen_prefix(screen)}{_quote(window_pattern)} "
            f"{_role_locator(role, label, index)}{suffix}"
        )

    def type(
        self, window_pattern: str, gadget_id: int, text: str, *, screen: str | None = None
    ) -> None:
        """TYPE <window-pattern> <gadget-id> <text> -- clicks the
        gadget to focus it, then types via real IECLASS_RAWKEY events.
        `text` is sent verbatim after the gadget ID (server/README.md's
        rest-of-line rule); it must not contain a newline. `screen`
        narrows the window search the same way `tree()`'s does."""
        if "\n" in text or "\r" in text:
            raise ValueError("TYPE text must not contain a line terminator")
        self._run(f"TYPE {_screen_prefix(screen)}{_quote(window_pattern)} {gadget_id} {text}")

    def type_by_name(self, name: str, text: str) -> None:
        if "\n" in text or "\r" in text:
            raise ValueError("TYPE text must not contain a line terminator")
        self._run(f"TYPE @{name} {text}")

    def type_by_role(
        self,
        window_pattern: str,
        text: str,
        *,
        role: str | None = None,
        label: str | None = None,
        index: int = 0,
        screen: str | None = None,
    ) -> None:
        """TYPE <window-pattern> ROLE=<r> [LABEL=<l>] [INDEX=<n>] <text>
        -- the tier-2 locator form of `type()`; see `click_by_role()`
        for the locator semantics."""
        if "\n" in text or "\r" in text:
            raise ValueError("TYPE text must not contain a line terminator")
        self._run(
            f"TYPE {_screen_prefix(screen)}{_quote(window_pattern)} "
            f"{_role_locator(role, label, index)} {text}"
        )

    def get_text(self, window_pattern: str, gadget_id: int, *, screen: str | None = None) -> str:
        """GETTEXT <window-pattern> <gadget-id> -- a string/integer
        gadget's live value, else its label. `screen` narrows the
        window search the same way `tree()`'s does."""
        return self._run(
            f"GETTEXT {_screen_prefix(screen)}{_quote(window_pattern)} {gadget_id}"
        ).text

    def get_text_by_name(self, name: str) -> str:
        return self._run(f"GETTEXT @{name}").text

    def get_text_by_role(
        self,
        window_pattern: str,
        *,
        role: str | None = None,
        label: str | None = None,
        index: int = 0,
        screen: str | None = None,
    ) -> str:
        """GETTEXT <window-pattern> ROLE=<r> [LABEL=<l>] [INDEX=<n>] --
        the tier-2 locator form of `get_text()`; see `click_by_role()`
        for the locator semantics."""
        return self._run(
            f"GETTEXT {_screen_prefix(screen)}{_quote(window_pattern)} "
            f"{_role_locator(role, label, index)}"
        ).text

    def drag(
        self,
        window_pattern: str,
        gadget_id: int,
        dx: int,
        dy: int,
        *,
        screen: str | None = None,
    ) -> None:
        """DRAG <window-pattern> <gadget-id> <dx> <dy> -- a genuine
        press/move/release drag of the gadget's current center by a
        pixel offset. The natural shape for adjusting a slider/
        scroller (GadTools SLIDER_KIND/PROP_KIND), which is a delta
        operation. Raises NotFound (no such window/gadget) or
        ActionFailed (event injection didn't deliver). `screen`
        narrows the window search the same way `tree()`'s does."""
        self._run(
            f"DRAG {_screen_prefix(screen)}{_quote(window_pattern)} {gadget_id} {dx} {dy}"
        )

    def drag_by_name(self, name: str, dx: int, dy: int) -> None:
        """DRAG @<name> <dx> <dy> -- the manifest-locator form of
        `drag()`; requires a manifest loaded first via `manifest()`."""
        self._run(f"DRAG @{name} {dx} {dy}")

    def drag_to(
        self,
        window_pattern: str,
        src_gadget_id: int,
        dest_gadget_id: int,
        *,
        screen: str | None = None,
    ) -> None:
        """DRAG <window-pattern> <src-gadget-id> TO <dest-gadget-id> --
        drags src's current center onto dest's, both resolved live at
        action time and both in the same window -- zero coordinates in
        the caller's script, for drag-and-drop/reorder cases (e.g.
        dragging one listview item onto another). Raises NotFound (no
        such window/either gadget) or ActionFailed (event injection
        didn't deliver)."""
        self._run(
            f"DRAG {_screen_prefix(screen)}{_quote(window_pattern)} "
            f"{src_gadget_id} TO {dest_gadget_id}"
        )

    def drag_to_by_name(self, src_name: str, dest_name: str) -> None:
        """DRAG @<src-name> TO @<dest-name> -- the manifest-locator
        form of `drag_to()`; both names must resolve to the same
        window (the server rejects a cross-window destination
        explicitly -- server/src/amipilotserver/main.c's
        HandleCommand())."""
        self._run(f"DRAG @{src_name} TO @{dest_name}")

    def window_move(
        self, window_pattern: str, dx: int, dy: int, *, screen: str | None = None
    ) -> None:
        """WINDOWMOVE <window-pattern> <dx> <dy> -- moves the WHOLE
        window by a pixel offset, a genuine press/move/release drag of
        its own title bar (built on the same primitive `drag()` uses,
        not a new mechanism). `screen` narrows the window search the
        same way `tree()`'s does; no manifest-locator ("@name") form --
        this acts on a whole window, the same scope `tree()`/`menu()`
        already have, neither of which takes one either.

        Raises NotFound if no window matches, ActionFailed if the
        window has no drag bar at all (`WFLG_DRAGBAR` unset -- rare,
        but some windows are deliberately undraggable) or the event
        injection didn't deliver. There's no separate "get window
        position" call -- `tree()`'s own result already carries
        `Window.left`/`Window.top`."""
        self._run(
            f"WINDOWMOVE {_screen_prefix(screen)}{_quote(window_pattern)} {dx} {dy}"
        )

    def window_resize(
        self, window_pattern: str, width: int, height: int, *, screen: str | None = None
    ) -> None:
        """WINDOWSIZE <window-pattern> <width> <height> -- resizes the
        WHOLE window to an ABSOLUTE target size, a genuine drag of its
        own sizing gadget from its current bottom-right corner to
        wherever that corner needs to land. `screen` narrows the
        window search the same way `tree()`'s does; no manifest-
        locator form, same reasoning as `window_move()`.

        Raises NotFound if no window matches, ActionFailed if the
        window has no sizing gadget at all (`WFLG_SIZEGADGET` unset)
        or the event injection didn't deliver. Does NOT pre-check
        `width`/`height` against the window's own min/max -- Intuition
        clamps the drag exactly as it would a genuine user drag, so
        confirm the actual resulting size with a follow-up `tree()`
        call rather than assuming the exact target was reached, same
        "verify the real outcome" precedent `drag()` already sets."""
        self._run(
            f"WINDOWSIZE {_screen_prefix(screen)}{_quote(window_pattern)} {width} {height}"
        )

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

    def wb_launch(
        self,
        icon_path: str,
        *,
        tooltypes: dict[str, str] | None = None,
        args: list[str] | None = None,
    ) -> None:
        """WBLAUNCH <icon-path> [TOOLTYPE=<key>=<value> ...] [ARG=<path> ...]
        -- launches `icon_path` (a tool or project icon's path, WITHOUT
        the ".info" suffix) as if its icon had been double-clicked: a
        real, hand-built Workbench-style start (genuine WBStartup/
        WBArg message, real non-CLI process), not launch()'s Shell-
        style one (SystemTagList()). See server/include/wblaunch.h for
        the full mechanism, including why tooltype overrides need a
        scratch icon write (never to `icon_path`'s own real .info).

        `tooltypes`, if given, overrides (or appends, if the key isn't
        already on the icon) each named tooltype for this launch only
        -- everything else on the real icon is left alone. `args`, if
        given, are additional fully-qualified paths passed as further
        WBArg project-file arguments (the "multiple project files"
        case), appended after the primary tool/project argument(s).

        Asynchronous, like launch(): raises CommandError as soon as
        the icon/path is rejected (bad icon, unsupported icon type, an
        ARG=/TOOLTYPE= path that can't be locked), ActionFailed if
        CreateNewProc() itself fails (out of memory, no process slot)
        -- neither confirms the launched program actually finished
        starting; assert on its expected effect instead, same as
        launch()."""
        parts = [f"WBLAUNCH {_quote(icon_path)}"]
        for key, value in (tooltypes or {}).items():
            parts.append(f"TOOLTYPE={_quote(f'{key}={value}')}")
        for path in args or []:
            parts.append(f"ARG={_quote(path)}")
        self._run(" ".join(parts))

    def fs_list(self, path: str) -> list[FsEntry]:
        """FSLIST <path> -- lists a directory's entries. `path` must
        resolve inside a root granted to the server at startup via
        FSROOT (server/README.md's "File API"); anything else raises
        CommandError naming the granted roots. Raises NotFound if
        `path` doesn't exist, ActionFailed if it exists but isn't a
        directory."""
        return parse_fs_entries(self._run(f"FSLIST {_quote(path)}").text)

    def fs_stat(self, path: str) -> FsEntry:
        """FSSTAT <path> -- a single file or directory's metadata,
        without listing a directory's contents. Same allowlist and
        NotFound rules as fs_list()."""
        return parse_fs_entry(self._run(f"FSSTAT {_quote(path)}").text)

    def fs_mkdir(self, path: str) -> None:
        """FSMKDIR <path> -- creates a directory. `path` itself need
        not (must not) already exist, but its parent must, and must
        resolve inside a granted root -- a bare relative name with no
        volume/assign or `/` is rejected rather than guessed against
        some assumed current directory."""
        self._run(f"FSMKDIR {_quote(path)}")

    def fs_delete(self, path: str) -> None:
        """FSDELETE <path> -- deletes a file or empty directory (plain
        DeleteFile() semantics -- a non-empty directory fails). Same
        allowlist and NotFound rules as fs_list()."""
        self._run(f"FSDELETE {_quote(path)}")

    def fs_get(self, path: str) -> bytes:
        """FSGET <path> -- reads a file's full contents back as raw
        bytes (may contain embedded NULs; the wire's length-prefixed
        framing carries them intact, unlike the other verbs' NUL-
        terminated text payloads). The server caps this at its own
        internal buffer size (server/src/fs.c's AMIP_FS_BUF_SIZE, a
        test-staging channel, not a file manager) and raises
        ActionFailed for anything larger. See fs_put() for the
        opposite direction."""
        return self._run(f"FSGET {_quote(path)}").payload

    def fs_put(self, path: str, data: bytes, *, timeout: float = 30.0) -> None:
        """FSPUT <path> <byte-count> [TIMEOUT=<n>] -- writes `data` to
        `path`, creating it if it doesn't exist and overwriting it if
        it does. Same allowlist rules as the other fs_*() methods
        (raises CommandError if `path`'s parent isn't inside a granted
        FSROOT), and the same AMIP_FS_BUF_SIZE cap as fs_get() (server
        rejects an oversized declared byte-count outright, before ever
        reading the payload off the wire).

        Wire-only: there is genuinely no way to carry a raw binary
        request body over ARexx (RexxMsg/ARG0() only ever carries
        string arguments), so unlike every other verb here this one
        cannot be issued from an ARexx script -- only from a client
        connected over TCP or serial, which is what this method
        always is.

        `timeout` (seconds, default 30 -- longer than most other
        waits here, since a real multi-KB payload over a slow serial
        link genuinely needs it) bounds how long the server waits for
        the payload to fully arrive after the request line; raises
        Timeout (RC_TIMEOUT) if it doesn't, distinctly from the write
        itself failing (ActionFailed, e.g. disk full)."""
        self._run(
            f"FSPUT {_quote(path)} {len(data)} TIMEOUT={int(timeout)}",
            payload=data,
        )

    def menu(self, window_pattern: str, *, screen: str | None = None) -> MenuStrip:
        """MENU <window-pattern> -- the matched window's full menu
        strip: every pulldown menu, its items, and (one level deep)
        their submenu items, with the checkit/checked/enabled state
        and keyboard shortcut (if any) the walker read live off
        Intuition's own struct Menu/MenuItem chain. Raises NotFound if
        no window matches; a window with no menu strip at all returns
        a MenuStrip with an empty `menus` list, not an error. `screen`
        narrows the window search the same way `tree()`'s does."""
        reply = self._run(f"MENU {_screen_prefix(screen)}{_quote(window_pattern)}")
        return parse_menu_strip(reply.text)

    def menu_pick(
        self,
        window_pattern: str,
        menu_num: int,
        item_num: int,
        sub_num: int | None = None,
        *,
        screen: str | None = None,
    ) -> None:
        """MENUPICK <window-pattern> <menu-num> <item-num> [<sub-num>]
        -- selects a menu item via its keyboard shortcut (Right-Amiga
        + the shortcut character), the same input.device path a human
        pressing that combination would use. `menu_num`/`item_num`/
        `sub_num` are the same 0-based chain positions `menu()`'s
        MenuItem.menu_num/item_num/sub_num report -- use `menu(...).
        find("Some Label")` to look one up by text instead of
        hand-counting positions. `screen` narrows the window search
        the same way `tree()`'s does; the target screen is brought to
        front as part of the pick regardless.

        Raises NotFound if the window or the addressed item doesn't
        exist, ActionFailed if the item is disabled, has no keyboard
        shortcut (pointer-based menu navigation for shortcut-less
        items isn't built yet -- see server/README.md), or the
        keystroke injection itself failed. RC 0 confirms the keystroke
        was genuinely delivered to input.device; Intuition resolves it
        against the window's live menu strip on its own, so this
        doesn't (and can't) confirm the app's own IDCMP_MENUPICK
        handler ran -- assert on the expected effect instead."""
        cmd = f"MENUPICK {_screen_prefix(screen)}{_quote(window_pattern)} {menu_num} {item_num}"
        if sub_num is not None:
            cmd += f" {sub_num}"
        self._run(cmd)

    def screens(self) -> list[Screen]:
        """SCREENS -- every open screen (title, position, size, and
        whether it's frontmost). `title` is each screen's own name
        (DefaultTitle), not the dynamic title-bar text a window's own
        WA_ScreenTitle can override -- a stable identity to match
        against, same as the `screen` parameter above and `SCREEN=`
        itself use (server/include/action_engine.h has the full
        rationale)."""
        return parse_screens(self._run("SCREENS").text)

    def screenshot(self, *, screen: str | None = None, window: str | None = None) -> Screenshot:
        """SCREENSHOT [SCREEN=<substring>] [WINDOW=<pattern>] -- raw
        bitmap capture, planar or Picasso96/RTG (phase 1.0,
        `amipilot.screenshot`'s own module docstring has the full
        format/PNG/ILBM story). With both omitted, captures the
        frontmost/default public screen; `screen` alone selects a
        screen by `DefaultTitle` substring and captures it whole;
        `window`, resolved exactly like click()'s own window-pattern
        (optionally narrowed by `screen`), captures that window's
        OWNING SCREEN in full, with the window's own rectangle
        recorded on the returned `Screenshot.crop` for cropping --
        there's no separate per-window pixel buffer on classic
        Intuition (overlapping windows share one screen bitmap), so
        that's what a "window screenshot" actually is.

        A genuine Picasso96/RTG screen is captured automatically, in
        its own native pixel format, WHEN a real P96 board is present
        AND that specific screen is P96-backed -- never assumed, never
        required (`server/include/p96_compat.h`'s own header has the
        detection story); on this project's own tested floor (no P96
        installed) every capture is the classic planar path,
        unchanged. Check `.pixel_format`/`.rgb_format` on the result
        if it matters which path a given capture took.

        Raises NotFound if `screen`/`window` doesn't match anything,
        CommandError for a screen with no bitmap at all, a P96 YUV
        pixel format (not supported -- see screenshot.h's own header
        comment), or a capture too large for the server's own size
        cap. Call `.save(path)` on the result to write both a `.iff`
        (IFF ILBM) and a `.png` (`.save()`/`.to_ilbm()` raise
        ScreenshotParseError for a P96 truecolor/hicolor capture, which
        ILBM has no way to represent -- use `.to_png()`/`.to_rgb888()`
        directly for those)."""
        parts = ["SCREENSHOT"]
        if screen is not None:
            parts.append(f"SCREEN={_quote(screen)}")
        if window is not None:
            parts.append(f"WINDOW={_quote(window)}")
        reply = self._run(" ".join(parts))
        return Screenshot.parse(reply.payload)

    def wait_for_window(
        self,
        window_pattern: str,
        *,
        screen: str | None = None,
        timeout: float = 20.0,
        poll_interval: float = 0.5,
    ) -> Window:
        """Polls `tree()` until it stops raising NotFound or `timeout`
        elapses -- promotes the poll loop LAUNCH's own docstring
        recommends ("assert on the expected effect instead, e.g.
        polling TREE for the launched app's window") into a reusable
        method rather than every caller hand-rolling it, the way
        tests/copperline/launch-test.py originally did. Raises
        TimeoutError (not NotFound) once the deadline passes, chained
        from the last NotFound via `__cause__`.

        Host-side polling (one `TREE` round trip per `poll_interval`)
        -- for waiting on something not tied to an action THIS client
        just took (e.g. an externally-launched or `launch()`ed
        process's own window appearing, with no click of yours to
        anchor to). If you just called `click()` and want to wait for
        its effect, prefer `click(..., expect="window:<pattern>")` or
        `wait_for("window:<pattern>")` instead -- a single server-side
        round trip with a much tighter poll interval, not N host round
        trips at this method's own 0.5s default."""
        deadline = time.monotonic() + timeout
        last_error: NotFound | None = None
        while time.monotonic() < deadline:
            try:
                return self.tree(window_pattern, screen=screen)
            except NotFound as e:
                last_error = e
                time.sleep(poll_interval)
        raise TimeoutError(
            f"no window matching {window_pattern!r} "
            f"{'on screen ' + repr(screen) + ' ' if screen else ''}"
            f"appeared within {timeout:.0f}s"
        ) from last_error

    def wait_for_screen(
        self, screen_pattern: str, *, timeout: float = 20.0, poll_interval: float = 0.5
    ) -> Screen:
        """Polls `screens()` until one whose title (DefaultTitle)
        contains `screen_pattern` shows up, or `timeout` elapses --
        same shape as `wait_for_window()`, for the case of waiting on
        a whole screen to open (e.g. an asynchronously `launch()`ed
        program that opens its own custom screen) rather than a
        window on an already-open one. Filtering is done here, host-
        side: SCREENS itself always returns every screen, same as
        `screens()`. Raises TimeoutError once the deadline passes. No
        server-side equivalent exists for screens (WAITFOR/EXPECT=
        only understand WINDOW=/NOWINDOW= -- see `wait_for()`'s own
        docstring), so this stays the only way to wait for one."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for screen in self.screens():
                if screen_pattern in screen.title:
                    return screen
            time.sleep(poll_interval)
        raise TimeoutError(
            f"no screen matching {screen_pattern!r} appeared within {timeout:.0f}s"
        )

    def wait_for(
        self, condition: str, *, screen: str | None = None, timeout: float = 10.0
    ) -> None:
        """WAITFOR [SCREEN=<substring>] <condition> [TIMEOUT=<n>] --
        polls entirely server-side (one wire round trip, not a host
        loop) until `condition` becomes true or `timeout` elapses.

        `condition` is "window:<pattern>" (a window matching <pattern>
        appears) or "nowindow:<pattern>" (no window matches <pattern>
        -- always a fresh pattern re-search each poll, since a
        standalone WAITFOR has no prior action to anchor an exact
        window identity to; if you need "the window my last click()
        acted on has closed" specifically, use
        `click(..., expect="nowindow")` instead, which checks by
        identity, not by pattern -- see `click()`'s own docstring).

        Raises `Timeout` (RC 15) once `timeout` elapses. This is the
        general-purpose wait primitive for anything not already tied
        to an action you just took; `wait_for_window()`/
        `wait_for_screen()` remain the right tool for polling for a
        window/screen with no accompanying action (they also cover
        screens, which WAITFOR's condition vocabulary doesn't). For
        waiting on a GADGET's text/state rather than a window, use
        `wait_for_text()`/`wait_for_text_by_role()`/
        `wait_for_text_by_name()` instead -- that condition needs a
        gadget locator this method's simple `condition` string doesn't
        carry."""
        self._run(
            f"WAITFOR {_screen_prefix(screen)}{_render_condition(condition)} "
            f"TIMEOUT={int(timeout)}"
        )

    def wait_for_text(
        self,
        window_pattern: str,
        gadget_id: int,
        text: str,
        *,
        screen: str | None = None,
        timeout: float = 10.0,
    ) -> None:
        """WAITFOR <window-pattern> <gadget-id> TEXT=<value> [TIMEOUT=<n>]
        -- polls server-side until the gadget's text (`get_text()`'s
        own value-or-label convention) EXACTLY equals `text`, or
        `timeout` elapses. Raises NotFound if the window/gadget
        doesn't exist, `Timeout` (RC 15) if the text never matches in
        time. WAITFOR-only -- `click()`'s own `expect=` doesn't have a
        text form, since the gadget whose text changes after a click
        is often a different gadget than the one clicked (see
        `click()`'s own docstring)."""
        self._run(
            f"WAITFOR {_screen_prefix(screen)}{_quote(window_pattern)} {gadget_id} "
            f"TEXT={_quote(text)} TIMEOUT={int(timeout)}"
        )

    def wait_for_text_by_name(self, name: str, text: str, *, timeout: float = 10.0) -> None:
        """WAITFOR @<name> TEXT=<value> [TIMEOUT=<n>] -- the manifest-
        locator form of `wait_for_text()`; requires a manifest loaded
        first via `manifest()`."""
        self._run(f"WAITFOR @{name} TEXT={_quote(text)} TIMEOUT={int(timeout)}")

    def wait_for_text_by_role(
        self,
        window_pattern: str,
        text: str,
        *,
        role: str | None = None,
        label: str | None = None,
        index: int = 0,
        screen: str | None = None,
        timeout: float = 10.0,
    ) -> None:
        """WAITFOR <window-pattern> ROLE=<r> [LABEL=<l>] [INDEX=<n>]
        TEXT=<value> [TIMEOUT=<n>] -- the tier-2 locator form of
        `wait_for_text()`; see `click_by_role()` for the locator
        semantics."""
        self._run(
            f"WAITFOR {_screen_prefix(screen)}{_quote(window_pattern)} "
            f"{_role_locator(role, label, index)} TEXT={_quote(text)} TIMEOUT={int(timeout)}"
        )

    def mui_command(
        self, app_base: str, command: str, *, timeout: float = 10.0
    ) -> str:
        """MUIREXX <app-base> [TIMEOUT=<n>] <command...> -- the MUI-
        ARexx bridge tier (docs/implementation-plan.md's "Locator
        tiers": every MUI application carries an automatic ARexx
        port). Sends `command` verbatim to the target application's
        own ARexx port (found by `app_base`, its
        `MUIA_Application_Base`, e.g. `"MUIDEMO"`) exactly as an ARexx
        script's own `ADDRESS` would, and returns whatever string
        result it replied with (empty string if none).

        This is a genuinely different mechanism from `click()`/
        `type()`/`get_text()` -- no structural walk, no input.device
        synthesis -- and a fundamentally different contract: MUI's own
        BUILT-IN ARexx support is a small, universal command set
        (`quit`/`hide`/`show`/`activate`/`deactivate`/`info <item>`/
        `help [file]` -- confirmed live against AmigaOS 3.2's own
        MUI-Demo), not a generic "read/write this widget's value"
        mechanism the way GA_ID or tier-2 ROLE=/LABEL=/INDEX= are for
        classic gadgets. Anything richer than the universal set is
        entirely up to the target application having registered its
        own commands (`MUIA_Application_Commands`) -- this method
        passes whatever you give it through unchanged; it can't
        invent commands an app doesn't have. `quit` is the one command
        every MUI app answers, useful for the same "exit via the
        app's own affordances" teardown discipline CLAUDE.md's other
        verbs already follow.

        Raises `NotFound` if no ARexx port exists for `app_base`
        (tries the bare base name, then `"<app_base>.1"` -- both are
        real, observed conventions), `Timeout` if sent but no reply
        arrived within `timeout` seconds, `CommandError` if the
        target replied but its OWN command handler reported a nonzero
        result code (an application-level failure this bridge relays
        rather than reinterprets -- `.message` on the exception starts
        with that numeric code), and `ActionFailed` if the server
        itself couldn't even allocate the ARexx message (out of
        memory, not this application's fault)."""
        return self._run(
            f"MUIREXX {_quote(app_base)} TIMEOUT={int(timeout)} {command}"
        ).text

    def quit(self) -> None:
        """QUIT -- shuts the server down cleanly. The connection is
        still open afterward (the server replies before exiting); call
        close() when done."""
        self._run("QUIT")
