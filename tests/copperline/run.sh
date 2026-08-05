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

run_click_check
run_type_check

if [ "$FAILED" -eq 0 ]; then
	rm -f "$BUILD"/.copperline-ctl-info-*.json "$BUILD"/.copperline-log-*.txt \
		"$BUILD"/inspect-gadtools.txt "$BUILD"/inspect-classact.txt \
		"$BUILD"/marker-gt.txt "$BUILD"/marker-ca.txt \
		"$BUILD"/click-result.txt "$BUILD"/inspect-after-click.txt \
		"$BUILD"/marker-click.txt \
		"$BUILD"/type-result.txt "$BUILD"/inspect-after-type.txt \
		"$BUILD"/marker-type.txt
	echo "run.sh: all fixtures PASS"
	exit 0
else
	echo "run.sh: FAILED -- logs and output left under build/ for inspection"
	exit 1
fi
