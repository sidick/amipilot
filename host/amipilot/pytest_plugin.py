"""pytest plugin: emulator-booting fixtures for AmiPilot host tests
(docs/implementation-plan.md phase 0.3 release gate -- "a host pytest
clicks a button and asserts a label changed, deterministically, under
the emulator in CI").

Auto-registered once `host/` is installed (pyproject.toml's
`[project.entry-points.pytest11]`). Provides one fixture, `amipilot`
(session-scoped): boots a Copperline config that already stages
`AmiPilotServer SERIAL` (e.g. via the target's `S:User-Startup`, same
technique `tests/copperline/run.sh` uses), waits for the wire transport
to answer, connects, and hands the test a live `Amipilot` client.

Nothing here is emulator-specific beyond invoking the `copperline` /
`copperline-ctl` binaries documented in Copperline's own headless-mode
guide -- this plugin doesn't know or care what the guest is running,
same as the rest of the host client.

Configuration is machine-specific (a real Kickstart ROM, a Workbench
install with the test's fixture staged), so a test using `amipilot`
SKIPS cleanly -- not a false pass -- when `--amipilot-config` /
`amipilot_config` isn't set, exactly like `make test-target` skips
without `tests/copperline/copperline.local.toml`.
"""

from __future__ import annotations

import json
import os
import shlex
import subprocess
import time

import pytest

from .client import Amipilot

_DEFAULT_COPPERLINE_ARGS = (
    "--model A1200 --chipset AGA --chip 2M --fast 8M --noaudio --serial tcp"
)


def pytest_addoption(parser: "pytest.Parser") -> None:
    group = parser.getgroup("amipilot")
    group.addoption(
        "--amipilot-config",
        action="store",
        default=None,
        help="path to a Copperline config booting a guest with "
             "AmiPilotServer SERIAL already staged (e.g. via "
             "S:User-Startup); unset skips tests using the amipilot fixture",
    )
    group.addoption(
        "--amipilot-copperline",
        action="store",
        default=os.environ.get("COPPERLINE", "copperline"),
        help="copperline binary (default: $COPPERLINE or 'copperline')",
    )
    group.addoption(
        "--amipilot-copperline-ctl",
        action="store",
        default=os.environ.get("COPPERLINE_CTL", "copperline-ctl"),
        help="copperline-ctl binary (default: $COPPERLINE_CTL or "
             "'copperline-ctl')",
    )
    group.addoption(
        "--amipilot-wire-port",
        action="store",
        type=int,
        default=1234,
        help="host port of Copperline's [serial] mode=tcp bridge (default 1234)",
    )
    group.addoption(
        "--amipilot-boot-timeout",
        action="store",
        type=float,
        default=60.0,
        help="seconds to wait for boot + wire handshake (default 60)",
    )

    parser.addini("amipilot_config", "same as --amipilot-config", default=None)
    parser.addini(
        "amipilot_copperline_args",
        "extra Copperline CLI args (space-separated; --config/--serial/"
        "--control are added automatically)",
        default=_DEFAULT_COPPERLINE_ARGS,
    )


def _option(request: "pytest.FixtureRequest", name: str, ini_name: str | None = None):
    value = request.config.getoption(name)
    if value not in (None, ""):
        return value
    return request.config.getini(ini_name or name.replace("-", "_"))


class _Boot:
    """Owns the two subprocesses (copperline, the free-running
    copperline-ctl call) for the lifetime of one boot -- teardown is
    "best effort, always terminate both", mirroring
    tests/copperline/run.sh's own cleanup trap."""

    def __init__(self, copperline_proc, ctl_proc, info_path):
        self.copperline_proc = copperline_proc
        self.ctl_proc = ctl_proc
        self.info_path = info_path

    def stop(self) -> None:
        for proc in (self.ctl_proc, self.copperline_proc):
            if proc is not None and proc.poll() is None:
                proc.terminate()
        for proc in (self.ctl_proc, self.copperline_proc):
            if proc is not None:
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
        try:
            os.unlink(self.info_path)
        except OSError:
            pass


def _boot_copperline(request: "pytest.FixtureRequest", tmp_path_factory) -> _Boot:
    config = _option(request, "--amipilot-config", "amipilot_config")
    if not config:
        pytest.skip(
            "no AmiPilot Copperline config configured "
            "(--amipilot-config or the amipilot_config ini setting) -- "
            "see host/README.md"
        )
    if not os.path.exists(config):
        pytest.skip(f"amipilot config not found: {config}")

    copperline = _option(request, "--amipilot-copperline")
    copperline_ctl = _option(request, "--amipilot-copperline-ctl")
    timeout = _option(request, "--amipilot-boot-timeout")
    # ini-only setting (no matching CLI flag) -- getoption() raises
    # AttributeError for a name it never registered via addoption(), so
    # this goes straight to getini() rather than through _option().
    extra_args = shlex.split(request.config.getini("amipilot_copperline_args"))

    info_path = str(tmp_path_factory.mktemp("amipilot") / "ctl-info.json")

    copperline_proc = subprocess.Popen(
        [copperline, "--config", config, *extra_args,
         "--control", ":0", "--control-info", info_path],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )

    deadline = time.monotonic() + timeout
    while not os.path.exists(info_path):
        if copperline_proc.poll() is not None:
            raise RuntimeError(
                f"copperline exited (rc={copperline_proc.returncode}) "
                "before writing its control-info file"
            )
        if time.monotonic() > deadline:
            copperline_proc.terminate()
            raise TimeoutError("copperline never wrote its control-info file")
        time.sleep(0.1)

    # Free-run for the whole test session on the emulator's own wall
    # clock -- the guest and the host client interact live from here,
    # same non-blocking run_until pattern tests/copperline/run.sh uses
    # for its wire check, just with no fixed target: this call blocks
    # inside its own process for up to an hour of emulated time, well
    # past any real test session, and gets torn down in _Boot.stop().
    ctl_proc = subprocess.Popen(
        [copperline_ctl, "--info", info_path, "run_until",
         json.dumps({"seconds": 3600})],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )

    return _Boot(copperline_proc, ctl_proc, info_path)


def _connect_with_retry(host: str, port: int, deadline: float) -> Amipilot:
    """Thin wrapper over `Amipilot.connect_with_retry()` -- the same
    connect/handshake-retry logic generalized onto the client itself
    (see its own docstring for the two real, confirmed-live failure
    modes it tolerates) so every caller gets it, not just this plugin.
    Only the deadline shape differs: this plugin computes an absolute
    monotonic deadline up front (shared with the boot-timeout wait
    above it), while the client's own public method takes a duration."""
    return Amipilot.connect_with_retry(
        host, port, deadline_seconds=max(deadline - time.monotonic(), 0.0)
    )


@pytest.fixture(scope="session")
def amipilot(request: "pytest.FixtureRequest", tmp_path_factory) -> Amipilot:
    """A live `Amipilot` client connected to a Copperline guest booted
    from `--amipilot-config`. Session-scoped: one emulator boot serves
    the whole test session (matching the wire's own "one test session
    at a time" model, server/WIRE.md). Skips cleanly if unconfigured."""
    port = _option(request, "--amipilot-wire-port")
    timeout = _option(request, "--amipilot-boot-timeout")

    boot = _boot_copperline(request, tmp_path_factory)
    try:
        client = _connect_with_retry("127.0.0.1", port, time.monotonic() + timeout)
    except Exception:
        boot.stop()
        raise

    yield client

    try:
        client.quit()
    except Exception:
        pass
    client.close()
    boot.stop()
