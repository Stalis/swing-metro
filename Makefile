SHELL := /bin/sh

.DEFAULT_GOAL := help

PIO ?= pio
PYTHON ?= $(if $(wildcard .venv/bin/python),.venv/bin/python,python3)
PIO_ENV ?= rpipico2
STAGE5_OFF_ENV ?= rpipico2-stage5-instrumentation-off
STAGE5_FAULT_ENV ?= rpipico2-stage5-fault-scenarios
STAGE5_OUTPUT_ROOT ?= data/stage5-4-runs
STAGE5_INTERNAL_OUTPUT_ROOT ?= data/stage5-5-internal-runs
STAGE5_OUTPUT_DIR ?=
STAGE5_DURATION_SECONDS ?= 244
STAGE5_SERIAL_PORT ?=
STAGE5_MIDI_PORT ?=
STAGE5_USB_TOPOLOGY ?= direct USB connection; not independently verified
STAGE5_PATTERN_CONFIRMED ?=
STAGE5_RESUME ?=
NATIVE_ENV ?= native
CLANG_FORMAT ?= clang-format
CLANG_TIDY ?= clang-tidy

CODE_DIRS := src include lib test
FORMAT_FILES := $(shell find $(CODE_DIRS) -type f \( \
	-name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' -o \
	-name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' -o \
	-name '*.ipp' -o -name '*.ino' \) 2>/dev/null | sort)
TIDY_FILES := $(shell find src lib -type f \( \
	-name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \
	\) 2>/dev/null | sort)

# clang-tidy does not automatically discover the C++ standard library shipped
# with PlatformIO's ARM cross-compiler. Resolve it from the generated database.
COMPILE_CXX := $(shell sed -n 's/^[[:space:]]*"command": "\([^ ]*\).*/\1/p' \
	compile_commands.json 2>/dev/null | head -n 1)
TOOLCHAIN_ROOT := $(abspath $(dir $(COMPILE_CXX))/..)
ARM_CXX_INCLUDE := $(firstword $(wildcard \
	$(TOOLCHAIN_ROOT)/arm-none-eabi/include/c++/*))
ARM_GCC_INCLUDE := $(firstword $(wildcard \
	$(TOOLCHAIN_ROOT)/lib/gcc/arm-none-eabi/*/include))
TIDY_TOOLCHAIN_ARGS := \
	--extra-arg=--target=arm-none-eabi \
	--extra-arg=-isystem --extra-arg=$(ARM_CXX_INCLUDE) \
	--extra-arg=-isystem --extra-arg=$(ARM_CXX_INCLUDE)/arm-none-eabi/thumb \
	--extra-arg=-isystem --extra-arg=$(ARM_CXX_INCLUDE)/backward \
	--extra-arg=-isystem --extra-arg=$(ARM_GCC_INCLUDE) \
	--extra-arg=-isystem --extra-arg=$(ARM_GCC_INCLUDE)-fixed \
	--extra-arg=-isystem --extra-arg=$(TOOLCHAIN_ROOT)/arm-none-eabi/include

.PHONY: help format format-check compiledb tidy tidy-run test test-scripts build build-stage5-off build-stage5-fault stage5-load-matrix stage5-internal-gate100 stage5-internal-mixed-gate verify \
	check-format-tool check-tidy-tool check-pio-tool check-tidy-toolchain

help:
	@printf '%s\n' \
		'make format        Format project C/C++ sources in place' \
		'make format-check  Check formatting without changing files' \
		'make tidy          Regenerate compile_commands.json and run clang-tidy' \
		'make test          Run native Unity tests' \
		'make test-scripts  Run host-side Python tests' \
		'make build         Build firmware for the configured board' \
		'make build-stage5-off Build firmware with Stage 5 instrumentation disabled' \
		'make build-stage5-fault Build stage-only deterministic fault-scenario firmware' \
		'make stage5-load-matrix Run the complete Stage 5.4 hardware matrix and restore production' \
		'make stage5-internal-gate100 Run Stage 5.5.1 production Gate 100 capture' \
		'make stage5-internal-mixed-gate Run Stage 5.5.1 production mixed-Gate matrix' \
		'make verify        Run format-check, tidy, tests, and firmware build'

check-format-tool:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { \
		printf 'Missing tool: %s\n' '$(CLANG_FORMAT)' >&2; \
		exit 127; \
	}

check-tidy-tool:
	@command -v $(CLANG_TIDY) >/dev/null 2>&1 || { \
		printf 'Missing tool: %s\n' '$(CLANG_TIDY)' >&2; \
		exit 127; \
	}

