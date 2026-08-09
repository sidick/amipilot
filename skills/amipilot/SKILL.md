---
name: amipilot
description: >
  Drive, test, or inspect a classic AmigaOS GUI application from the host
  using AmiPilot — object-level Amiga GUI automation (find windows/gadgets
  by ID, role, or label; click/type/drag with real synthesized input;
  assert on state; golden-tree fixtures; pytest integration). Use whenever
  the task involves automating or testing an Amiga program's GUI, writing
  a host-side pytest for an Amiga app, connecting to AmiPilotServer,
  dumping an Amiga window's gadget tree, taking Amiga screenshots over the
  wire, launching Amiga programs remotely (Shell or Workbench-style), or
  anything mentioning amipilot, AmiPilotServer, AmiInspect, or the
  amipilot pytest fixture.
---

# Using AmiPilot from a host project

AmiPilot drives real AmigaOS GUIs semantically — locate a window/gadget by
`GA_ID`, role, or label; act with genuinely synthesized `input.device`
events; assert on live state — instead of pixel coordinates or screen
scraping. Two halves:

- **On-Amiga**: `AmiPilotServer` (a commodity) speaking a line protocol
  over serial (or Copperline's `[serial] mode="tcp"` bridge) or TCP.
- **Host**: the `amipilot` Python package (stdlib-only, Python 3.9+) —
  the `Amipilot` client class, an `amipilot` CLI, and a pytest plugin.

Upstream repo (source of truth for anything not covered here):
<https://github.com/sidick/amipilot> — `server/README.md` (full verb
set), `server/WIRE.md` (framing), `userdocs/` (MkDocs site),
`userdocs/Locator-Tiers-and-Limits.md` (what each Amiga toolkit can and
cannot expose).

## Install the host package

```sh
pip install amipilot            # if published for your environment, else:
pip install -e /path/to/amipilot/host          # from a checkout
pip install -e '/path/to/amipilot/host[serial]'  # + pyserial, only for real serial ports
```

Installing it also auto-registers the pytest plugin (entry point
`pytest11`), no `conftest.py` wiring needed.

## Getting a server to talk to

The Amiga side must already be running `AmiPilotServer SERIAL` (or
`TCP`). Three common shapes:

1. **Copperline emulator (preferred for tests)** — a Copperline config
   whose guest stages `AmiPilotServer SERIAL` via `S:User-Startup`, with
   `[serial] mode = "tcp"` exposing the wire on a host port (default
   1234). The pytest fixture below can boot this itself.
2. **TCP** — `AmiPilotServer TCP` on a real/emulated Amiga with
   networking. LAN/trusted networks only: no TLS, default password is
   the public string `"amipilot"` (the client sends it automatically via
   `AUTH`; override with `password=`). Optional server-side hardening:
   `TCPALLOW` (source-IP/CIDR allowlist) and `TCPPASSWORD`.
3. **Real serial cable** — `Amipilot.connect_serial(device, baud=19200)`
   (needs the `[serial]` extra).

## Connecting

```python
from amipilot import Amipilot

# Robust connect for a guest that may still be booting:
pilot = Amipilot.connect_with_retry("127.0.0.1", 1234, deadline_seconds=60)

# Single-shot (raises immediately on failure):
pilot = Amipilot.connect("127.0.0.1", 1234)

pilot.close()   # or use it as a context manager: `with Amipilot.connect(...) as pilot:`
```

**Timeout gotcha (real bug class):** `connect_with_retry()`'s
`connect_timeout` (default 15s) becomes the socket's per-command read
timeout for the client's whole lifetime. If any call will use a server-
side wait longer than that (`wait_for(..., timeout=30)`,
`click(..., expect=..., timeout=30)`), pass a larger `connect_timeout`,
or the server's legitimate answer surfaces as a raw socket
`TimeoutError` instead of the clean `amipilot.Timeout`.

## Core API cheat sheet

