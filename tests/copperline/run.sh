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
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --chip 2M \
		--fast 8M --noaudio --control :0 --control-info "$info" \
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
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --chip 2M \
		--fast 8M --noaudio --control :0 --control-info "$info" \
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
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --chip 2M \
		--fast 8M --noaudio --control :0 --control-info "$info" \
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
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --chip 2M \
		--fast 8M --noaudio --control :0 --control-info "$info" \
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
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --chip 2M \
		--fast 8M --noaudio --control :0 --control-info "$info" \
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
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --chip 2M \
		--fast 8M --noaudio --serial tcp --control :0 --control-info "$info" \
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
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --chip 2M \
		--fast 8M --noaudio --serial tcp --control :0 --control-info "$info" \
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
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --chip 2M \
		--fast 8M --noaudio --serial tcp --control :0 --control-info "$info" \
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

# --- file API check (phase 0.4, allowlist-scoped FSLIST/FSSTAT/FSMKDIR/
# FSDELETE/FSGET) ------------------------------------------------------
# smoke.script stages a granted root (RAM:amipilot-fs-test, created and
# seeded with a small file BEFORE AmiPilotServer starts -- FSROOT is a
# Lock() at startup, so the directory must already exist) and starts
# the server with FSROOT=RAM:amipilot-fs-test. tests/copperline/
# fs-test.py then exercises the happy path (list/stat/get the seeded
# file, mkdir/stat/delete a subdirectory) and the containment check
# (FSLIST SYS: must be rejected, not served) over the wire.
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
	"$COPPERLINE" --config "$CONFIG" --model A1200 --chipset AGA --chip 2M \
		--fast 8M --noaudio --serial tcp --control :0 --control-info "$info" \
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

run_click_check
run_type_check
run_arexx_check
run_manifest_check
run_wire_check
run_launch_check
run_fs_check
run_menu_check
run_pytest_release_gate_check

if [ "$FAILED" -eq 0 ]; then
	rm -f "$BUILD"/.copperline-ctl-info-*.json "$BUILD"/.copperline-log-*.txt \
		"$BUILD"/inspect-gadtools.txt "$BUILD"/inspect-classact.txt \
		"$BUILD"/marker-gt.txt "$BUILD"/marker-ca.txt \
		"$BUILD"/click-result.txt "$BUILD"/inspect-after-click.txt \
		"$BUILD"/marker-click.txt \
		"$BUILD"/type-result.txt "$BUILD"/inspect-after-type.txt \
		"$BUILD"/marker-type.txt \
		"$BUILD"/arexx-result.txt "$BUILD"/marker-arexx.txt \
		"$BUILD"/manifest-result.txt "$BUILD"/marker-manifest.txt \
		"$BUILD"/wire-result.txt "$BUILD"/marker-wire-ready.txt \
		"$BUILD"/launch-result.txt "$BUILD"/marker-launch-ready.txt \
		"$BUILD"/fs-result.txt "$BUILD"/marker-fs-ready.txt \
		"$BUILD"/menu-result.txt "$BUILD"/marker-menu-ready.txt \
		"$BUILD"/pytest-result.txt
	echo "run.sh: all fixtures PASS"
	exit 0
else
	echo "run.sh: FAILED -- logs and output left under build/ for inspection"
	exit 1
fi
