#!/bin/sh
# run.sh -- headless Copperline conformance check for the phase 0.1 fixtures.
#
# Boots fixtures/gadtools-app and fixtures/classact-app in turn under a
# headless Copperline instance (via the User-Startup dev-test hook, see
# README.md), runs AmiInspect against each, and asserts the specific
# classification lines confirmed correct by hand during phase 0.1
# development -- so a future change that regresses role/label/class
# reading (like the GTYP_CUSTOMGADGET bitmask bug caught this way) fails
# loudly instead of silently.
#
# Requires tests/copperline/copperline.local.toml (gitignored -- see
# copperline.example.toml and README.md), which points at a real
# Kickstart 3.2 ROM and Workbench 3.2.3 install. Skips (not a false
# pass -- see the Makefile's test-target) when that file is absent,
# since CI has no such machine-specific asset.
#
# Prereqs: copperline + copperline-ctl on PATH (or COPPERLINE=/
# COPPERLINE_CTL= overrides), and the fixtures already built
# (`make fixtures` from the repo root).
set -eu

REPO_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CONFIG="$REPO_ROOT/tests/copperline/copperline.local.toml"
COPPERLINE=${COPPERLINE:-copperline}
COPPERLINE_CTL=${COPPERLINE_CTL:-copperline-ctl}
SMOKE_SCRIPT="$REPO_ROOT/tests/copperline/smoke.script"
BUILD="$REPO_ROOT/build"
RUN_SECONDS=25

if [ ! -f "$CONFIG" ]; then
	echo "run.sh: no $CONFIG -- skipping (see copperline.example.toml)"
	exit 0
fi

COPPERLINE_PID=""

cleanup() {
	if [ -n "$COPPERLINE_PID" ] && kill -0 "$COPPERLINE_PID" 2>/dev/null; then
		kill "$COPPERLINE_PID" 2>/dev/null || true
	fi
	rm -f "$SMOKE_SCRIPT"
}
trap cleanup EXIT INT TERM

FAILED=0