Errors are typed exceptions, not return codes: `NotFound`,
`CommandError`, `ActionFailed`, `Timeout` (all subclass
`AmipilotError`). Window arguments are **title substrings**; `screen=`
(a screen-name substring) disambiguates same-titled windows across
screens.

```python
win = pilot.tree("MyApp")                 # full gadget model: Window -> .gadgets list
                                          #   Gadget: .gadget_id .role .class_name .label
                                          #   .value .text (value-or-label), geometry
g = win.find(3)                           # by GA_ID
g = win.find_by_role(role="button", index=1)   # host-side tier-2 lookup
pilot.click("MyApp", 3)                   # by GA_ID
pilot.click_by_role("MyApp", role="button", label="Save", index=0)
pilot.type("MyApp", 2, "hello")           # focus + real rawkey typing (no newlines allowed)
pilot.type_by_role("MyApp", "hello", role="string")
text = pilot.get_text("MyApp", 2)         # string/integer gadget value, else its label
pilot.drag("MyApp", 5, dx, dy)            # sliders/scrollers (offset form)
pilot.drag_to("MyApp", src_id, dest_id)   # drag-and-drop (gadget-to-gadget)

# Waits — all polled SERVER-side, one round trip:
pilot.wait_for("window:Save As", timeout=10)     # a matching window appears
pilot.wait_for("nowindow:Save As")               # no window matches (pattern re-search)
pilot.wait_for("requester")                      # any window grows an Intuition Requester
pilot.wait_for_text("MyApp", 4, "Done")          # gadget text exactly equals value
pilot.click("MyApp", 1, expect="window:About")   # click + wait, atomic
pilot.click("MyApp", 9, expect="nowindow")       # "the window I clicked closed" (by identity)
pilot.wait_for_window("MyApp", timeout=30)       # host-side polling helpers
pilot.wait_for_screen("Workbench")

# Launching test subjects over the wire (both asynchronous — assert on
# the window appearing, not on the call returning):
pilot.launch("SYS:Utilities/Clock", stack=16384)         # Shell-style; GUI apps want stack>=16384
pilot.wb_launch("SYS:Prefs/Font",                        # real Workbench-style start (WBStartup)
                tooltypes={"KEY": "value"}, args=["SYS:file"])  # icon path WITHOUT ".info"

# Menus (shortcut-based selection):
strip = pilot.menu("MyApp")                       # full menu strip incl. shortcuts/check state
pilot.menu_pick("MyApp", menu_num, item_num)      # items WITHOUT a keyboard shortcut can't be
                                                  # picked yet (honest limit)

# Screens / screenshots / windows:
pilot.screens()                                   # list of Screen (matched by DefaultTitle)
shot = pilot.screenshot(window="MyApp")           # inspector tooling, not a locator mechanism
shot.save("out")                                  # writes out.png + out.iff (or use
                                                  # shot.to_png()/shot.to_ilbm() for bytes)
pilot.window_move("MyApp", dx, dy)                # needs a drag bar
pilot.window_resize("MyApp", width, height)       # needs a sizing gadget

# Allowlist-scoped file API (paths must be under a server-side FSROOT):
pilot.fs_list("SRC:build"); pilot.fs_stat(p); pilot.fs_get(p)  # -> bytes
pilot.fs_put("RAM:t.txt", b"data"); pilot.fs_mkdir(p); pilot.fs_delete(p)

# Manifest (@name) locators — logical names from a manifest file:
pilot.manifest("SRC:MyApp.manifest")
pilot.click_by_name("main.save"); pilot.type_by_name("main.name", "x")
pilot.get_text_by_name("main.status"); pilot.wait_for_text_by_name("main.status", "Done")

# Golden-tree fixture — "this app's UI still has this shape":
pilot.assert_tree_matches("MyApp", "goldens/MyApp.golden")   # auto-creates first run,
                                                             # raises GoldenMismatch w/ diff after
# MUI apps: verbatim passthrough to the app's own ARexx port
# (MUI's built-in set is just quit/hide/show/activate/deactivate/info/help):
pilot.mui_command("MYAPP", "quit")
```

