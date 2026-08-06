"""Tests for amipilot.pytest_plugin.

Two layers: `pytester`-driven end-to-end checks that the plugin's ini/
CLI options are wired correctly and the `amipilot` fixture SKIPS
cleanly (not errors) when unconfigured -- the same "skip, don't fake a
pass" contract `make test-target` uses without a Kickstart/Workbench
config; and monkeypatched unit tests of the boot/connect helpers,
standing in for a real Copperline process so this suite runs with no
emulator, matching every other file in host/tests.
"""

import os
import socket
import subprocess
import time

import pytest

from amipilot.pytest_plugin import (  # noqa: E402
    _DEFAULT_COPPERLINE_ARGS,
    _boot_copperline,
    _connect_with_retry,
)

HOST_DIR = os.path.join(os.path.dirname(__file__), "..")


class FakeConfig:
    """Mimics real pytest.Config's split behaviour closely enough to
    catch the bug class this module already hit once: getoption()
    raises AttributeError for a name that was never registered via
    addoption() -- it does NOT fall back to None like a plain dict.get
    would. (amipilot_copperline_args is ini-only, no matching CLI flag;
    _boot_copperline learned this the hard way against a real
    Copperline run before this test existed -- see git history.)"""

    def __init__(self, options, ini):
        self._options = options
        self._ini = ini

    def getoption(self, name):
        if name not in self._options:
            raise AttributeError(f"no such option: {name}")
        return self._options[name]

    def getini(self, name):
        return self._ini.get(name)


class FakeRequest:
    def __init__(self, options, ini=None):
        self.config = FakeConfig(options, ini or {})


class FakeTmpPathFactory:
    def __init__(self, path):
        self._path = path

    def mktemp(self, _name):
        return self._path


def _request(tmp_path, **overrides):
    options = {
        "--amipilot-config": str(tmp_path / "cfg.toml"),
        "--amipilot-copperline": "fake-copperline",
        "--amipilot-copperline-ctl": "fake-ctl",
        "--amipilot-wire-port": 1234,
        "--amipilot-boot-timeout": 0.3,
    }
    options.update(overrides)
    ini = {"amipilot_copperline_args": _DEFAULT_COPPERLINE_ARGS,
           "amipilot_config": None}
    return FakeRequest(options, ini)


class TestBootCopperline:
    def test_skips_when_no_config_set(self, tmp_path):
        request = _request(tmp_path, **{"--amipilot-config": None})
        with pytest.raises(pytest.skip.Exception, match="no AmiPilot Copperline config"):
            _boot_copperline(request, FakeTmpPathFactory(tmp_path))

    def test_skips_when_config_file_missing(self, tmp_path):
        request = _request(tmp_path)  # cfg.toml deliberately never created
        with pytest.raises(pytest.skip.Exception, match="config not found"):
            _boot_copperline(request, FakeTmpPathFactory(tmp_path))

    def test_raises_if_process_exits_before_info_file(self, tmp_path, monkeypatch):
        (tmp_path / "cfg.toml").write_text("")

        class DeadProc:
            def __init__(self, *a, **k):
                self.returncode = 1

            def poll(self):
                return self.returncode

        monkeypatch.setattr(subprocess, "Popen", DeadProc)
        request = _request(tmp_path)
        with pytest.raises(RuntimeError, match="copperline exited"):
            _boot_copperline(request, FakeTmpPathFactory(tmp_path))

    def test_raises_timeout_if_info_file_never_appears(self, tmp_path, monkeypatch):
        (tmp_path / "cfg.toml").write_text("")

        class HangingProc:
            def __init__(self, *a, **k):
                self.terminated = False

            def poll(self):
                return None  # still running, never writes the info file

            def terminate(self):
                self.terminated = True

        monkeypatch.setattr(subprocess, "Popen", HangingProc)
        request = _request(tmp_path)
        with pytest.raises(TimeoutError, match="control-info file"):
            _boot_copperline(request, FakeTmpPathFactory(tmp_path))


class FakeSocket:
    """A connected-socket stand-in exercising the real path
    _connect_with_retry uses post-connect: settimeout/sendall/recv/
    close, no TCP involved. `response` is what a VERSION command gets
    back, fed to recv() in the chunks listed in `chunks` (default: all
    at once)."""

    def __init__(self, response: bytes, chunks=None):
        self._response = response
        self._chunks = list(chunks) if chunks is not None else [response]
        self.closed = False

    def settimeout(self, _t):
        pass

    def sendall(self, _data):
        pass

    def recv(self, _n):
        if not self._chunks:
            # Matches real socket.settimeout() expiry (a TimeoutError,
            # an OSError subclass) -- the actual failure mode when the
            # guest hasn't answered yet, not a closed-connection EOF.
            raise TimeoutError("timed out")
        return self._chunks.pop(0)

    def close(self):
        self.closed = True


