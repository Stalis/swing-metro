SHELL := /bin/sh

.DEFAULT_GOAL := help

PIO ?= pio
PIO_ENV ?= rpipico2
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

.PHONY: help format format-check compiledb tidy tidy-run test build verify \
	check-format-tool check-tidy-tool check-pio-tool check-tidy-toolchain

help:
	@printf '%s\n' \
		'make format        Format project C/C++ sources in place' \
		'make format-check  Check formatting without changing files' \
		'make tidy          Regenerate compile_commands.json and run clang-tidy' \
		'make test          Run native Unity tests' \
		'make build         Build firmware for the configured board' \
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

build: check-pio-tool
	$(PIO) run -e $(PIO_ENV)

verify:
	@$(MAKE) --no-print-directory format-check
	@$(MAKE) --no-print-directory tidy
	@$(MAKE) --no-print-directory test
	@$(MAKE) --no-print-directory build
