#!/usr/bin/env python3
"""Drives the full "bare machine" lifecycle end to end
(docs/implementation-plan.md's own "Success criteria" section):

  "A full lifecycle test passes: Workbench-launch a tool with an
  overridden tooltype and a project argument, assert the override took
  effect in the GUI, drive it, and quit it via its own affordances --
  no disk-modified icons, no kill, window-closed confirmed."

  "The same lifecycle test runs entirely self-contained against a bare
  machine over TCP -- fixtures staged via fs-put (icon included),
  artifacts harvested via fs-get, tmpdir cleaned -- no shared drive,
  mount, or emulator involved."

Everything from this point on happens purely over the wire (a real
AmiPilotServer TCP connection, not the serial-to-TCP bridge every
other check's `AmiPilotServer SERIAL` uses): the fixture binary and
its icon are read as plain bytes off the HOST's own local build/
directory (compiled artifacts, the same way any real deployment would
have pre-built binaries ready to hand -- not read live off a
Copperline SRC: hostfs mount, which this script never touches) and
uploaded via FSPUT, launched via WBLAUNCH with a TOOLTYPE= override
and an ARG= project argument, driven and asserted on via TREE/
GETTEXT/CLICK, harvested via FSGET, and cleaned up via FSDELETE.
`AmiPilotServer` itself must be started with `FSROOT=T:` (the only
guest-side setup this check's own smoke.script does, beyond starting
the server).

fixtures/wbgui-app is a SEPARATE fixture from fixtures/wbapp/WBApp
(see its own header for why) -- it's the one member of this project's
fixture set that's both Workbench-startable AND has a real GUI window
to assert on and a real close gadget to quit through.

Prints one greppable line per stage (run.sh asserts on them) and exits
non-zero on any failure.
"""

import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "host"))

from amipilot import Amipilot  # noqa: E402
from amipilot.wire import WireError  # noqa: E402

REPO_ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
BIN_PATH = os.path.join(REPO_ROOT, "build", "fixtures", "WBGuiApp")
ICON_PATH = os.path.join(REPO_ROOT, "build", "fixtures", "WBGuiApp.info")

STAGE_DIR = "T:amipilot-lifecycle"
STAGE_BIN = f"{STAGE_DIR}/WBGuiApp"
STAGE_ICON = f"{STAGE_DIR}/WBGuiApp.info"
STAGE_ARG = f"{STAGE_DIR}/project.txt"
RESULT_PATH = "T:amipilot-lifecycle-result.txt"

OVERRIDE_GREETING = "hello from fs-put"
PROJECT_ARG_CONTENT = b"amipilot bare-machine lifecycle project file\n"


