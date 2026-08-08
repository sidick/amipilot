# AmiPilot -- object-level GUI automation for AmigaOS.
# See docs/implementation-plan.md for the full plan and phase sequencing.
#
#   make amiga   - build the on-Amiga pieces (intuition-model + AmiInspect)
#   make docker  - run 'make amiga' inside the cross-compiler container
#   make dist    - package build/dist/amipilot.lha for Aminet
#   make clean
#
# Also implements sidick/amiga-workflows' five-verb CI contract (build,
# test-host, test-target, lint, dist) -- see the "Verb contract" section
# below.
#
# Amiga toolchain: Bebbo m68k-amigaos GCC, via the shared sidick/amiga-dev
# image (ghcr.io/sidick/amiga-dev). Use 'make docker' if you don't have the
# cross-compiler installed locally.
#
# Target floor: AmigaOS 2.04 (V37), plain 68000, no FPU -- see
# docs/implementation-plan.md "Minimum requirements" for why.
#
# Sequencing (docs/implementation-plan.md phases): 0.1 ships intuition-model
# + AmiInspect only, both independently useful on-Amiga with no host or
# transport involved. server/ (0.2, ARexx-driven) and host/ (0.3+, wire
# protocol + Python client) are scaffolded but not yet wired into this
# Makefile -- they gain real build rules as their phases land.

BUILD := build

include version.mk

CC := m68k-amigaos-gcc
AR := m68k-amigaos-ar

# -m68000/-msoft-float: the actual target floor, not just a default -- see
# "Minimum requirements" above. -noixemul links against libnix, the
# static C library for GCC on classic Amiga (no ixemul.library dependency
# at runtime) -- see the libnix reference for startup/library-opening
# conventions this code follows.
CFLAGS := -O2 -Wall -Wextra -Werror -m68000 -msoft-float -noixemul \
          -DVERSION=$(VERSION) -DREVISION=$(REVISION)

MODEL_SRCDIR := intuition-model/src
MODEL_INCDIR := intuition-model/include
MODEL_SRC    := $(MODEL_SRCDIR)/walk.c
MODEL_LIB    := $(BUILD)/libintuitionmodel.a

INSPECT_SRCDIR := amiinspect/src
INSPECT_BIN    := $(BUILD)/AmiInspect

GADTOOLS_APP_SRC := fixtures/gadtools-app/src/main.c
GADTOOLS_APP_BIN := $(BUILD)/fixtures/GTApp

CLASSACT_APP_SRC := fixtures/classact-app/src/main.c
CLASSACT_APP_BIN := $(BUILD)/fixtures/CAApp

SECONDSCREEN_APP_SRC := fixtures/second-screen-app/src/main.c
SECONDSCREEN_APP_BIN := $(BUILD)/fixtures/SecondScreenApp

# wbapp: a Workbench-startable (not GUI) fixture proving WBLAUNCH
# (phase 1.0, server/include/wblaunch.h) against a real WBStartup
# handshake -- see fixtures/wbapp/src/main.c. MakeIcon is its own
# build/test-time-only helper that stamps a real .info for it (no
# hand-authored binary icon file -- see makeicon.c's own header).
WBAPP_SRC       := fixtures/wbapp/src/main.c
WBAPP_BIN       := $(BUILD)/fixtures/WBApp
MAKEICON_SRC    := fixtures/wbapp/src/makeicon.c
MAKEICON_BIN    := $(BUILD)/fixtures/MakeIcon

# --- server/ (phase 0.2, in progress -- see docs/implementation-plan.md) --
ACTION_SRCDIR := server/src
ACTION_INCDIR := server/include
ACTION_SRC    := $(ACTION_SRCDIR)/action.c
ACTION_LIB    := $(BUILD)/libamipaction.a

