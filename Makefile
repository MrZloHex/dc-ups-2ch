# DC-UPS-2CH — top-level build orchestrator.
#
# Everything is stamped with the same identity: a `VERSION` string, the short
# git commit hash (with "-dirty" if the working tree is not clean), and the
# UTC build date. Those three values are:
#
#   - injected into the firmware via scripts/inject_version.py (as -D macros
#     FW_VERSION / FW_GIT_HASH / FW_BUILD_DATE, exposed to code through
#     include/version.h),
#   - baked into the LaTeX PDFs via docs/build_info.tex (regenerated on every
#     `make docs`).
#
# Both children (pio and docs/Makefile) can also run standalone and derive the
# same values on their own. This Makefile just exports env vars so that a
# single `make` at the repo root produces byte-identical stamps everywhere.
#
# Common targets:
#   make              # firmware + docs
#   make firmware     # just pio run
#   make docs         # just PDFs
#   make upload       # firmware, then flash to the ESP32
#   make monitor      # pio serial monitor at 115200
#   make flash        # upload + monitor
#   make version      # print the identity that will be stamped
#   make clean        # remove build artefacts (keeps PDFs)
#   make distclean    # remove build artefacts AND generated PDFs
#   make help         # this list

# ------------------------- Build identity -------------------------
# Each identity variable can be overridden from the environment or the
# command line (`make VERSION=... firmware`). Otherwise we derive it once.

ifndef VERSION
VERSION := $(shell test -f VERSION && head -n1 VERSION | tr -d '[:space:]' || echo dev)
endif

ifndef GIT_HASH
GIT_HASH_RAW := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)
GIT_DIRTY    := $(shell test -n "$$(git status --porcelain 2>/dev/null)" && echo -dirty)
GIT_HASH     := $(GIT_HASH_RAW)$(GIT_DIRTY)
endif

ifndef BUILD_DATE
BUILD_DATE := $(shell date -u +%Y-%m-%d)
endif

HW_VERSION ?= v2.1
DOCREV     ?= $(GIT_HASH)

# Export everything so children (scripts/inject_version.py, docs/Makefile)
# use the same values as this top-level Makefile.
export VERSION GIT_HASH BUILD_DATE HW_VERSION DOCREV

# ------------------------- Tools -------------------------

PIO ?= pio

.PHONY: all firmware docs upload monitor flash clean distclean version help

.DEFAULT_GOAL := all

# ------------------------- Targets -------------------------

all: firmware docs

firmware:
	@echo "==> Building firmware $(VERSION) ($(GIT_HASH), $(BUILD_DATE))"
	$(PIO) run

upload: firmware
	@echo "==> Flashing firmware to ESP32"
	$(PIO) run --target upload

monitor:
	@echo "==> Opening serial monitor (Ctrl-] to exit)"
	$(PIO) device monitor

flash: upload monitor

docs:
	@echo "==> Building documentation $(VERSION) ($(GIT_HASH), $(BUILD_DATE))"
	$(MAKE) -C docs all

clean:
	@echo "==> Cleaning firmware"
	-$(PIO) run --target clean
	@echo "==> Cleaning docs"
	$(MAKE) -C docs clean

distclean: clean
	@echo "==> Wiping generated PDFs"
	$(MAKE) -C docs distclean

version:
	@echo "VERSION    = $(VERSION)"
	@echo "GIT_HASH   = $(GIT_HASH)"
	@echo "BUILD_DATE = $(BUILD_DATE)"
	@echo "HW_VERSION = $(HW_VERSION)"
	@echo "DOCREV     = $(DOCREV)"

help:
	@echo "DC-UPS-2CH build orchestrator"
	@echo ""
	@echo "Targets:"
	@echo "  all         firmware + docs (default)"
	@echo "  firmware    build the ESP32 firmware only"
	@echo "  docs        build the LaTeX PDFs only"
	@echo "  upload      firmware + flash to the ESP32"
	@echo "  monitor     open pio device monitor (115200)"
	@echo "  flash       upload + monitor"
	@echo "  clean       remove firmware build tree + docs intermediates"
	@echo "  distclean   clean + remove generated PDFs"
	@echo "  version     print the identity that will be stamped"
	@echo "  help        this list"
	@echo ""
	@$(MAKE) --no-print-directory version
