"""The AmiPilot object API: a Pythonic client over `WireClient`
(server/WIRE.md framing) matching the verb set `AmiPilotServer`
currently implements (TREE/CLICK/TYPE/GETTEXT/MANIFEST/LAUNCH/
FSLIST/FSSTAT/FSMKDIR/FSDELETE/FSGET/MENU/MENUPICK/DRAG/SCREENS/
VERSION/QUIT -- see server/README.md; windows/list, find, and fs-put
are still 0.4+ scope, not invented here ahead of the server actually
offering them).

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

from .fs import FsEntry, parse_fs_entries, parse_fs_entry
from .menu import MenuStrip, parse_menu_strip
from .model import Window, parse_tree
from .screen import Screen, parse_screens
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
    def connect_with_retry(
        cls,
        host: str,
        port: int,
        deadline_seconds: float = 60.0,
        connect_timeout: float = 5.0,
        *,
        password: str = DEFAULT_TCP_PASSWORD,
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

    def _run(self, command: str, *, allow: tuple[int, ...] = (RC_OK,)) -> Reply:
        reply = self._wire.command(command)
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

    def click(self, window_pattern: str, gadget_id: int, *, screen: str | None = None) -> None:
        """CLICK <window-pattern> <gadget-id> -- a genuine input.device
        click. Raises NotFound (no such window/gadget) or ActionFailed
        (event injection didn't deliver). `screen` narrows the window
        search the same way `tree()`'s does; the target screen is
        brought to front as part of the click regardless."""
        self._run(f"CLICK {_screen_prefix(screen)}{_quote(window_pattern)} {gadget_id}")

    def click_by_name(self, name: str) -> None:
        """CLICK @<name> -- the manifest-locator form; requires a
        manifest loaded first via `manifest()`."""
        self._run(f"CLICK @{name}")

    def click_by_role(
        self,
        window_pattern: str,
        *,
        role: str | None = None,
        label: str | None = None,
        index: int = 0,
        screen: str | None = None,
    ) -> None:
        """CLICK <window-pattern> ROLE=<r> [LABEL=<l>] [INDEX=<n>] --
        the tier-2 semantic locator (docs/implementation-plan.md's
        "Locator tiers"): finds the gadget by role and/or label text
        instead of a numeric GA_ID, resolved live against the window
        at action time. At least one of `role`/`label` is required.
        `index` (0-based, default the first match) disambiguates when
        more than one gadget matches. Raises NotFound if the window or
        no matching gadget exists, ActionFailed if the click itself
        didn't deliver."""
        self._run(
            f"CLICK {_screen_prefix(screen)}{_quote(window_pattern)} "
            f"{_role_locator(role, label, index)}"
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
        ActionFailed for anything larger. There is no fs_put() --
        writing files host-to-Amiga needs a binary request-body
        mechanism the wire doesn't have yet (deferred, see the plan's
        "File system access" section)."""
        return self._run(f"FSGET {_quote(path)}").payload

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
        from the last NotFound via `__cause__`."""
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
        `screens()`. Raises TimeoutError once the deadline passes."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for screen in self.screens():
                if screen_pattern in screen.title:
                    return screen
            time.sleep(poll_interval)
        raise TimeoutError(
            f"no screen matching {screen_pattern!r} appeared within {timeout:.0f}s"
        )

    def quit(self) -> None:
        """QUIT -- shuts the server down cleanly. The connection is
        still open afterward (the server replies before exiting); call
        close() when done."""
        self._run("QUIT")
