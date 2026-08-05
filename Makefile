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

# --- Docker (shared toolchain image, see sidick/amiga-dev) ---
IMAGE      ?= ghcr.io/sidick/amiga-dev:1
# --user matches the container process's UID/GID to the host caller's, so
# every file `make amiga` creates under build/ (bind-mounted, not a
# container-private volume) is host-owned, not root-owned.
DOCKER_RUN := docker run --rm --user "$$(id -u):$$(id -g)" -v "$(CURDIR)":/work -w /work $(IMAGE)

.PHONY: all amiga docker clean version build test-host test-target lint dist

all: amiga

amiga: $(INSPECT_BIN)

$(MODEL_LIB): $(MODEL_SRC) $(MODEL_INCDIR)/intuition_model.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(MODEL_INCDIR) -c $(MODEL_SRC) -o $(BUILD)/walk.o
	$(AR) rcs $@ $(BUILD)/walk.o

$(INSPECT_BIN): $(INSPECT_SRCDIR)/main.c $(MODEL_LIB)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(MODEL_INCDIR) -o $@ $(INSPECT_SRCDIR)/main.c $(MODEL_LIB)

docker:
	$(DOCKER_RUN) make amiga

# --- Verb contract (sidick/amiga-workflows' build-test.yml) ---------------
# Each build-test.yml job is independent (no artifact-passing between
# them), so test-target/dist below pull in `build` themselves rather than
# assuming a prior job already ran it.
build: amiga

# No host-side Python package exists yet (phase 0.3) -- a real no-op per
# the verb contract's own allowance, not a disabled/skipped job.
test-host:
	@echo "test-host: no host-side tests yet (lands in phase 0.3, see docs/implementation-plan.md)"

# No on-target test harness exists yet -- AmiInspect has no emulator smoke
# test until the conformance fixtures (fixtures/) land alongside it.
test-target:
	@echo "test-target: no on-target test harness yet (lands with phase 0.1's fixtures)"

lint:
	pip install --quiet semgrep
	semgrep --config auto --error \
	  --include='*.c' --include='*.h' \
	  intuition-model/ amiinspect/ server/

version:
	@echo "$(VERSION).$(REVISION)"

clean:
	rm -rf $(BUILD)