# AmiClickTest is a dev-only scoping tool proving the action engine
# against fixtures/gadtools-app -- not a fixture, not the eventual server
# commodity. Not part of `amiga`/`build`/`docker`: it's not a real
# deliverable yet, so it doesn't belong in the surface CI/releases treat
# as such. See server/src/clicktest/main.c's own header comment.
CLICKTEST_SRCDIR := server/src/clicktest
CLICKTEST_BIN    := $(BUILD)/AmiClickTest

# AmiSetMouse: even smaller dev-only diagnostic -- the RKM's documented
# Set_Mouse.c pointer-positioning example almost verbatim, to verify the
# documented move mechanism in isolation before layering clicks on it.
SETMOUSE_SRCDIR := server/src/setmouse
SETMOUSE_BIN    := $(BUILD)/AmiSetMouse

# AmiPilotServer: the actual phase 0.2 deliverable -- a commodity hosting
# the action engine + intuition-model behind a genuine ARexx port
# ("AMIPILOT.<n>"). Unlike AmiClickTest/AmiSetMouse above, this one IS
# meant to ship; kept under `server` rather than `amiga`/`build` for now
# because phase 0.2 isn't tagged yet, not because it's a throwaway tool.
AREXX_SRC       := $(ACTION_SRCDIR)/arexx.c $(ACTION_SRCDIR)/arexx_cmd.c \
                   $(ACTION_SRCDIR)/manifest.c $(ACTION_SRCDIR)/serial.c \
                   $(ACTION_SRCDIR)/tcp.c $(ACTION_SRCDIR)/fs.c \
                   $(ACTION_SRCDIR)/muirexx.c $(ACTION_SRCDIR)/wblaunch.c
AMIPILOTD_SRCDIR := server/src/amipilotserver
AMIPILOTD_BIN    := $(BUILD)/AmiPilotServer

# --- Docker (shared toolchain image, see sidick/amiga-dev) ---
IMAGE      ?= ghcr.io/sidick/amiga-dev:1
# --user matches the container process's UID/GID to the host caller's, so
# every file `make amiga` creates under build/ (bind-mounted, not a
# container-private volume) is host-owned, not root-owned.
DOCKER_RUN := docker run --rm --user "$$(id -u):$$(id -g)" -v "$(CURDIR)":/work -w /work $(IMAGE)

.PHONY: all amiga fixtures server docker clean version build test-host test-target lint dist guide

all: amiga

amiga: $(INSPECT_BIN)

fixtures: $(GADTOOLS_APP_BIN) $(CLASSACT_APP_BIN) $(SECONDSCREEN_APP_BIN) $(WBAPP_BIN) $(MAKEICON_BIN)

server: $(CLICKTEST_BIN) $(SETMOUSE_BIN) $(AMIPILOTD_BIN)

$(MODEL_LIB): $(MODEL_SRC) $(MODEL_INCDIR)/intuition_model.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(MODEL_INCDIR) -c $(MODEL_SRC) -o $(BUILD)/walk.o
	$(AR) rcs $@ $(BUILD)/walk.o

$(INSPECT_BIN): $(INSPECT_SRCDIR)/main.c $(MODEL_LIB)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(MODEL_INCDIR) -o $@ $(INSPECT_SRCDIR)/main.c $(MODEL_LIB)

# Fixture apps link against system libraries only (intuition/gadtools/
# graphics), not intuition-model -- they are the thing being inspected,
# not a consumer of the walker.
$(GADTOOLS_APP_BIN): $(GADTOOLS_APP_SRC)
	@mkdir -p $(BUILD)/fixtures
	$(CC) $(CFLAGS) -o $@ $(GADTOOLS_APP_SRC)

# -lamiga: DoMethod/NewObject/GetAttr (alib.h) need amiga.lib's varargs
# marshaling -- confirmed necessary against real Kickstart 3.2 (silently
# broken without it: links clean, but the BOOPSI calls don't work).
$(CLASSACT_APP_BIN): $(CLASSACT_APP_SRC)
	@mkdir -p $(BUILD)/fixtures
	$(CC) $(CFLAGS) -o $@ $(CLASSACT_APP_SRC) -lamiga

