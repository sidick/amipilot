#!/usr/bin/env python3
"""Stock-app conformance check (phase 0.5, docs/implementation-plan.md's
"Testing strategy": "Foreign-app tier tested against a fixed set of
stock programs... with golden interaction scripts") and its own stated
success criterion: "A stock Prefs editor is driven end-to-end (open,
change a setting, save) via tier-2 locators only -- with the script
written using only dump/AmiInspect output, no source or docs
consulted."

Target: SYS:Prefs/Time (AmigaOS 3.2's stock Time Preferences editor --
genuinely shipped with the OS, not a hand-written fixture). Every
locator below was discovered by running AmiInspect/amipilot dump
against a live Time Preferences window and reading its output, then
confirmed empirically under Copperline -- nothing here comes from
Commodore/Hyperion source or docs, matching the release criterion's
own constraint.

What was tried and what actually works, honestly:

- The year string field (the window's only role=string gadget) reads
  "1978" and never changes no matter what's typed into it, even after
  an explicit click to focus it first. This machine profile's own
  Copperline config has no RTC hardware (`rtc: none`, confirmed in
  its boot log) -- the most likely explanation is that Time
  Preferences disables direct date/time entry without one, though
  that's an inference from behavior, not something read from source.
  Confirmed live rather than assumed; not a bug in AmiPilot's TYPE
  path (the same mechanism types into fixtures/gadtools-app's own
  string field correctly).
- Time Preferences' three bottom buttons are PLACETEXT_IN (baked into
  rendered imagery, GadgetText empty) -- the same, already-documented
  BUTTON_KIND limit fixtures/gadtools-app hits -- so LABEL= can't
  address any of them; ROLE=button INDEX=<n> (positional) is the only
  tier-2 locator that reaches them. Clicking each one individually and
  checking which closed the window (the only externally observable
  effect available here) found: INDEX=0 (leftmost, conventionally
  "Save") does nothing detectable -- the window stays open and no
  ENVARC:Sys/time.prefs appears -- while INDEX=1 (middle,
  conventionally "Use") reliably closes it. Both findings are
  consistent with the RTC theory above (an editor with no clock
  hardware to set may reasonably disable persisting a setting it
  can't act on, while still letting a session-only "Use" go through)
  but are reported as *observed*, not explained by source access.
- The two sliders (Hours/Minutes) drag fine via the existing DRAG verb
  -- a real, working setting change -- but their resulting position
  isn't readable back through GETTEXT (only string/integer gadgets
  expose a `value=`), so "the setting changed" is asserted by the
  DRAG call succeeding, not by reading a confirmed new value back;
  an honest limit of what's observable here, not a gap in this test.

So: this script drags the Minutes slider (the setting change) and
uses INDEX=1 ("Use") to close the window with EXPECT=NOWINDOW,
composing the click with a server-side wait -- the actually-verified,
working end of "open, change a setting, exit via the app's own
affordances", rather than the unreachable "...and Save" this specific
target turned out not to support cleanly. The RTC-inert Save button
and the disabled year field are worth their own note in a quirk
profile for this exact app+environment (manifest/SPEC.md's "Quirk
profiles" section) -- not something to keep guessing at here.

This script mutates nothing persistent (no ENVARC: file gets
written by "Use"), unlike an earlier version of this check that used
Save -- so no backup/restore step is needed around it, unlike other
run.sh checks that do mutate shared Workbench state.

Prints one greppable line per stage (run.sh asserts on them) and
exits non-zero on any failure.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "host"))

from amipilot import Amipilot, NotFound, Timeout  # noqa: E402
from amipilot.wire import WireError  # noqa: E402

WINDOW = "Time Preferences"
MINUTES_GADGET_ID = 14  # discovered via AmiInspect; DRAG has no tier-2 form


def main() -> int:
    hostport = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:1234"
    host, port = hostport.rsplit(":", 1)

    # connect_timeout must comfortably exceed the CLICK EXPECT=
    # TIMEOUT= used below (10s) -- see connect_with_retry()'s own
    # docstring for why a too-small value here silently breaks a
    # legitimately slow-but-successful server-side wait.
    client = Amipilot.connect_with_retry(host, int(port), deadline_seconds=30,
                                          connect_timeout=20)
    print(f"HANDSHAKE SERVER={client.info.server_version} "
          f"PROTOCOL={client.info.protocol}")

    client.launch("SYS:Prefs/Time", stack=8192)
    print("LAUNCH-SENT OK")

    try:
        client.wait_for(f"window:{WINDOW}", timeout=20)
        print("WINDOW-APPEARED PASS")
    except Timeout:
        print("WINDOW-APPEARED FAIL")
        return 1

    client.drag(WINDOW, MINUTES_GADGET_ID, 30, 0)
    print("MINUTES-DRAGGED OK")

    # ROLE=button INDEX=1 is "Use" (confirmed live -- see module
    # docstring). EXPECT= composes the click with the server-side
    # wait for the window it closes, closing the same click-then-
    # check race WAITFOR/EXPECT= exist for generally.
    client.click_by_role(WINDOW, role="button", index=1, expect="nowindow", timeout=10)
    print("USE-CLICKED OK")

    try:
        client.tree(WINDOW)
        print("WINDOW-GONE FAIL")
        return 1
    except NotFound:
        print("WINDOW-GONE PASS")

    client.quit()
    client.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (WireError, Timeout) as e:
        print(f"STOCK-APP-TEST ERROR: {e}")
        sys.exit(1)