VERSION_PAYLOAD = (
    b"AMIPILOT 0.3 PROTOCOL 1\n"
    b"STABLE VERSION\n"
    b"EXPERIMENTAL TREE CLICK TYPE GETTEXT MANIFEST QUIT\n"
)


class TestConnectWithRetry:
    """Covers the real shape: retry only the TCP connect (cheap, and
    the only step that's expected to transiently fail before Copperline
    is listening), then hold that ONE connection for the handshake --
    see _connect_with_retry's own docstring for why reconnecting per
    attempt is wrong (it can strand the guest's reply on an abandoned
    socket, confirmed against a real Copperline boot)."""

    def test_succeeds_after_transient_connect_refusals(self, monkeypatch):
        attempts = {"n": 0}
        fake_sock = FakeSocket(b"RC 0 %d\n%s" % (len(VERSION_PAYLOAD), VERSION_PAYLOAD))

        def fake_create_connection(addr, timeout=10.0):
            attempts["n"] += 1
            if attempts["n"] < 3:
                raise OSError("connection refused")
            return fake_sock

        monkeypatch.setattr(socket, "create_connection", fake_create_connection)
        monkeypatch.setattr(time, "sleep", lambda _s: None)

        client = _connect_with_retry("127.0.0.1", 1234, time.monotonic() + 5)
        assert attempts["n"] == 3
        assert client.info.protocol == 1
        assert not fake_sock.closed  # the handshake connection is kept, not torn down

    def test_reuses_the_same_socket_for_the_handshake_not_a_new_one(self, monkeypatch):
        """The bug this guards: an earlier version reconnected on every
        attempt, including after a successful TCP connect whose
        handshake just hadn't arrived yet -- which can abandon the
        socket the guest's reply actually lands on. create_connection
        must be called exactly once here, not once per handshake
        timeout."""
        calls = {"n": 0}
        fake_sock = FakeSocket(b"RC 0 %d\n%s" % (len(VERSION_PAYLOAD), VERSION_PAYLOAD))

        def fake_create_connection(addr, timeout=10.0):
            calls["n"] += 1
            return fake_sock

        monkeypatch.setattr(socket, "create_connection", fake_create_connection)
        monkeypatch.setattr(time, "sleep", lambda _s: None)

        _connect_with_retry("127.0.0.1", 1234, time.monotonic() + 5)
        assert calls["n"] == 1

    def test_raises_timeout_when_connect_never_succeeds(self, monkeypatch):
        def always_refuses(addr, timeout=10.0):
            raise OSError("connection refused")

        monkeypatch.setattr(socket, "create_connection", always_refuses)
        monkeypatch.setattr(time, "sleep", lambda _s: None)

        with pytest.raises(TimeoutError, match="could not reach the wire transport"):
            _connect_with_retry("127.0.0.1", 1234, time.monotonic() - 1)

    def test_raises_timeout_when_handshake_never_completes(self, monkeypatch):
        fake_sock = FakeSocket(b"", chunks=[])  # recv() always returns b"" (EOF-like)

        monkeypatch.setattr(socket, "create_connection", lambda addr, timeout=10.0: fake_sock)
        monkeypatch.setattr(time, "sleep", lambda _s: None)

        with pytest.raises(TimeoutError, match="handshake never completed"):
            _connect_with_retry("127.0.0.1", 1234, time.monotonic() + 0.1)
        assert fake_sock.closed  # the abandoned connection is cleaned up on this path


class TestFixtureWiring:
    """End-to-end via pytester: proves pytest_addoption/addini and the
    `amipilot` fixture's skip path work exactly the way a real consumer
    sees them -- auto-loaded via the pytest11 entry point (this test
    process has host/ editable-installed, same as CI's test-host), not
    force-loaded with `-p`."""

    def test_skips_cleanly_without_amipilot_config(self, pytester):
        pytester.syspathinsert(HOST_DIR)
        pytester.makepyfile(
            test_it="def test_uses_amipilot(amipilot):\n    assert False\n"
        )
        result = pytester.runpytest("-rs")
        result.assert_outcomes(skipped=1)
        result.stdout.fnmatch_lines(["*no AmiPilot Copperline config configured*"])

    def test_skips_cleanly_with_missing_config_path(self, pytester):
        pytester.syspathinsert(HOST_DIR)
        pytester.makepyfile(
            test_it="def test_uses_amipilot(amipilot):\n    assert False\n"
        )
        result = pytester.runpytest(
            "-rs", "--amipilot-config=/no/such/amipilot-config.toml",
        )
        result.assert_outcomes(skipped=1)
        result.stdout.fnmatch_lines(["*amipilot config not found*"])

    def test_ini_option_is_also_honoured(self, pytester):
        pytester.syspathinsert(HOST_DIR)
        pytester.makeini(
            "[pytest]\namipilot_config = /no/such/amipilot-config.toml\n"
        )
        pytester.makepyfile(
            test_it="def test_uses_amipilot(amipilot):\n    assert False\n"
        )
        result = pytester.runpytest()
        result.assert_outcomes(skipped=1)