$(SECONDSCREEN_APP_BIN): $(SECONDSCREEN_APP_SRC)
	@mkdir -p $(BUILD)/fixtures
	$(CC) $(CFLAGS) -o $@ $(SECONDSCREEN_APP_SRC)

$(WBAPP_BIN): $(WBAPP_SRC)
	@mkdir -p $(BUILD)/fixtures
	$(CC) $(CFLAGS) -o $@ $(WBAPP_SRC)

$(MAKEICON_BIN): $(MAKEICON_SRC)
	@mkdir -p $(BUILD)/fixtures
	$(CC) $(CFLAGS) -o $@ $(MAKEICON_SRC)

$(ACTION_LIB): $(ACTION_SRC) $(ACTION_INCDIR)/action_engine.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(ACTION_INCDIR) -c $(ACTION_SRC) -o $(BUILD)/action.o
	$(AR) rcs $@ $(BUILD)/action.o

$(CLICKTEST_BIN): $(CLICKTEST_SRCDIR)/main.c $(ACTION_LIB)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(ACTION_INCDIR) -o $@ $(CLICKTEST_SRCDIR)/main.c $(ACTION_LIB)

$(SETMOUSE_BIN): $(SETMOUSE_SRCDIR)/main.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(SETMOUSE_SRCDIR)/main.c

$(AMIPILOTD_BIN): $(AMIPILOTD_SRCDIR)/main.c $(AREXX_SRC) $(ACTION_LIB) $(MODEL_LIB)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(ACTION_INCDIR) -I$(MODEL_INCDIR) \
	  -o $@ $(AMIPILOTD_SRCDIR)/main.c $(AREXX_SRC) $(ACTION_LIB) $(MODEL_LIB)

docker:
	$(DOCKER_RUN) make amiga fixtures

# --- Verb contract (sidick/amiga-workflows' build-test.yml) ---------------
# Each build-test.yml job is independent (no artifact-passing between
# them), so test-target/dist below pull in `build` themselves rather than
# assuming a prior job already ran it.
build: amiga fixtures

# Host-side tests: wire client framing/handshake, the TREE parser, the
# object API, and the pytest plugin's own boot/skip logic (phase 0.3) --
# all against a scripted transport/subprocess, no emulator needed. Runs
# under pytest (a superset of unittest -- it collects the plain
# unittest.TestCase files here too, so this is one test run, not two);
# host/ is editable-installed first so amipilot.pytest_plugin registers
# via its real pytest11 entry point, exactly as a real consumer sees it,
# not force-loaded with -p.
test-host:
	pip install --quiet -e 'host/[test]'
	python3 -m pytest host/tests -v

# tests/copperline/run.sh boots both fixtures headlessly under Copperline
# and asserts AmiInspect's classification output -- but it needs
# tests/copperline/copperline.local.toml (gitignored: a real Kickstart
# ROM + Workbench install path, machine-specific), which CI doesn't have.
# run.sh itself skips (exit 0, not a false pass) when that file is
# absent, so this stays a real check locally and an honest no-op in CI
# rather than silently claiming coverage it can't have.
# server too: run.sh's action-engine click check needs AmiClickTest.
test-target: amiga fixtures server
	sh tests/copperline/run.sh

lint:
	pip install --quiet semgrep
	semgrep --config auto --error \
	  --include='*.c' --include='*.h' \
	  intuition-model/ amiinspect/ server/ fixtures/

version:
	@echo "$(VERSION).$(REVISION)"