# run_fixture NAME BINARY WINDOW_TITLE OUTFILE MARKERFILE PATTERN...
run_fixture() {
	name=$1; binary=$2; window_title=$3; outfile=$4; markerfile=$5
	shift 5

	echo "run.sh: $name"

	rm -f "$BUILD/$outfile" "$BUILD/$markerfile"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/fixtures/$binary
Wait 5
SRC:build/AmiInspect WINDOW=$window_title >SRC:build/$outfile
Echo "DONE" >SRC:build/$markerfile
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL ($name): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until "{\"seconds\": $RUN_SECONDS}" > /dev/null
	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	if [ ! -f "$BUILD/$markerfile" ]; then
		echo "run.sh: FAIL ($name): $markerfile never appeared -- crash or hang"
		echo "run.sh:   see $REPO_ROOT/build/.copperline-log-$$.txt"
		FAILED=1
		return
	fi

	ok=1
	for pattern in "$@"; do
		if ! grep -qF "$pattern" "$BUILD/$outfile" 2>/dev/null; then
			echo "run.sh: FAIL ($name): expected line not found: $pattern"
			ok=0
		fi
	done

	if [ "$ok" -eq 1 ]; then
		echo "run.sh: PASS ($name)"
	else
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/$outfile" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

run_fixture "gadtools-app" "GTApp" "GadTools" "inspect-gadtools.txt" "marker-gt.txt" \
	'gadget id=1 role=button' \
	'gadget id=2 role=string class="" label="Host:"' \
	'gadget id=3 role=checkbox class="" label="Enabled"'

# The layout.gadget entry's geometry is asserted too, not just its class:
# GA_Width/GA_Height answer via GetAttr with GFLG_RELWIDTH/RELHEIGHT set
# (a negative offset from the window's own size, e.g. Width=-8 meaning
# "window width minus 8"), which read as nonsensical negative numbers
# before ResolveGadgetGeometry() -- confirmed regression-worthy the hard
# way, see action.c's own comment on this. 220x130 is the resolved
# absolute size against this fixture's auto-computed 228x143 window.
run_fixture "classact-app" "CAApp" "ClassAct" "inspect-classact.txt" "marker-ca.txt" \
	'class="layout.gadget" label="" [4,11 220x130]'

# --- WBPattern regression check (phase 0.5, a real hang found and fixed) --
# AmigaOS 3.2's stock SYS:Prefs/WBPattern has custom-drawn Preview/Sketch
# boxes (GadgetID 23/24) whose GadgetType bits claim GTYP_CUSTOMGADGET but
# which don't actually carry a real BOOPSI _Object header -- OCLASS()
# returns a bogus class pointer for them, and blindly dispatching
# GetAttr()/DoMethod() through it wedged the whole machine (confirmed via
# GDB against Copperline's --gdb remote server: the blocked task's own
# saved PC was inside a ROM trampoline, reached via that garbage class
# pointer -- see the walk.c comment at the TypeOfMem() check this added).
# Not a hand-written fixture -- reaches SYS:Prefs/WBPattern directly off
# the mounted Workbench: volume, same as run_stock_app_check reaches
# SYS:Prefs/Time. Asserts BOTH that AmiInspect returns at all (the crux
# of the regression -- a hang here fails via the marker-never-appears
# path, same signal run_fixture() uses above) and that gadget 23
# degrades to role=custom rather than being silently misclassified.
run_wbpattern_check() {
	echo "run.sh: WBPattern"

	rm -f "$BUILD/inspect-wbpattern.txt" "$BUILD/marker-wbp.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SYS:Prefs/WBPattern
Wait 10
SRC:build/AmiInspect WINDOW=WBPattern >SRC:build/inspect-wbpattern.txt
Echo "DONE" >SRC:build/marker-wbp.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (WBPattern): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until '{"seconds": 30}' > /dev/null
	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	if [ ! -f "$BUILD/marker-wbp.txt" ]; then
		echo "run.sh: FAIL (WBPattern): marker-wbp.txt never appeared -- AmiInspect hung or crashed"
		echo "run.sh:   see $REPO_ROOT/build/.copperline-log-$$.txt"
		FAILED=1
		return
	fi

	if grep -qF 'gadget id=23 role=custom class="" label=""' "$BUILD/inspect-wbpattern.txt" 2>/dev/null; then
		echo "run.sh: PASS (WBPattern)"
	else
		echo "run.sh: FAIL (WBPattern): expected line not found: gadget id=23 role=custom class=\"\" label=\"\""
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/inspect-wbpattern.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

# --- action-engine click check (phase 0.2) --------------------------------
# Boots gadtools-app, clicks its Connect button (GA_ID=1) via AmiClickTest
# (the action engine's genuine input.device injection), and asserts the
# window actually closed -- the fixture exits when Connect is pressed, so
# a subsequent AmiInspect finding no matching window is the pass signal.
# This is an end-to-end proof the synthetic click is really delivered
# through Intuition to the gadget, not just that the events were queued.
run_click_check() {
	echo "run.sh: action-engine click"

	rm -f "$BUILD/click-result.txt" "$BUILD/inspect-after-click.txt" "$BUILD/marker-click.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/fixtures/GTApp
Wait 5
SRC:build/AmiClickTest WINDOW=GadTools ID=1 >SRC:build/click-result.txt
Wait 3
SRC:build/AmiInspect WINDOW=GadTools >SRC:build/inspect-after-click.txt
Echo "DONE" >SRC:build/marker-click.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (click): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until "{\"seconds\": $RUN_SECONDS}" > /dev/null
	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	if [ ! -f "$BUILD/marker-click.txt" ]; then
		echo "run.sh: FAIL (click): marker-click.txt never appeared -- crash or hang"
		FAILED=1
		return
	fi

	if ! grep -qF 'click delivered' "$BUILD/click-result.txt" 2>/dev/null; then
		echo "run.sh: FAIL (click): AmiClickTest did not report delivery"
		sed 's/^/run.sh:   /' "$BUILD/click-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
		return
	fi

	# The window must be GONE: AmiInspect writes nothing to stdout when no
	# window matches (its error goes to stderr), so a non-empty inspect
	# output means the fixture is still open -- the click didn't register.
	if [ -s "$BUILD/inspect-after-click.txt" ]; then
		echo "run.sh: FAIL (click): fixture window still open after click"
		sed 's/^/run.sh:   /' "$BUILD/inspect-after-click.txt"
		FAILED=1
		return
	fi

	echo "run.sh: PASS (action-engine click)"
}

# --- action-engine type check (phase 0.2) ---------------------------------
# Clicks the Host string gadget (GA_ID=2) to focus it, types a string via
# AmipTypeString (real IECLASS_RAWKEY events through MapANSI), then asserts
# AmiInspect reads the typed text back out of the live StringInfo buffer --
# proof the whole round trip (click-to-focus, keymap-aware typing, and the
# walker's new value= field) really works, not just that events were sent.
run_type_check() {
	echo "run.sh: action-engine type"

	rm -f "$BUILD/type-result.txt" "$BUILD/inspect-after-type.txt" "$BUILD/marker-type.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/fixtures/GTApp
Wait 5
SRC:build/AmiClickTest WINDOW=GadTools ID=2 TEXT="hello amipilot" >SRC:build/type-result.txt
Wait 3
SRC:build/AmiInspect WINDOW=GadTools >SRC:build/inspect-after-type.txt
Echo "DONE" >SRC:build/marker-type.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (type): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until "{\"seconds\": $RUN_SECONDS}" > /dev/null
	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	if [ ! -f "$BUILD/marker-type.txt" ]; then
		echo "run.sh: FAIL (type): marker-type.txt never appeared -- crash or hang"
		FAILED=1
		return
	fi

	if ! grep -qF 'value="hello amipilot"' "$BUILD/inspect-after-type.txt" 2>/dev/null; then
		echo "run.sh: FAIL (type): typed text not found in string gadget"
		sed 's/^/run.sh:   /' "$BUILD/inspect-after-type.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
		return
	fi

	echo "run.sh: PASS (action-engine type)"
}

# --- server commodity / ARexx port check (phase 0.2 release gate) --------
# The actual phase 0.2 release gate (docs/implementation-plan.md): "an
# ARexx script clicks a button on the test app and asserts [state]
# changed" -- with no host involved. Boots the fixture and AmiPilotServer,
# then runs tests/copperline/arexx-test.rexx via the resident RexxMast
# (`rx`, so RC/RESULT populate exactly as a real script would see --
# rexxsyslib.library's IsRexxMsg() only validates messages whose
# rm_TaskBlock came from a live ARexx task, so this exercises the real
# interpreter, not a simulation of one). The script TYPEs into the Host
# string gadget and reads it back over the port (RC=0, RESULT="hello
# amipilot"), then CLICKs Connect and confirms the window is gone
# afterward (RC=5, "not found") -- state genuinely changed, observed
# entirely through ARexx.
run_arexx_check() {
	echo "run.sh: server commodity / ARexx port"

	rm -f "$BUILD/arexx-result.txt" "$BUILD/marker-arexx.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/fixtures/GTApp
Wait 5
Run SRC:build/AmiPilotServer
Wait 5
rx SRC:tests/copperline/arexx-test.rexx >SRC:build/arexx-result.txt
Wait 2
Echo "DONE" >SRC:build/marker-arexx.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (arexx): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until "{\"seconds\": $RUN_SECONDS}" > /dev/null
	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	if [ ! -f "$BUILD/marker-arexx.txt" ]; then
		echo "run.sh: FAIL (arexx): marker-arexx.txt never appeared -- crash or hang"
		FAILED=1
		return
	fi

	ok=1
	for pattern in \
		'GETTEXT-HOST RC=0 RESULT=hello amipilot' \
		'CLICK-CONNECT RC=0' \
		'TREE-AFTER RC=5' \
		'QUIT RC=0'; do
		if ! grep -qF "$pattern" "$BUILD/arexx-result.txt" 2>/dev/null; then
			echo "run.sh: FAIL (arexx): expected line not found: $pattern"
			ok=0
		fi
	done

	if [ "$ok" -eq 1 ]; then
		echo "run.sh: PASS (server commodity / ARexx port)"
	else
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/arexx-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

# --- manifest locator check (phase 0.3, manifest/SPEC.md) -----------------
# Same shape as the ARexx check above, but every gadget reference in
# tests/copperline/arexx-manifest-test.rexx is a logical name from
# fixtures/gadtools-app/GTApp.manifest -- no GA_ID, window title, or
# position appears in the script at all. Asserts the load report, that
# an unknown name is a clean RC=10 (not a crash or a silent no-op), the
# typed-text round trip, and the click-then-window-gone sequence.
run_manifest_check() {
	echo "run.sh: manifest locators"

	rm -f "$BUILD/manifest-result.txt" "$BUILD/marker-manifest.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/fixtures/GTApp
Wait 5
Run SRC:build/AmiPilotServer
Wait 5
rx SRC:tests/copperline/arexx-manifest-test.rexx >SRC:build/manifest-result.txt
Wait 2
Echo "DONE" >SRC:build/marker-manifest.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (manifest): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until "{\"seconds\": $RUN_SECONDS}" > /dev/null
	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	if [ ! -f "$BUILD/marker-manifest.txt" ]; then
		echo "run.sh: FAIL (manifest): marker-manifest.txt never appeared -- crash or hang"
		FAILED=1
		return
	fi

	ok=1
	for pattern in \
		'LOAD RC=0 RESULT=loaded GTApp: 1 windows, 3 gadgets' \
		'BADNAME RC=10' \
		'GETTEXT-HOST RC=0 RESULT=aminet.net' \
		'CLICK-CONNECT RC=0' \
		'GONE RC=5' \
		'QUIT RC=0'; do
		if ! grep -qF "$pattern" "$BUILD/manifest-result.txt" 2>/dev/null; then
			echo "run.sh: FAIL (manifest): expected line not found: $pattern"
			ok=0
		fi
	done

	if [ "$ok" -eq 1 ]; then
		echo "run.sh: PASS (manifest locators)"
	else
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/manifest-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

# --- wire protocol check (phase 0.3, server/WIRE.md) ----------------------
# The host half of the phase 0.3 loop: AmiPilotServer's SERIAL transport
# carried over Copperline's serial TCP bridge (--serial tcp, guest
# serial.device <-> host 127.0.0.1:1234), driven by the real host client
# (host/amipilot/wire.py) via tests/copperline/wire-test.py. Differs
# structurally from the checks above: the host must talk WHILE the
# machine runs, so run_until is issued in the background as a free-run
# and the assertions happen on the host's own wall clock.
run_wire_check() {
	echo "run.sh: wire protocol (serial)"

	rm -f "$BUILD/wire-result.txt" "$BUILD/marker-wire-ready.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/fixtures/GTApp
Wait 5
Run >NIL: SRC:build/AmiPilotServer SERIAL
Wait 5
Echo "READY" >SRC:build/marker-wire-ready.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --serial tcp --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (wire): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	# Free-run in the background: the ctl call blocks until the (large)
	# target is reached or the emulator dies, and the guest boots, runs
	# the server, and answers the wire meanwhile.
	"$COPPERLINE_CTL" --info "$info" run_until '{"seconds": 600}' > /dev/null 2>&1 &

	tries=0
	while [ ! -f "$BUILD/marker-wire-ready.txt" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 120 ]; then
			echo "run.sh: FAIL (wire): guest never became ready -- crash or hang"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			rm -f "$info"
			return
		fi
		sleep 0.5
	done

	python3 "$REPO_ROOT/tests/copperline/wire-test.py" 127.0.0.1:1234 \
		> "$BUILD/wire-result.txt" 2>&1 || true

	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	ok=1
	for pattern in \
		'PROTOCOL=1 STABLE=VERSION' \
		'BADVERB RC=10' \
		'GETTEXT-HOST RC=0 RESULT=hello wire' \
		'CLICK-CONNECT RC=0' \
		'TREE-AFTER RC=5' \
		'QUIT RC=0'; do
		if ! grep -qF "$pattern" "$BUILD/wire-result.txt" 2>/dev/null; then
			echo "run.sh: FAIL (wire): expected line not found: $pattern"
			ok=0
		fi
	done

	if [ "$ok" -eq 1 ]; then
		echo "run.sh: PASS (wire protocol / serial)"
	else
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/wire-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

# --- TCP transport over Copperline 0.15's real hostsocket net="host"
# backend (phase 1.0, GitHub issue #55) --------------------------------
# Unlike run_wire_check above (which carries the wire over a serial-to-
# TCP bridge -- AmiPilotServer SERIAL, guest-side serial.device), this
# drives AmiPilotServer's OWN TCP transport (bsdsocket.library, real
# listen()/accept()) via Copperline's `--hostsocket-net host` --
# delegates straight to a real host OS socket, so the host Python
# client below connects directly to the port AmiPilotServer itself
# bound inside the guest. No bridge, no /dev/bpf, no root, no static
# interface/address/gateway config at all -- unlike the `bridge`
# backend's own real setup cost (see this project's own Copperline-
# hostsocket notes). Confirmed independently of run_wire_check's own
# serial-bridge path so a regression in either transport is caught on
# its own.
run_tcp_host_check() {
	echo "run.sh: TCP transport (hostsocket net=host)"

	rm -f "$BUILD/tcp-host-result.txt" "$BUILD/marker-tcphost-ready.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/fixtures/GTApp
Wait 5
Run >NIL: SRC:build/AmiPilotServer TCP TCPPORT=1236
Wait 5
Echo "READY" >SRC:build/marker-tcphost-ready.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --hostsocket-net host --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (tcp-host): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until '{"seconds": 600}' > /dev/null 2>&1 &

	tries=0
	while [ ! -f "$BUILD/marker-tcphost-ready.txt" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 120 ]; then
			echo "run.sh: FAIL (tcp-host): guest never became ready -- crash or hang"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			rm -f "$info"
			return
		fi
		sleep 0.5
	done

	python3 "$REPO_ROOT/tests/copperline/tcp-host-test.py" 127.0.0.1:1236 \
		> "$BUILD/tcp-host-result.txt" 2>&1 || true

	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	ok=1
	for pattern in \
		'TREE PASS' \
		'CLICK PASS'; do
		if ! grep -qF "$pattern" "$BUILD/tcp-host-result.txt" 2>/dev/null; then
			echo "run.sh: FAIL (tcp-host): expected line not found: $pattern"
			ok=0
		fi
	done

	if [ "$ok" -eq 1 ]; then
		echo "run.sh: PASS (TCP transport / hostsocket net=host)"
	else
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/tcp-host-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

# --- LAUNCH verb check (phase 0.4) -----------------------------------------
# Unlike every check above, this one's smoke.script stages ONLY
# AmiPilotServer -- no fixture pre-launched via User-Startup. Proves
# the actual point of LAUNCH: a host session connects to a bare
# server, confirms nothing is running yet, starts the fixture *over
# the wire* with a non-default STACK, and drives it exactly as if it
# had been pre-staged -- tests/copperline/launch-test.py.
run_launch_check() {
	echo "run.sh: LAUNCH verb"

	rm -f "$BUILD/launch-result.txt" "$BUILD/marker-launch-ready.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/AmiPilotServer SERIAL
Wait 5
Echo "READY" >SRC:build/marker-launch-ready.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --serial tcp --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (launch): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until '{"seconds": 600}' > /dev/null 2>&1 &

	tries=0
	while [ ! -f "$BUILD/marker-launch-ready.txt" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 120 ]; then
			echo "run.sh: FAIL (launch): guest never became ready -- crash or hang"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			rm -f "$info"
			return
		fi
		sleep 0.5
	done

	python3 "$REPO_ROOT/tests/copperline/launch-test.py" 127.0.0.1:1234 \
		> "$BUILD/launch-result.txt" 2>&1 || true

	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	ok=1
	for pattern in \
		'PRELAUNCH-CHECK PASS' \
		'LAUNCH-SENT OK' \
		'WINDOW-APPEARED PASS title=AmiPilot GadTools Fixture' \
		'GETTEXT RESULT=launched via wire' \
		'WINDOW-GONE PASS'; do
		if ! grep -qF "$pattern" "$BUILD/launch-result.txt" 2>/dev/null; then
			echo "run.sh: FAIL (launch): expected line not found: $pattern"
			ok=0
		fi
	done

	if [ "$ok" -eq 1 ]; then
		echo "run.sh: PASS (LAUNCH verb)"
	else
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/launch-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

# --- SCREENSHOT check (phase 1.0, GitHub issue #41: "SCREENSHOT verb:
# raw pixel capture, host-side PNG encoding") ----------------------------
# Only GTApp needs staging -- SCREENSHOT captures whatever's already on
# screen, no dedicated fixture of its own. tests/copperline/
# screenshot-test.py exercises a bare (default-screen) capture, a
# WINDOW= capture (non-empty crop rect), writes both a real .iff (IFF
# ILBM) and .png to the HOST filesystem via Screenshot.save() and
# checks their magic bytes, and confirms a bad SCREEN= substring is
# rejected. amipilot.screenshot's own parsing/encoding correctness
# (exact byte layout, chunk CRCs, IDAT round-trip) has its own
# dedicated host-side unit tests (host/tests/test_screenshot.py) -- this
# check is about a REAL capture from a REAL running screen, not the
# format logic itself.
run_screenshot_check() {
	echo "run.sh: SCREENSHOT verb"

	rm -f "$BUILD/screenshot-result.txt" "$BUILD/marker-screenshot-ready.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/fixtures/GTApp
Wait 5
Run >NIL: SRC:build/AmiPilotServer SERIAL
Wait 5
Echo "READY" >SRC:build/marker-screenshot-ready.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --serial tcp --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (screenshot): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until '{"seconds": 600}' > /dev/null 2>&1 &

	tries=0
	while [ ! -f "$BUILD/marker-screenshot-ready.txt" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 120 ]; then
			echo "run.sh: FAIL (screenshot): guest never became ready -- crash or hang"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			rm -f "$info"
			return
		fi
		sleep 0.5
	done

	python3 "$REPO_ROOT/tests/copperline/screenshot-test.py" 127.0.0.1:1234 \
		> "$BUILD/screenshot-result.txt" 2>&1 || true

	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	ok=1
	for pattern in \
		'BARE-CAPTURE PASS' \
		'WINDOW-CROP PASS' \
		'WINDOW-CROP-DEFAULT PASS' \
		'ILBM-FILE PASS' \
		'PNG-FILE PASS' \
		'BAD-SCREEN PASS rejected'; do
		if ! grep -qF "$pattern" "$BUILD/screenshot-result.txt" 2>/dev/null; then
			echo "run.sh: FAIL (screenshot): expected line not found: $pattern"
			ok=0
		fi
	done

	if [ "$ok" -eq 1 ]; then
		echo "run.sh: PASS (SCREENSHOT verb)"
	else
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/screenshot-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

# --- SCREENSHOT P96/Picasso96 RTG check (phase 1.0, GitHub issue #55) -----
# Unlike run_screenshot_check above (classic planar only -- Copperline
# had no RTG emulation until 0.15), this exercises the P96-ACTIVE
# capture path against a REAL Picasso96/RTG board under Copperline
# itself, using its own [rtg] card=picasso2 support. SKIP-SAFE BY
# DESIGN: most machines running make test-target won't have [rtg]
# configured in their own copperline.local.toml at all (it's opt-in --
# see copperline.example.toml), and even with a board configured, the
# right monitor driver has to be installed and bound on the Workbench
# disk before Picasso96API.library can offer any display mode (a real
# gap this project hit and fixed live -- see this directory's own
# README). fixtures/p96-app itself detects and reports exactly this
# via a real SRC:build/p96-status.txt status file
# (tests/copperline/screenshot-p96-test.py reads it directly off the
# host filesystem, not over the wire -- Run's own >file redirection
# was confirmed unreliable for a launched process's real stdout during
# this feature's development); a `SKIP ...` status is a genuine,
# honest skip here, not a failure, matching the whole make test-target
# gate's own "skip cleanly, don't falsely pass" precedent for
# copperline.local.toml itself.
#
# Every check in this file (not just this one) uses --cpu 68020
# (32-bit address bus) + --accelerator 8M (fast RAM at 0x08000000)
# rather than the --model A1200 default's own 68EC020 (24-bit) +
# --fast 8M (Zorro II, $200000) -- discovered getting THIS check
# working: RTG needs real Zorro II autoconfig space, and the 24-bit
# profile doesn't have room for it once 8M of Zorro II fast RAM claims
# most of it, so a board here autoconfigures at address $00000000 and
# gets shut down by the OS's own Expansion Board Diagnostic, which
# also blocks headless boot outright (needs a manual "Continue"
# click). Applied to every check, not just this one, both because it's
# a straightforwardly more correct default (CLAUDE.md's own
# "Recommended/CI-tested config" already says plain "68020", not
# "68EC020" -- this project's own A1200 checks were quietly emulating
# the wrong CPU variant all along) and because a real A1200 has no
# built-in Zorro II slots at all -- actual A1200 fast-RAM upgrades are
# always accelerator-slot cards, so --accelerator is the more
# historically accurate choice too, not just a workaround for this one
# check's own needs.
run_screenshot_p96_check() {
	echo "run.sh: SCREENSHOT P96/Picasso96 RTG"

	rm -f "$BUILD/screenshot-p96-result.txt" "$BUILD/marker-screenshot-p96-ready.txt" "$BUILD/p96-status.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/AmiPilotServer SERIAL
Wait 5
Echo "READY" >SRC:build/marker-screenshot-p96-ready.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 \
		--chip 2M --accelerator 8M --noaudio --serial tcp --control :0 \
		--control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (screenshot-p96): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until '{"seconds": 600}' > /dev/null 2>&1 &

	tries=0
	while [ ! -f "$BUILD/marker-screenshot-p96-ready.txt" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 120 ]; then
			echo "run.sh: FAIL (screenshot-p96): guest never became ready -- crash or hang"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			rm -f "$info"
			return
		fi
		sleep 0.5
	done

	python3 "$REPO_ROOT/tests/copperline/screenshot-p96-test.py" 127.0.0.1:1234 \
		> "$BUILD/screenshot-p96-result.txt" 2>&1 || true

	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	if grep -qF 'P96 SKIP' "$BUILD/screenshot-p96-result.txt" 2>/dev/null; then
		echo "run.sh: SKIP (SCREENSHOT P96/Picasso96 RTG): $(grep -F 'P96 SKIP' "$BUILD/screenshot-p96-result.txt")"
		return
	fi

	ok=1
	for pattern in \
		'WINDOW PASS' \
		'CAPTURE PASS' \
		'RAMP PASS' \
		'CLOSE PASS'; do
		if ! grep -qF "$pattern" "$BUILD/screenshot-p96-result.txt" 2>/dev/null; then
			echo "run.sh: FAIL (screenshot-p96): expected line not found: $pattern"
			ok=0
		fi
	done

	if [ "$ok" -eq 1 ]; then
		echo "run.sh: PASS (SCREENSHOT P96/Picasso96 RTG)"
	else
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/screenshot-p96-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

# --- WBLAUNCH check (phase 1.0, docs/implementation-plan.md's "Program
# launch": "Workbench launch, done properly... The launched program
# experiences a real Workbench start") -----------------------------------
# smoke.script stages a real icon for fixtures/wbapp/WBApp (via its own
# MakeIcon helper, stamped BEFORE AmiPilotServer starts -- same "stage
# first, then Run the server" shape run_fs_check's own seed file uses)
# and starts the server with FSROOT=T: (WBApp's non-GUI fixture reports
# what it received to a T: file the wire's FSGET reads back). tests/
# copperline/wblaunch-test.py then exercises a bare launch (both baked-
# in tooltypes unchanged), a TOOLTYPE= override (PORT changes, GREETING
# untouched -- the real "merge", not a full replace), an ARG= project-
# file argument, and a bad-icon-path rejection, all against a real
# WBStartup handshake, not a hand-simulated one.
run_wblaunch_check() {
	echo "run.sh: WBLAUNCH verb"

	rm -f "$BUILD/wblaunch-result.txt" "$BUILD/marker-wblaunch-ready.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/fixtures/MakeIcon SRC:build/fixtures/WBApp
Wait 2
Run >NIL: SRC:build/AmiPilotServer SERIAL FSROOT=T:
Wait 5
Echo "READY" >SRC:build/marker-wblaunch-ready.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --serial tcp --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (wblaunch): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until '{"seconds": 600}' > /dev/null 2>&1 &

	tries=0
	while [ ! -f "$BUILD/marker-wblaunch-ready.txt" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 120 ]; then
			echo "run.sh: FAIL (wblaunch): guest never became ready -- crash or hang"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			rm -f "$info"
			return
		fi
		sleep 0.5
	done

	python3 "$REPO_ROOT/tests/copperline/wblaunch-test.py" 127.0.0.1:1234 \
		> "$BUILD/wblaunch-result.txt" 2>&1 || true

	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	ok=1
	for pattern in \
		'BARE-LAUNCH PASS' \
		'TOOLTYPE-OVERRIDE PASS' \
		'ARG-EXTRA PASS' \
		'BAD-ICON PASS rejected' \
		'SCRATCH-ICON-LEAK PASS no orphaned scratch icon'; do
		if ! grep -qF "$pattern" "$BUILD/wblaunch-result.txt" 2>/dev/null; then
			echo "run.sh: FAIL (wblaunch): expected line not found: $pattern"
			ok=0
		fi
	done

	if [ "$ok" -eq 1 ]; then
		echo "run.sh: PASS (WBLAUNCH verb)"
	else
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/wblaunch-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

# --- Bare-machine lifecycle check (phase 1.0, docs/implementation-plan.md's
# own "Success criteria": a Workbench-launch with an overridden tooltype and
# a project argument, driven and quit via its own affordances, "entirely
# self-contained against a bare machine over TCP -- fixtures staged via
# fs-put... no shared drive, mount, or emulator involved"). Unlike
# run_wblaunch_check above (which stages WBApp's own icon and launches it
# both via the SRC: hostfs mount), this check's OWN smoke.script does only
# two things -- run MakeIcon once (still fundamentally needs a real Amiga
# environment, since it reads the system's own live default WBTOOL icon;
# not different in kind from fixtures/wbgui-app/src/main.c itself being a
# pre-built cross-compiled artifact) to produce a reusable
# fixtures/wbgui-app/WBGuiApp.info on the host disk, then start
# `AmiPilotServer TCP FSROOT=T:` -- no fixture launch via SRC: at all.
# Everything else (staging the binary/icon/project-arg file, launching,
# driving, harvesting, cleanup) happens purely over the wire from
# tests/copperline/bare-lifecycle-test.py; see that script's own header for
# the full reasoning, including why fixtures/wbgui-app is a separate
# fixture from wbapp/WBApp rather than a modification of it.
run_bare_lifecycle_check() {
	echo "run.sh: bare-machine lifecycle (WBLAUNCH+FSPUT+FSGET over TCP)"

	rm -f "$BUILD/lifecycle-result.txt" "$BUILD/marker-lifecycle-ready.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/fixtures/MakeIcon SRC:build/fixtures/WBGuiApp
Wait 2
Run >NIL: SRC:build/AmiPilotServer TCP TCPPORT=1237 FSROOT=T:
Wait 5
Echo "READY" >SRC:build/marker-lifecycle-ready.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --hostsocket-net host --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (lifecycle): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until '{"seconds": 600}' > /dev/null 2>&1 &

	tries=0
	while [ ! -f "$BUILD/marker-lifecycle-ready.txt" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 120 ]; then
			echo "run.sh: FAIL (lifecycle): guest never became ready -- crash or hang"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			rm -f "$info"
			return
		fi
		sleep 0.5
	done

	python3 "$REPO_ROOT/tests/copperline/bare-lifecycle-test.py" 127.0.0.1:1237 \
		> "$BUILD/lifecycle-result.txt" 2>&1 || true

	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	ok=1
	for pattern in \
		'STAGE PASS' \
		'LAUNCH PASS' \
		'WINDOW PASS' \
		'OVERRIDE PASS' \
		'CLOSE PASS' \
		'HARVEST PASS' \
		'CLEANUP PASS'; do
		if ! grep -qF "$pattern" "$BUILD/lifecycle-result.txt" 2>/dev/null; then
			echo "run.sh: FAIL (lifecycle): expected line not found: $pattern"
			ok=0
		fi
	done

	if [ "$ok" -eq 1 ]; then
		echo "run.sh: PASS (bare-machine lifecycle)"
	else
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/lifecycle-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

# --- stock-app conformance check (phase 0.5, docs/implementation-plan.md's
# "Testing strategy": "Foreign-app tier tested against a fixed set of stock
# programs... with golden interaction scripts") -----------------------------
# Same shape as run_launch_check (only AmiPilotServer staged; the target
# itself -- SYS:Prefs/Time, a genuine OS-shipped Prefs editor, not a
# hand-written fixture -- is started over the wire via LAUNCH).
# tests/copperline/stock-app-test.py's own header explains, in detail, what
# was actually tried and confirmed live (its year field and "Save" button
# turned out to be inert under this profile's `rtc: none` config -- an
# honest finding, not a bug this check works around) and settles on
# dragging a slider plus tier-2 ROLE=button INDEX=1 ("Use") as the
# genuinely-verified "open, change a setting, exit via the app's own
# affordances" path. Mutates no persistent Workbench: state, unlike some
# other checks here, so no backup/restore step is needed.
run_stock_app_check() {
	echo "run.sh: stock-app conformance (Time Preferences)"

	rm -f "$BUILD/stock-result.txt" "$BUILD/marker-stock-ready.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/AmiPilotServer SERIAL
Wait 5
Echo "READY" >SRC:build/marker-stock-ready.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --serial tcp --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (stock-app): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until '{"seconds": 600}' > /dev/null 2>&1 &

	tries=0
	while [ ! -f "$BUILD/marker-stock-ready.txt" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 120 ]; then
			echo "run.sh: FAIL (stock-app): guest never became ready -- crash or hang"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			rm -f "$info"
			return
		fi
		sleep 0.5
	done

	python3 "$REPO_ROOT/tests/copperline/stock-app-test.py" 127.0.0.1:1234 \
		> "$BUILD/stock-result.txt" 2>&1 || true

	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	ok=1
	for pattern in \
		'LAUNCH-SENT OK' \
		'WINDOW-APPEARED PASS' \
		'MINUTES-DRAGGED OK' \
		'USE-CLICKED OK' \
		'WINDOW-GONE PASS'; do
		if ! grep -qF "$pattern" "$BUILD/stock-result.txt" 2>/dev/null; then
			echo "run.sh: FAIL (stock-app): expected line not found: $pattern"
			ok=0
		fi
	done

	if [ "$ok" -eq 1 ]; then
		echo "run.sh: PASS (stock-app conformance)"
	else
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/stock-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

# --- MUI-ARexx bridge check (phase 0.5) ------------------------------------
# MUI itself is a third-party archive, NOT part of any standard Workbench
# install (unlike SYS:Prefs/Time above) -- present here only because this
# developer installed it by hand. Skips cleanly (not a false pass, same
# discipline copperline.local.toml's own absence already gets from the
# Makefile) when Workbench:MUI/Demos/MUI-Demo isn't found on the configured
# Workbench volume, rather than failing for anyone else's checkout.
run_mui_check() {
	echo "run.sh: MUI-ARexx bridge"

	wb_path=$(awk '
		/^path = / { p = $0; sub(/^path = "/, "", p); sub(/"$/, "", p) }
		/volume = "Workbench"/ { print p; exit }
	' "$CONFIG")

	if [ -z "$wb_path" ] || [ ! -f "$wb_path/MUI/Demos/MUI-Demo" ]; then
		echo "run.sh: SKIP (mui): MUI not installed on the configured Workbench volume"
		return
	fi

	rm -f "$BUILD/mui-result.txt" "$BUILD/marker-mui-ready.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Assign MUI: Workbench:MUI
Assign add LIBS: MUI:Libs
Run >NIL: MUI:Demos/MUI-Demo
Wait 10
Run >NIL: SRC:build/AmiPilotServer SERIAL
Wait 5
Echo "READY" >SRC:build/marker-mui-ready.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --serial tcp --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (mui): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until '{"seconds": 600}' > /dev/null 2>&1 &

	tries=0
	while [ ! -f "$BUILD/marker-mui-ready.txt" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 150 ]; then
			echo "run.sh: FAIL (mui): guest never became ready -- crash or hang"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			rm -f "$info"
			return
		fi
		sleep 0.5
	done

	python3 "$REPO_ROOT/tests/copperline/mui-test.py" 127.0.0.1:1234 \
		> "$BUILD/mui-result.txt" 2>&1 || true

	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	ok=1
	for pattern in \
		'INFO-TITLE RESULT=MUI-Demo' \
		'NOTFOUND-CHECK PASS' \
		'APPERROR-CHECK PASS' \
		'BEFORE-QUIT PASS' \
		'QUIT-SENT OK' \
		'AFTER-QUIT PASS'; do
		if ! grep -qF "$pattern" "$BUILD/mui-result.txt" 2>/dev/null; then
			echo "run.sh: FAIL (mui): expected line not found: $pattern"
			ok=0
		fi
	done

	if [ "$ok" -eq 1 ]; then
		echo "run.sh: PASS (MUI-ARexx bridge)"
	else
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/mui-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

# --- MENU/MENUPICK check (phase 0.4) ---------------------------------
# fixtures/gadtools-app carries a menu strip (see its own header
# comment): Project > About(shortcut A)/Toggle(checkit)/Disabled/
# separator/More>Sub Item(shortcut S). tests/copperline/menu-test.py
# walks it via MENU, asserts the fields the walker read off Intuition's
# live struct Menu/MenuItem chain, MENUPICKs About and Sub Item by
# their keyboard shortcuts and confirms each pick genuinely reached
# the app (GTApp's own IDCMP_MENUPICK handler writes a marker into its
# Host string gadget, read back via GETTEXT), and confirms the
# permanently-disabled item is rejected without sending a keystroke.
run_menu_check() {
	echo "run.sh: MENU/MENUPICK"

	rm -f "$BUILD/menu-result.txt" "$BUILD/marker-menu-ready.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/fixtures/GTApp
Wait 5
Run >NIL: SRC:build/AmiPilotServer SERIAL
Wait 5
Echo "READY" >SRC:build/marker-menu-ready.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --serial tcp --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (menu): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until '{"seconds": 600}' > /dev/null 2>&1 &

	tries=0
	while [ ! -f "$BUILD/marker-menu-ready.txt" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 120 ]; then
			echo "run.sh: FAIL (menu): guest never became ready -- crash or hang"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			rm -f "$info"
			return
		fi
		sleep 0.5
	done

	python3 "$REPO_ROOT/tests/copperline/menu-test.py" 127.0.0.1:1234 \
		> "$BUILD/menu-result.txt" 2>&1 || true

	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	ok=1
	for pattern in \
		'MENU-STRUCTURE PASS' \
		'MENU-FIELDS PASS' \
		'MENUPICK-ABOUT PASS' \
		'MENUPICK-SUBITEM PASS' \
		'MENUPICK-TOGGLE-POINTER PASS' \
		'MENUPICK-SUBITEM-POINTER PASS' \
		'MENUPICK-DISABLED PASS'; do
		if ! grep -qF "$pattern" "$BUILD/menu-result.txt" 2>/dev/null; then
			echo "run.sh: FAIL (menu): expected line not found: $pattern"
			ok=0
		fi
	done

	if [ "$ok" -eq 1 ]; then
		echo "run.sh: PASS (MENU/MENUPICK)"
	else
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/menu-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

# --- multi-screen check (phase 0.4, SCREENS + SCREEN=) --------------------
# Stages fixtures/gadtools-app (opens on the default Workbench screen)
# THEN fixtures/second-screen-app (opens its own custom screen, which
# becomes frontmost as a result) -- the actual scenario this feature
# exists for: a window needing its background screen brought forward
# before it can be driven. tests/copperline/screens-test.py asserts
# SCREENS/SCREEN= (via wait_for_screen(), exercising that polling
# helper live), disambiguates two loosely-matching windows by screen,
# and -- the core proof -- confirms CLICK/TYPE against GTApp's window
# on the now-background Workbench screen still works, i.e. the
# existing ScreenToFront()/WindowToFront()/ActivateWindow() calls in
# AmipClickGadget() genuinely bring a background screen forward.
run_screens_check() {
	echo "run.sh: multi-screen (SCREENS/SCREEN=)"

	rm -f "$BUILD/screens-result.txt" "$BUILD/marker-screens-ready.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/fixtures/GTApp
Wait 5
Run >NIL: SRC:build/fixtures/SecondScreenApp
Wait 5
Run >NIL: SRC:build/AmiPilotServer SERIAL
Wait 5
Echo "READY" >SRC:build/marker-screens-ready.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --serial tcp --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (screens): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until '{"seconds": 600}' > /dev/null 2>&1 &

	tries=0
	while [ ! -f "$BUILD/marker-screens-ready.txt" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 120 ]; then
			echo "run.sh: FAIL (screens): guest never became ready -- crash or hang"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			rm -f "$info"
			return
		fi
		sleep 0.5
	done

	python3 "$REPO_ROOT/tests/copperline/screens-test.py" 127.0.0.1:1234 \
		> "$BUILD/screens-result.txt" 2>&1 || true

	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	ok=1
	for pattern in \
		'SCREENS PASS' \
		'LOOSE-MATCH PASS' \
		'SCREEN-DISAMBIGUATE PASS' \
		'BACKGROUND-TYPE PASS' \
		'BACKGROUND-CLICK PASS'; do
		if ! grep -qF "$pattern" "$BUILD/screens-result.txt" 2>/dev/null; then
			echo "run.sh: FAIL (screens): expected line not found: $pattern"
			ok=0
		fi
	done

	if [ "$ok" -eq 1 ]; then
		echo "run.sh: PASS (multi-screen)"
	else
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/screens-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

# --- WINDOWMOVE/WINDOWSIZE check (phase 1.0) -------------------------
# Stages fixtures/gadtools-app (a drag-bar-only window, for the "no
# sizing gadget" honest-failure case) and fixtures/second-screen-app
# (the only fixture with a real WA_SizeGadget -- see its own header
# comment for why neither golden-tree-tested fixture gets one).
# tests/copperline/windowmoveresize-test.py drives a real title-bar
# drag (WINDOWMOVE) and a real sizing-gadget drag (WINDOWSIZE) against
# it, confirming the actual resulting position/size via TREE
# afterward, plus the "no drag bar"/"no sizing gadget" and "no
# matching window" rejection cases.
run_windowmoveresize_check() {
	echo "run.sh: WINDOWMOVE/WINDOWSIZE"

	rm -f "$BUILD/windowmoveresize-result.txt" "$BUILD/marker-windowmoveresize-ready.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/fixtures/GTApp
Wait 5
Run >NIL: SRC:build/fixtures/SecondScreenApp
Wait 5
Run >NIL: SRC:build/AmiPilotServer SERIAL
Wait 5
Echo "READY" >SRC:build/marker-windowmoveresize-ready.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --serial tcp --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (windowmoveresize): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until '{"seconds": 600}' > /dev/null 2>&1 &

	tries=0
	while [ ! -f "$BUILD/marker-windowmoveresize-ready.txt" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 120 ]; then
			echo "run.sh: FAIL (windowmoveresize): guest never became ready -- crash or hang"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			rm -f "$info"
			return
		fi
		sleep 0.5
	done

	python3 "$REPO_ROOT/tests/copperline/windowmoveresize-test.py" 127.0.0.1:1234 \
		> "$BUILD/windowmoveresize-result.txt" 2>&1 || true

	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	ok=1
	for pattern in \
		'WINDOWMOVE PASS' \
		'WINDOWSIZE PASS' \
		'WINDOWSIZE-NO-SIZEGADGET PASS rejected' \
		'WINDOWMOVE-NO-MATCH PASS rejected'; do
		if ! grep -qF "$pattern" "$BUILD/windowmoveresize-result.txt" 2>/dev/null; then
			echo "run.sh: FAIL (windowmoveresize): expected line not found: $pattern"
			ok=0
		fi
	done

	if [ "$ok" -eq 1 ]; then
		echo "run.sh: PASS (WINDOWMOVE/WINDOWSIZE)"
	else
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/windowmoveresize-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

# --- WAITFOR REQUESTER check (GitHub issue #52's detection-only slice) ----
# fixtures/gadtools-app's "Ask" button (GID_ASK) calls a real, blocking
# AutoRequest() -- a genuine window-attached struct Requester, not a
# simulation. Confirms both directions: WAITFOR REQUESTER correctly times
# out before the requester exists (no false positive against GTApp's own
# ordinary window), and succeeds once the Ask button is clicked and the
# requester is genuinely up.
run_requester_check() {
	echo "run.sh: WAITFOR REQUESTER + CLICK dismiss (issue #52)"

	rm -f "$BUILD/requester-result.txt" "$BUILD/marker-requester-ready.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/fixtures/GTApp
Wait 5
Run >NIL: SRC:build/AmiPilotServer SERIAL
Wait 5
Echo "READY" >SRC:build/marker-requester-ready.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --serial tcp --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (requester): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until '{"seconds": 600}' > /dev/null 2>&1 &

	tries=0
	while [ ! -f "$BUILD/marker-requester-ready.txt" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 120 ]; then
			echo "run.sh: FAIL (requester): guest never became ready -- crash or hang"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			rm -f "$info"
			return
		fi
		sleep 0.5
	done

	python3 "$REPO_ROOT/tests/copperline/requester-test.py" 127.0.0.1:1234 \
		> "$BUILD/requester-result.txt" 2>&1 || true

	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	ok=1
	for pattern in \
		'WINDOW-FOUND OK' \
		'NO-REQUESTER-YET PASS' \
		'ASK-CLICKED OK' \
		'REQUESTER-DETECTED PASS' \
		'REQUESTER-YES-CLICKED OK' \
		'REQUESTER-DISMISSED PASS'; do
		if ! grep -qF "$pattern" "$BUILD/requester-result.txt" 2>/dev/null; then
			echo "run.sh: FAIL (requester): expected line not found: $pattern"
			ok=0
		fi
	done

	if [ "$ok" -eq 1 ]; then
		echo "run.sh: PASS (WAITFOR REQUESTER + CLICK dismiss)"
	else
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/requester-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

# --- file API check (phase 0.4-1.0, allowlist-scoped FSLIST/FSSTAT/
# FSMKDIR/FSDELETE/FSGET/FSPUT) -----------------------------------------
# smoke.script stages a granted root (RAM:amipilot-fs-test, created and
# seeded with a small file BEFORE AmiPilotServer starts -- FSROOT is a
# Lock() at startup, so the directory must already exist) and starts
# the server with FSROOT=RAM:amipilot-fs-test. tests/copperline/
# fs-test.py then exercises the happy path (list/stat/get the seeded
# file, mkdir/stat/delete a subdirectory, FSPUT a new file and read it
# back byte-for-byte -- over this check's own serial.device transport,
# exercising AmipSerialReadExact()'s read-the-declared-payload path)
# and the containment check (FSLIST/FSPUT against SYS: must be
# rejected, not served) over the wire.
run_fs_check() {
	echo "run.sh: file API"

	rm -f "$BUILD/fs-result.txt" "$BUILD/marker-fs-ready.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
MakeDir RAM:amipilot-fs-test
Echo "seed data" >RAM:amipilot-fs-test/seed.txt
Run >NIL: SRC:build/AmiPilotServer SERIAL FSROOT=RAM:amipilot-fs-test
Wait 5
Echo "READY" >SRC:build/marker-fs-ready.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --serial tcp --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (fs): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until '{"seconds": 600}' > /dev/null 2>&1 &

	tries=0
	while [ ! -f "$BUILD/marker-fs-ready.txt" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 120 ]; then
			echo "run.sh: FAIL (fs): guest never became ready -- crash or hang"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			rm -f "$info"
			return
		fi
		sleep 0.5
	done

	python3 "$REPO_ROOT/tests/copperline/fs-test.py" 127.0.0.1:1234 \
		> "$BUILD/fs-result.txt" 2>&1 || true

	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	ok=1
	for pattern in \
		'FSLIST-SEED PASS' \
		"FSGET RESULT='seed data" \
		'FSMKDIR PASS' \
		'FSDELETE PASS' \
		'FSPUT PASS' \
		'FSPUT-CONTAINMENT PASS SYS: rejected' \
		'FSPUT-TOOLARGE PASS rejected' \
		'FSPUT-TOOLARGE-NO-DESYNC PASS connection still usable' \
		'CONTAINMENT PASS SYS: rejected'; do
		if ! grep -qF "$pattern" "$BUILD/fs-result.txt" 2>/dev/null; then
			echo "run.sh: FAIL (fs): expected line not found: $pattern"
			ok=0
		fi
	done

	if [ "$ok" -eq 1 ]; then
		echo "run.sh: PASS (file API)"
	else
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/fs-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

# --- golden-tree fixture check (phase 0.5, docs/implementation-plan.md's
# "Golden trees": "a saved dump doubles as a structural fixture") --------
# Both fixtures run together in one boot (only their windows' baseline
# structure is asserted, no interaction needed) so a single TREE round
# trip per fixture is enough. tests/copperline/golden-test.py compares
# each live tree against its checked-in fixtures/<app>/<App>.golden via
# amipilot.golden.assert_golden() -- a mismatch here is real UI drift
# this check exists to catch, matching CLASSIFY-style checks above but
# for the whole tree's shape at once rather than one asserted line.
# See README.md's "Golden-tree fixtures and Locale" section: these two
# golden files are locale-invariant because both fixtures hardcode
# plain C string labels/titles (no locale.library catalog) -- not a
# property golden trees have in general, for a real localized app.
run_golden_check() {
	echo "run.sh: golden-tree fixtures"

	rm -f "$BUILD/golden-result.txt" "$BUILD/marker-golden-ready.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/fixtures/GTApp
Run >NIL: SRC:build/fixtures/CAApp
Run >NIL: SRC:build/fixtures/ReactionClassesApp
Wait 5
Run >NIL: SRC:build/AmiPilotServer SERIAL
Wait 5
Echo "READY" >SRC:build/marker-golden-ready.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --serial tcp --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (golden): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until '{"seconds": 600}' > /dev/null 2>&1 &

	tries=0
	while [ ! -f "$BUILD/marker-golden-ready.txt" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 120 ]; then
			echo "run.sh: FAIL (golden): guest never became ready -- crash or hang"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			rm -f "$info"
			return
		fi
		sleep 0.5
	done

	python3 "$REPO_ROOT/tests/copperline/golden-test.py" 127.0.0.1:1234 \
		> "$BUILD/golden-result.txt" 2>&1 || true

	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	ok=1
	for pattern in 'GOLDEN-GTAPP MATCH' 'GOLDEN-CAAPP MATCH' 'GOLDEN-RCAPP MATCH'; do
		if ! grep -qF "$pattern" "$BUILD/golden-result.txt" 2>/dev/null; then
			echo "run.sh: FAIL (golden): expected line not found: $pattern"
			ok=0
		fi
	done

	if [ "$ok" -eq 1 ]; then
		echo "run.sh: PASS (golden-tree fixtures)"
	else
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/golden-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

# --- WHERE / cooperative geometry port check (issue #49) ------------------
# fixtures/classact-app's three gadgets are all layout.gadget children --
# permanently unreachable by GA_ID (the project's documented "Confirmed
# limit") -- so CAApp.manifest (format version 2) names every one as a
# WHEREGADGET, resolved through the fixture's own CAAPP.WHERE ARexx port
# instead. Launched directly by this check's own smoke script (same
# pattern run_mui_check uses for MUI-Demo). CAApp writes its own
# host-readable log itself (build/caapp-log.txt, opened directly via
# dos.library, not via "Run >file" -- see fixtures/classact-app/src/
# main.c's DiagFile() for why the latter didn't reliably work) -- the
# only external way to confirm TYPE @host_field's text genuinely landed
# in a gadget GETTEXT can't read back (see tests/copperline/where-
# test.py's own header for the full rationale).
run_where_check() {
	echo "run.sh: cooperative geometry port (WHERE)"

	rm -f "$BUILD/where-result.txt" "$BUILD/marker-where-ready.txt" "$BUILD/caapp-log.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/fixtures/CAApp
Wait 5
Run >NIL: SRC:build/AmiPilotServer SERIAL
Wait 5
Echo "READY" >SRC:build/marker-where-ready.txt
EOF

	info="$REPO_ROOT/build/.copperline-ctl-info-$$.json"
	rm -f "$info"
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --cpu 68020 --chip 2M --accelerator 8M \
		--noaudio --serial tcp --control :0 --control-info "$info" \
		> "$REPO_ROOT/build/.copperline-log-$$.txt" 2>&1 &
	COPPERLINE_PID=$!

	tries=0
	while [ ! -f "$info" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 100 ]; then
			echo "run.sh: FAIL (where): copperline never wrote $info"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			return
		fi
		sleep 0.1
	done

	"$COPPERLINE_CTL" --info "$info" run_until '{"seconds": 600}' > /dev/null 2>&1 &

	tries=0
	while [ ! -f "$BUILD/marker-where-ready.txt" ]; do
		tries=$((tries + 1))
		if [ "$tries" -gt 150 ]; then
			echo "run.sh: FAIL (where): guest never became ready -- crash or hang"
			FAILED=1
			kill "$COPPERLINE_PID" 2>/dev/null || true
			COPPERLINE_PID=""
			rm -f "$info"
			return
		fi
		sleep 0.5
	done

	python3 "$REPO_ROOT/tests/copperline/where-test.py" 127.0.0.1:1234 \
		> "$BUILD/where-result.txt" 2>&1 || true

	# Give CAApp's own process a moment to actually exit and flush its
	# redirected log after the click that closes its window -- the
	# Python script's own CLICK-VIA-WHERE/WINDOW-GONE checks only prove
	# the WINDOW closed, not that the guest process (and its Run-owned
	# output redirection) has finished tearing down yet.
	sleep 2

	kill "$COPPERLINE_PID" 2>/dev/null || true
	COPPERLINE_PID=""
	rm -f "$info"

	ok=1
	for pattern in \
		'WHERE-GEOMETRY PASS' \
		'UNKNOWN-NAME PASS' \
		'GETTEXT-LIMIT PASS' \
		'TYPE-VIA-WHERE SENT' \
		'CLICK-VIA-WHERE PASS' \
		'WINDOW-GONE PASS'; do
		if ! grep -qF "$pattern" "$BUILD/where-result.txt" 2>/dev/null; then
			echo "run.sh: FAIL (where): expected line not found: $pattern"
			ok=0
		fi
	done
	if ! grep -qF "caapp: host=AmigaTest" "$BUILD/caapp-log.txt" 2>/dev/null; then
		echo "run.sh: FAIL (where): CAApp's own log never showed the typed text landing in host_field"
		ok=0
	fi

	if [ "$ok" -eq 1 ]; then
		echo "run.sh: PASS (cooperative geometry port)"
	else
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/where-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		echo "run.sh:   --- CAApp's own log ---"
		sed 's/^/run.sh:   /' "$BUILD/caapp-log.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

# --- pytest release gate (phase 0.3, host/amipilot/pytest_plugin.py) ------
# The literal phase 0.3 release gate (docs/implementation-plan.md): "a
# host pytest clicks a button and asserts a label changed,
# deterministically, under the emulator." Unlike every check above,
# THIS ONE doesn't launch Copperline itself -- the `amipilot` pytest
# fixture does that, from --amipilot-config, exercising the plugin's own
# boot path (not just its unit-tested mock) against a real guest. Only
# the User-Startup staging (GTApp + AmiPilotServer SERIAL) is this
# script's job, same smoke.script mechanism every other check uses.
run_pytest_release_gate_check() {
	echo "run.sh: pytest release gate"

	if ! python3 -m pytest --version > /dev/null 2>&1; then
		echo "run.sh: pytest not installed -- run: pip install -e '$REPO_ROOT/host/[test]'"
		FAILED=1
		return
	fi

	rm -f "$BUILD/pytest-result.txt"
	cat > "$SMOKE_SCRIPT" <<EOF
Run >NIL: SRC:build/fixtures/GTApp
Wait 5
Run >NIL: SRC:build/AmiPilotServer SERIAL
EOF

	python3 -m pytest "$REPO_ROOT/tests/copperline/pytest-example" -v \
		--amipilot-config="$CONFIG" \
		--amipilot-copperline="$COPPERLINE" \
		--amipilot-copperline-ctl="$COPPERLINE_CTL" \
		--amipilot-boot-timeout=40 \
		> "$BUILD/pytest-result.txt" 2>&1
	rc=$?

	if [ "$rc" -eq 0 ] && grep -qE '1 passed' "$BUILD/pytest-result.txt"; then
		echo "run.sh: PASS (pytest release gate)"
	else
		echo "run.sh: FAIL (pytest release gate): exit=$rc"
		echo "run.sh:   --- actual output ---"
		sed 's/^/run.sh:   /' "$BUILD/pytest-result.txt" 2>/dev/null || echo "run.sh:   (empty)"
		FAILED=1
	fi
}

run_wbpattern_check
run_click_check
run_type_check
run_arexx_check
run_manifest_check
run_wire_check
run_tcp_host_check
run_launch_check
run_wblaunch_check
run_bare_lifecycle_check
run_screenshot_check
run_screenshot_p96_check
run_stock_app_check
run_mui_check
run_fs_check
run_menu_check
run_screens_check
run_windowmoveresize_check
run_requester_check
run_golden_check
run_where_check
run_pytest_release_gate_check

if [ "$FAILED" -eq 0 ]; then
	rm -f "$BUILD"/.copperline-ctl-info-*.json "$BUILD"/.copperline-log-*.txt \
		"$BUILD"/inspect-gadtools.txt "$BUILD"/inspect-classact.txt \
		"$BUILD"/marker-gt.txt "$BUILD"/marker-ca.txt \
		"$BUILD"/inspect-wbpattern.txt "$BUILD"/marker-wbp.txt \
		"$BUILD"/click-result.txt "$BUILD"/inspect-after-click.txt \
		"$BUILD"/marker-click.txt \
		"$BUILD"/type-result.txt "$BUILD"/inspect-after-type.txt \
		"$BUILD"/marker-type.txt \
		"$BUILD"/arexx-result.txt "$BUILD"/marker-arexx.txt \
		"$BUILD"/manifest-result.txt "$BUILD"/marker-manifest.txt \
		"$BUILD"/wire-result.txt "$BUILD"/marker-wire-ready.txt \
		"$BUILD"/launch-result.txt "$BUILD"/marker-launch-ready.txt \
		"$BUILD"/stock-result.txt "$BUILD"/marker-stock-ready.txt \
		"$BUILD"/mui-result.txt "$BUILD"/marker-mui-ready.txt \
		"$BUILD"/fs-result.txt "$BUILD"/marker-fs-ready.txt \
		"$BUILD"/menu-result.txt "$BUILD"/marker-menu-ready.txt \
		"$BUILD"/screens-result.txt "$BUILD"/marker-screens-ready.txt \
		"$BUILD"/golden-result.txt "$BUILD"/marker-golden-ready.txt \
		"$BUILD"/where-result.txt "$BUILD"/marker-where-ready.txt "$BUILD"/caapp-log.txt \
		"$BUILD"/pytest-result.txt
	echo "run.sh: all fixtures PASS"
	exit 0
else
	echo "run.sh: FAILED -- logs and output left under build/ for inspection"
	exit 1
fi