Role vocabulary for `role=` / `find_by_role()`: `button`, `string`,
`integer`, `checkbox`, `radio_button`, `cycle`, `slider`, `scroller`,
`listview`, `listbrowser`, `text`, `custom`, `unknown`.

## pytest integration

The installed package provides a session-scoped `amipilot` fixture that
boots a Copperline config (which must stage `AmiPilotServer SERIAL`,
e.g. via `S:User-Startup`), waits for the wire, and hands the test a
connected `Amipilot`. Tests using it **skip cleanly** (not a false
pass) when no config/device is provided — safe to check into CI.

```python
def test_save_updates_status(amipilot):
    amipilot.type("MyApp", 2, "hello")
    amipilot.click_by_role("MyApp", role="button", label="Save")
    assert amipilot.get_text("MyApp", 4) == "Saved"
```

Configure via CLI or ini:

- `--amipilot-config PATH` / ini `amipilot_config` — Copperline config
  (machine-specific: real Kickstart + Workbench, so keep it out of VCS)
- `--amipilot-wire-port` (default 1234), `--amipilot-boot-timeout`
  (default 60), `--amipilot-copperline` / `--amipilot-copperline-ctl`
  (binary paths, also via `$COPPERLINE`/`$COPPERLINE_CTL`)
- ini `amipilot_copperline_args` — extra Copperline CLI args (default
  `--model A1200 --chipset AGA --chip 2M --fast 8M --noaudio --serial tcp`)
- `--amipilot-serial-device DEV` / ini `amipilot_serial_device` (+
  `--amipilot-serial-baud`, default 19200) — real-hardware mode: connects
  only, boots nothing; mutually exclusive with `--amipilot-config`

## The `amipilot` CLI (inspector)

```sh
amipilot dump "MyApp"                     # print the window's gadget tree (AmiInspect shape)
amipilot dump "MyApp" --format python     # manifest-ready name suggestions
amipilot dump "MyApp" --golden PATH [--update-golden]   # golden-tree check/regenerate
# --host/--port/--screen as with the API (defaults 127.0.0.1:1234)
```

When unsure what an app's gadget tree actually looks like, dump it first
and pick locators from real output — never guess IDs or labels.

## Honest limits and gotchas (stated constraints, don't fight them)

- **GadTools `BUTTON_KIND` labels are invisible to `LABEL=`** — the
  label is baked into rendered imagery under every `PLACETEXT_*`.
  Address buttons by `GA_ID`, `ROLE=`+`INDEX=`, or a manifest name.
- **ReAction `window.class` children aren't walkable** — only the
  top-level layout gadget appears; there is no public API to enumerate
  its children on classic OS 3.x. Permanent constraint.
- **`type()` text can't contain newlines** (the wire is line-based);
  `launch()` commands likewise.
- **`launch()`/`wb_launch()` are asynchronous** and don't confirm the
  program was found or started — always follow with
  `wait_for_window()`/`wait_for("window:...")`. GUI apps usually need
  `stack=16384` or more (AmigaDOS default is 4000).
- **Golden trees include window/screen titles verbatim**, which Locale
  catalogs can change with zero UI change — golden files aren't portable
  across machines with different Locale prefs.
- **`menu_pick()` needs a keyboard shortcut** on the item; shortcut-less
  items can't be selected yet.
- **`wait_for("requester")` is detection only** — it can't click a
  requester's gadgets, and only sees window-attached requesters.
- **TCP transport is not internet-safe** — no TLS, public default
  password, no rate limiting. LAN/trusted networks only.
- **`fs_*` paths must fall under a server-side `FSROOT`** grant;
  `fs_put` is wire-only (no ARexx form exists, by design).
