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
import subprocess
import time

import pytest

from amipilot.client import Amipilot  # noqa: E402
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
        "--amipilot-serial-device": None,
        "--amipilot-serial-baud": 19200,
    }
    options.update(overrides)
    ini = {"amipilot_copperline_args": _DEFAULT_COPPERLINE_ARGS,
           "amipilot_config": None,
           "amipilot_serial_device": None}
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

        instances = []

        class HangingProc:
            def __init__(self, *a, **k):
                self.terminated = False
                self.waited = False
                instances.append(self)

            def poll(self):
                return None  # still running, never writes the info file

            def terminate(self):
                self.terminated = True

            def wait(self, timeout=None):
                # Simulates a process that exits promptly once
                # terminate()d -- confirms _boot_copperline() actually
                # reaps it on this path (not just calling terminate()
                # and leaving a zombie for this process's own lifetime,
                # since nothing else would ever wait() on it otherwise).
                if not self.terminated:
                    raise subprocess.TimeoutExpired(cmd="hanging", timeout=timeout)
                self.waited = True

        monkeypatch.setattr(subprocess, "Popen", HangingProc)
        request = _request(tmp_path)
        with pytest.raises(TimeoutError, match="control-info file"):
            _boot_copperline(request, FakeTmpPathFactory(tmp_path))

        assert instances[0].terminated
        assert instances[0].waited, (
            "copperline_proc.wait() must be called after terminate() on the "
            "timeout path, or the process is never reaped and leaks as a "
            "zombie for this pytest process's own lifetime"
        )


class TestConnectWithRetry:
    """_connect_with_retry() is a thin wrapper over
    Amipilot.connect_with_retry() (host/amipilot/client.py), which
    carries the real logic and its own thorough test coverage
    (host/tests/test_client.py) -- this just confirms the wrapper
    converts its absolute deadline into the shared method's duration
    correctly and returns a working client, not a duplicate of that
    coverage."""

    def test_delegates_to_amipilot_connect_with_retry(self, monkeypatch):
        received = {}

        def fake_connect_with_retry(host, port, deadline_seconds=60.0, **kw):
            received["host"] = host
            received["port"] = port
            received["deadline_seconds"] = deadline_seconds
            return "CLIENT-STUB"

        monkeypatch.setattr(Amipilot, "connect_with_retry",
                             classmethod(lambda cls, *a, **kw: fake_connect_with_retry(*a, **kw)))

        deadline = time.monotonic() + 5
        result = _connect_with_retry("127.0.0.1", 1234, deadline)

        assert result == "CLIENT-STUB"
        assert received["host"] == "127.0.0.1"
        assert received["port"] == 1234
        assert 4.0 < received["deadline_seconds"] <= 5.0


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

    def test_serial_and_config_together_raises_usage_error(self, pytester, tmp_path):
        # Both connection modes configured at once is a config mistake,
        # not something that should silently pick one -- see the
        # amipilot fixture's own docstring.
        pytester.syspathinsert(HOST_DIR)
        cfg = tmp_path / "cfg.toml"
        cfg.write_text("")
        pytester.makepyfile(
            test_it="def test_uses_amipilot(amipilot):\n    assert False\n"
        )
        result = pytester.runpytest(
            f"--amipilot-config={cfg}",
            "--amipilot-serial-device=/dev/nonexistent",
        )
        result.assert_outcomes(errors=1)
        result.stdout.fnmatch_lines(["*mutually exclusive*"])

    def test_serial_device_without_pyserial_raises_clear_error(self, pytester):
        try:
            import serial  # noqa: F401
        except ImportError:
            pass
        else:
            pytest.skip("pyserial is installed in this environment")
        pytester.syspathinsert(HOST_DIR)
        pytester.makepyfile(
            test_it="def test_uses_amipilot(amipilot):\n    assert False\n"
        )
        result = pytester.runpytest(
            "--amipilot-serial-device=/dev/nonexistent",
        )
        result.assert_outcomes(errors=1)
        result.stdout.fnmatch_lines(["*pyserial*"])
