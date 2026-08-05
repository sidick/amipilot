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

run_fixture "classact-app" "CAApp" "ClassAct" "inspect-classact.txt" "marker-ca.txt" \
	'class="layout.gadget"'

if [ "$FAILED" -eq 0 ]; then
	rm -f "$BUILD"/.copperline-ctl-info-*.json "$BUILD"/.copperline-log-*.txt \
		"$BUILD"/inspect-gadtools.txt "$BUILD"/inspect-classact.txt \
		"$BUILD"/marker-gt.txt "$BUILD"/marker-ca.txt
	echo "run.sh: all fixtures PASS"
	exit 0
else
	echo "run.sh: FAILED -- logs and output left under build/ for inspection"
	exit 1
fi
