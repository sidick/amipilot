#!/usr/bin/env python3
"""Drives AmiPilotServer's MENU/MENUPICK verbs (phase 0.4, plus the
pointer-based fallback from issue #63) end to end for the on-target
regression check (tests/copperline/run.sh), against
fixtures/gadtools-app's menu strip (see its own header comment for the
exact layout: Project > About(A) / Toggle(T, checkit) / Disabled /
separator / More > Sub Item(S) / Sub NoShortcut).

Walks the menu via MENU and asserts the structure the walker read off
Intuition's live struct Menu/MenuItem chain, then MENUPICKs "About"
and "Sub Item" by their keyboard shortcuts, and "Toggle"/"Sub
NoShortcut" via the pointer-based fallback (a genuine synthesized
RMB-down/move/RMB-up, not a shortcut keystroke -- server/src/action.c's
AmipMenuPickByPointer()), confirming each pick genuinely reached the
app (not just that input was injected) by reading back the marker text
GTApp's own IDCMP_MENUPICK handler writes into its Host string gadget
-- the same GETTEXT path every other check already trusts. Also
confirms the permanently-disabled item is rejected client-side (no
input sent at all).

Prints one greppable line per stage (run.sh asserts on them) and exits
non-zero on any failure.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "host"))

from amipilot import ActionFailed, Amipilot  # noqa: E402
from amipilot.wire import WireError  # noqa: E402


def main() -> int:
    hostport = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:1234"
    host, port = hostport.rsplit(":", 1)

    client = Amipilot.connect_with_retry(host, int(port), deadline_seconds=30)
    print(f"HANDSHAKE SERVER={client.info.server_version} "
          f"PROTOCOL={client.info.protocol}")

    strip = client.menu("GadTools")
    menu = strip.menus[0]
    if menu.title == "Project" and len(menu.items) == 5:
        print("MENU-STRUCTURE PASS")
    else:
        print(f"MENU-STRUCTURE FAIL title={menu.title} items={len(menu.items)}")
        return 1

    about = strip.find("About")
    toggle = strip.find("Toggle")
    disabled = strip.find("Disabled")
    sub_item = strip.find("Sub Item")
    if (about is not None and about.shortcut == "A"
            and toggle is not None and toggle.checkit and toggle.checked
            and disabled is not None and not disabled.enabled
            and sub_item is not None and sub_item.shortcut == "S"
            and sub_item.item_num == 4 and sub_item.sub_num == 0):
        print("MENU-FIELDS PASS")
    else:
        print("MENU-FIELDS FAIL")
        return 1

    client.menu_pick("GadTools", about.menu_num, about.item_num)
    result = client.get_text("GadTools", 2)
    if result == "about picked":
        print(f"MENUPICK-ABOUT PASS RESULT={result}")
    else:
        print(f"MENUPICK-ABOUT FAIL RESULT={result}")
        return 1

    client.menu_pick("GadTools", sub_item.menu_num, sub_item.item_num, sub_item.sub_num)
    result = client.get_text("GadTools", 2)
    if result == "subitem picked":
        print(f"MENUPICK-SUBITEM PASS RESULT={result}")
    else:
        print(f"MENUPICK-SUBITEM FAIL RESULT={result}")
        return 1

    # "Toggle" is enabled but has no keyboard shortcut -- MENUPICK
    # against it exercises the pointer-based fallback (issue #63:
    # genuine RMB-down/move/RMB-up, not a shortcut keystroke).
    client.menu_pick("GadTools", toggle.menu_num, toggle.item_num)
    result = client.get_text("GadTools", 2)
    if result == "toggle picked":
        print(f"MENUPICK-TOGGLE-POINTER PASS RESULT={result}")
    else:
        print(f"MENUPICK-TOGGLE-POINTER FAIL RESULT={result}")
        return 1

    # "Sub NoShortcut" is enabled, one level deep, and has no shortcut
    # -- the pointer-based fallback's genuinely uncertain case (issue
    # #63: submenu box geometry is undocumented by the RKRM), unlike
    # Toggle above (a top-level item).
    sub_noshortcut = strip.find("Sub NoShortcut")
    client.menu_pick("GadTools", sub_noshortcut.menu_num,
                      sub_noshortcut.item_num, sub_noshortcut.sub_num)
    result = client.get_text("GadTools", 2)
    if result == "subitem noshortcut picked":
        print(f"MENUPICK-SUBITEM-POINTER PASS RESULT={result}")
    else:
        print(f"MENUPICK-SUBITEM-POINTER FAIL RESULT={result}")
        return 1

    try:
        client.menu_pick("GadTools", disabled.menu_num, disabled.item_num)
        print("MENUPICK-DISABLED FAIL no exception raised")
        return 1
    except ActionFailed:
        print("MENUPICK-DISABLED PASS rejected")

    client.click("GadTools", 1)
    client.quit()
    client.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (WireError, TimeoutError) as e:
        print(f"MENU-TEST ERROR: {e}")
        sys.exit(1)