check-tidy-toolchain:
	@test -x '$(COMPILE_CXX)' && test -d '$(ARM_CXX_INCLUDE)' && \
		test -d '$(ARM_GCC_INCLUDE)' || { \
		printf '%s\n' 'Unable to resolve the PlatformIO ARM C++ toolchain.' >&2; \
		printf '%s\n' 'Run make compiledb, then retry make tidy.' >&2; \
		exit 1; \
	}

check-pio-tool:
	@command -v $(PIO) >/dev/null 2>&1 || { \
		printf 'Missing tool: %s\n' '$(PIO)' >&2; \
		exit 127; \
	}

format: check-format-tool
	$(CLANG_FORMAT) -i $(FORMAT_FILES)

format-check: check-format-tool
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES)

compiledb: check-pio-tool
	$(PIO) run -e $(PIO_ENV) -t compiledb

tidy: check-tidy-tool compiledb
	@$(MAKE) --no-print-directory tidy-run

tidy-run: check-tidy-tool check-tidy-toolchain
	$(CLANG_TIDY) -p . $(TIDY_TOOLCHAIN_ARGS) $(TIDY_FILES)

test: check-pio-tool
	$(PIO) test -e $(NATIVE_ENV) -f test_native

test-scripts:
	$(PYTHON) -m unittest discover -s scripts/tests

build: check-pio-tool
	$(PIO) run -e $(PIO_ENV)

build-stage5-off: check-pio-tool
	$(PIO) run -e $(STAGE5_OFF_ENV)

build-stage5-fault: check-pio-tool
	$(PIO) run -e $(STAGE5_FAULT_ENV)

stage5-load-matrix: check-pio-tool
	$(PYTHON) scripts/stage5_load_matrix_run.py \
		--pio "$(PIO)" \
		--output-root "$(STAGE5_OUTPUT_ROOT)" \
		--duration-seconds "$(STAGE5_DURATION_SECONDS)" \
		--usb-topology "$(STAGE5_USB_TOPOLOGY)" \
		$(if $(STAGE5_OUTPUT_DIR),--output-dir "$(STAGE5_OUTPUT_DIR)") \
		$(if $(STAGE5_SERIAL_PORT),--port "$(STAGE5_SERIAL_PORT)") \
		$(if $(STAGE5_MIDI_PORT),--midi-port "$(STAGE5_MIDI_PORT)") \
		$(if $(STAGE5_RESUME),--resume)

stage5-internal-gate100: check-pio-tool
	$(PYTHON) scripts/stage5_production_internal_matrix.py \
		--scenario gate100 \
		--pio "$(PIO)" \
		--output-root "$(STAGE5_INTERNAL_OUTPUT_ROOT)" \
		--duration-seconds "$(STAGE5_DURATION_SECONDS)" \
		--usb-topology "$(STAGE5_USB_TOPOLOGY)" \
		$(if $(STAGE5_OUTPUT_DIR),--output-dir "$(STAGE5_OUTPUT_DIR)") \
		$(if $(STAGE5_SERIAL_PORT),--port "$(STAGE5_SERIAL_PORT)") \
		$(if $(STAGE5_MIDI_PORT),--midi-port "$(STAGE5_MIDI_PORT)") \
		$(if $(STAGE5_PATTERN_CONFIRMED),--pattern-confirmed) \
		$(if $(STAGE5_RESUME),--resume)

stage5-internal-mixed-gate: check-pio-tool
	$(PYTHON) scripts/stage5_production_internal_matrix.py \
		--scenario mixed-gate \
		--pio "$(PIO)" \
		--output-root "$(STAGE5_INTERNAL_OUTPUT_ROOT)" \
		--duration-seconds "$(STAGE5_DURATION_SECONDS)" \
		--usb-topology "$(STAGE5_USB_TOPOLOGY)" \
		$(if $(STAGE5_OUTPUT_DIR),--output-dir "$(STAGE5_OUTPUT_DIR)") \
		$(if $(STAGE5_SERIAL_PORT),--port "$(STAGE5_SERIAL_PORT)") \
		$(if $(STAGE5_MIDI_PORT),--midi-port "$(STAGE5_MIDI_PORT)") \
		$(if $(STAGE5_PATTERN_CONFIRMED),--pattern-confirmed) \
		$(if $(STAGE5_RESUME),--resume)

verify:
	@$(MAKE) --no-print-directory format-check
	@$(MAKE) --no-print-directory tidy
	@$(MAKE) --no-print-directory test
	@$(MAKE) --no-print-directory test-scripts
	@$(MAKE) --no-print-directory build
	@$(MAKE) --no-print-directory build-stage5-off
	@$(MAKE) --no-print-directory build-stage5-fault