def main() -> int:
    hostport = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:1234"
    host, port = hostport.rsplit(":", 1)

    client = Amipilot.connect_with_retry(host, int(port), deadline_seconds=30)
    print(f"HANDSHAKE SERVER={client.info.server_version} "
          f"PROTOCOL={client.info.protocol}")

    with open(BIN_PATH, "rb") as f:
        bin_bytes = f.read()
    with open(ICON_PATH, "rb") as f:
        icon_bytes = f.read()

    client.fs_mkdir(STAGE_DIR)
    client.fs_put(STAGE_BIN, bin_bytes)
    client.fs_put(STAGE_ICON, icon_bytes)
    client.fs_put(STAGE_ARG, PROJECT_ARG_CONTENT)
    print(f"STAGE PASS bin={len(bin_bytes)}B icon={len(icon_bytes)}B "
          f"arg={len(PROJECT_ARG_CONTENT)}B (all via FSPUT, no hostfs)")

    client.wb_launch(STAGE_BIN, tooltypes={"GREETING": OVERRIDE_GREETING}, args=[STAGE_ARG])
    print("LAUNCH PASS")

    window = client.wait_for_window("AmiPilot Lifecycle Fixture", timeout=20.0)
    print(f"WINDOW PASS title={window.title!r}")

    greeting_text = client.get_text(window.title, 1)
    override_ok = greeting_text == OVERRIDE_GREETING
    print(f"OVERRIDE {'PASS' if override_ok else 'FAIL'} "
          f"expected={OVERRIDE_GREETING!r} actual={greeting_text!r}")
    if not override_ok:
        client.close()
        return 1

    # Quits via the window's own real system close gadget, addressed
    # by the tier-2 ROLE=custom locator -- the same close gadget as
    # every window's own WFLG_CLOSEGADGET, not fixtures/wbgui-app's
    # separate "_Quit" button (GA_ID=2, kept on the fixture too as a
    # second, equally legitimate affordance). This exercises the fix
    # for issue #60: ResolveTargetGadget() used to re-resolve a
    # ROLE=/INDEX=-matched system gadget by its (always-0, ambiguous)
    # GA_ID, silently landing on whichever system gadget happened to
    # be first in Intuition's own chain (the depth gadget) regardless
    # of which index was requested -- now dispatches through
    # AmipFindSystemGadget() (matching by GTYP_SYSTYPEMASK sub-type)
    # instead, which is unambiguous. Close is index 1 among this
    # window's own role=custom gadgets (0=depth, 1=close, 2=drag bar --
    # confirmed via a live TREE dump, not assumed).
    custom_gadgets = [g for g in window.gadgets if g.role == "custom"]
    close_index = next(
        i for i, g in enumerate(custom_gadgets)
        if g.class_name == "buttongclass" and g.left < window.width // 2
    )
    client.click_by_role(window.title, role="custom", index=close_index)

    try:
        client.tree(window.title)
        print("CLOSE FAIL window still open after clicking its close gadget")
        client.close()
        return 1
    except Exception:
        print("CLOSE PASS window gone after its own close gadget (no kill)")

    result_bytes = client.fs_get(RESULT_PATH)
    result_text = result_bytes.decode("latin-1")
    # WBArg[0] is always the launched tool itself (its own basename);
    # additional args= are appended from WBArg[1] on, and wa_Name is
    # just the filename relative to its own wa_Lock directory lock --
    # "project.txt", not the full STAGE_ARG path.
    arg_ok = "ARG1=project.txt" in result_text
    tooltype_ok = f"TOOLTYPE_GREETING={OVERRIDE_GREETING}" in result_text
    print(f"HARVEST {'PASS' if (arg_ok and tooltype_ok) else 'FAIL'} "
          f"arg_ok={arg_ok} tooltype_ok={tooltype_ok}")
    if not (arg_ok and tooltype_ok):
        print(f"HARVEST   --- result.txt ---\n{result_text}")
        client.close()
        return 1

    client.fs_delete(STAGE_BIN)
    client.fs_delete(STAGE_ICON)
    client.fs_delete(STAGE_ARG)
    client.fs_delete(RESULT_PATH)

    # The launched process's own CurrentDir() (set to the icon's own
    # wa_Lock -- STAGE_DIR -- for the tooltype self-lookup, see
    # fixtures/wbgui-app) only actually releases at process exit,
    # which happens a moment AFTER the window itself closes (CloseWindow
    # completing doesn't mean the process has finished unwinding and
    # replied its WBStartup message yet -- same "assert on the expected
    # effect, don't assume synchronous completion" honesty this
    # project's WBLAUNCH/LAUNCH docs already call for elsewhere). A
    # short retry, not a fixed sleep, waits out exactly that race.
    deleted = False
    last_error = None
    for _ in range(10):
        try:
            client.fs_delete(STAGE_DIR)
            deleted = True
            break
        except Exception as e:
            last_error = e
            time.sleep(0.5)
    if not deleted:
        remaining = client.fs_list(STAGE_DIR)
        print(f"CLEANUP FAIL directory delete kept failing ({last_error}); "
              f"contents={[e.name for e in remaining]}")
        client.close()
        return 1
    print("CLEANUP PASS")

    client.quit()
    client.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except WireError as e:
        print(f"LIFECYCLE-TEST ERROR: {e}")
        sys.exit(1)