# --- guide: AmigaGuide user documentation, generated from userdocs/ -------
# userdocs/ is the single source of truth for user docs (built as the
# MkDocs site, see mkdocs.yml); this converts it for on-Amiga reading
# (MultiView/AmigaGuide). @mkdir's own recipe line, not a `| $(BUILD)`
# order-only prerequisite on a separate $(BUILD): rule -- BUILD's value is
# literally the string "build", so a target named $(BUILD) would collide
# with this Makefile's own build: verb-contract target above.
guide:
	@mkdir -p $(BUILD)
	python3 tools/docs2guide.py userdocs $(BUILD)/amipilot.guide

# --- lha: build the real LHa for UNIX (archive-capable), pinned ------------
# Homebrew's and Ubuntu's `lha` is Lhasa -- extract-only, useless for
# packaging -- and the last lha *release* tag (2021) no longer compiles with
# modern compilers, so build a pinned master commit from source into
# build/tools/. Needs git + autoconf/automake. Override with a known-good
# archiver: make dist LHA=/path/to/real/lha
# Same pinned commit as sibling projects amiauth/sana2loop's own dist target.
LHA_REPO   := https://github.com/jca02266/lha.git
LHA_COMMIT := 86094cb56aba34de45668f39f74fcfb61e9d7fb6
LHA        ?= $(BUILD)/tools/lha

$(BUILD)/tools/lha:
	@mkdir -p $(BUILD)/tools
	rm -rf $(BUILD)/tools/lha-src
	git clone -q $(LHA_REPO) $(BUILD)/tools/lha-src
	cd $(BUILD)/tools/lha-src && \
		git -c advice.detachedHead=false checkout -q $(LHA_COMMIT) && \
		autoreconf -fi >/dev/null 2>&1 && ./configure >/dev/null && \
		$(MAKE) >/dev/null
	cp $(BUILD)/tools/lha-src/src/lha $(BUILD)/tools/lha
	rm -rf $(BUILD)/tools/lha-src

# --- dist: assemble the release archive (AmiInspect + AmiPilotServer + ----
#           guide + license)
# Builds the m68k binaries themselves (build-test.yml's dist job runs
# `make dist` standalone, with no prior `build` job's artifacts to reuse -
# see the verb-contract comment above); the lha archiver is built
# automatically (above). Produces build/dist/amipilot.lha (drawer with
# AmiInspect+AmiPilotServer+guide+license+readme) and
# build/dist/amipilot.readme alongside it.
#
# Only AmiPilotServer from server/ ships, not AmiClickTest/AmiSetMouse
# (server's other two build products) -- those are dev-only scoping
# tools, not deliverables, same reasoning as fixtures/ being excluded
# entirely (they're conformance test apps for this project's own
# development -- see fixtures/README.md).
#
# The $VER grep below confirms the binaries just built actually embed the
# CURRENT version.mk VERSION.REVISION -- matching sibling projects
# amiauth/sana2loop's own dist targets' self-check, catching a stale
# build/ (built before a version bump) before it ships.
dist: amiga $(AMIPILOTD_BIN) guide $(LHA)
	rm -rf $(BUILD)/dist
	mkdir -p $(BUILD)/dist/amipilot
	cp $(INSPECT_BIN) $(AMIPILOTD_BIN) $(BUILD)/amipilot.guide LICENSE amipilot.readme \
		$(BUILD)/dist/amipilot/
	cp amipilot.readme $(BUILD)/dist/
	@v="$(VERSION).$(REVISION)"; \
	for b in AmiInspect AmiPilotServer; do \
		grep -aqF "\$$VER: $$b $$v (" $(BUILD)/dist/amipilot/$$b || \
			{ echo "dist: $(BUILD)/dist/amipilot/$$b lacks \"\$$VER: $$b $$v (...)\" - stale build/?"; exit 1; }; \
	done
	cd $(BUILD)/dist && $(abspath $(LHA)) aq amipilot.lha amipilot
	@ls -l $(BUILD)/dist/amipilot.lha $(BUILD)/dist/amipilot.readme

clean:
	rm -rf $(BUILD)
