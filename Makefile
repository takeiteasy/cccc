# Makefile -- builds ./cccc, the one compiler binary.
#
#   make                       # produce ./cccc (one pass, no generators)
#   ./cccc --build build.c     # everything else: test, clean, sanitizers,
#                              # afl, bench, profile-*, the Linux Colima
#                              # targets, ...
#   ./cccc --build build.c --build-list-targets
#
# Optional features are detected here:
#   libbacktrace   vendor/libbacktrace submodule (CCCC_HAS_BACKTRACE=0 opts out)
#   readline       REPL line editing (pkg-config, then Homebrew keg paths)
#   curl, decimal, ndbm   opt-in: CCCC_HAS_CURL=1, CCCC_HAS_DECIMAL=1,
#                         CCCC_HAS_NDBM=1 (same variables as build.c)
# A fresh clone with a system `cc` and libffi always produces a working
# compiler.

CC ?= cc
PKG_CONFIG ?= pkg-config

EXE :=
ifeq ($(OS),Windows_NT)
	EXE := .exe
endif
UNAME_S := $(shell uname -s)

# Every bundled header and -c=native shim chunk is embedded into the binary
# (src/std_table.c, src/shim_table.c), so an edit to any of them relinks.
HEADERS := $(wildcard include/*.h include/*/*.h)
SHIMS := $(wildcard src/shims/*/*.c src/stdlib/wide_bitint/*.c src/stdlib/c23_math/*.c src/stdlib/c23_mem/*.c src/stdlib/c23_strtol/*.c)
SRCS := $(filter-out src/ops.c, $(wildcard src/*.c src/stdlib/*.c))

# MODE=release optimizes the binary itself (mirrors build.c's release target).
MODE ?= debug
ifeq ($(MODE),release)
CFLAGS := -Wall -O2 -g -DNDEBUG -std=c23 -Wno-deprecated-declarations -Wno-switch -pthread
else
CFLAGS := -Wall -O0 -g -std=c23 -Wno-deprecated-declarations -Wno-switch -pthread
endif
LDFLAGS := -pthread

ifeq ($(UNAME_S),Darwin)
	# iconv() symbols resolve at link time via libiconv on macOS; glibc ships
	# iconv in libc itself.
	LDFLAGS += -liconv -framework CoreServices
else
	CFLAGS += -fgnu89-inline -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
	LDFLAGS += -lm
endif

# libffi is required for native FFI calls.
LIBFFI_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags libffi 2>/dev/null)
LIBFFI_LDFLAGS ?= $(shell $(PKG_CONFIG) --libs libffi 2>/dev/null)
ifeq ($(LIBFFI_LDFLAGS),)
	ifeq ($(UNAME_S),Darwin)
		LIBFFI_CFLAGS := -I/opt/homebrew/opt/libffi/include -I/usr/local/opt/libffi/include
		LIBFFI_LDFLAGS := -L/opt/homebrew/opt/libffi/lib -L/usr/local/opt/libffi/lib -lffi
	else
		LIBFFI_CFLAGS := -I/usr/include -I/usr/local/include
		LIBFFI_LDFLAGS := -L/usr/lib -L/usr/local/lib -lffi
	endif
endif
CFLAGS += $(LIBFFI_CFLAGS)
LDFLAGS += $(LIBFFI_LDFLAGS)

# Optional readline for the REPL (-r); falls back to plain fgets when absent.
# readline is keg-only on Homebrew (macOS ships libedit under the same name).
READLINE_CFLAGS := $(shell $(PKG_CONFIG) --cflags readline 2>/dev/null)
READLINE_LDFLAGS := $(shell $(PKG_CONFIG) --libs readline 2>/dev/null)
ifeq ($(READLINE_LDFLAGS),)
	ifeq ($(UNAME_S),Darwin)
		READLINE_PROBE_CFLAGS := -I/opt/homebrew/opt/readline/include -I/usr/local/opt/readline/include
		READLINE_PROBE_LDFLAGS := -L/opt/homebrew/opt/readline/lib -L/usr/local/opt/readline/lib -lreadline
	else
		READLINE_PROBE_CFLAGS := -I/usr/include -I/usr/local/include
		READLINE_PROBE_LDFLAGS := -L/usr/lib -L/usr/local/lib -lreadline
	endif
	ifneq ($(shell printf '\#include <stdio.h>\n\#include <readline/readline.h>\nint main(void){return 0;}\n' | $(CC) -x c - $(READLINE_PROBE_CFLAGS) $(READLINE_PROBE_LDFLAGS) -o /dev/null 2>/dev/null && echo ok),)
		READLINE_CFLAGS := $(READLINE_PROBE_CFLAGS)
		READLINE_LDFLAGS := $(READLINE_PROBE_LDFLAGS)
	endif
endif
ifneq ($(READLINE_LDFLAGS),)
	CFLAGS += -DHAVE_READLINE $(READLINE_CFLAGS)
	LDFLAGS += $(READLINE_LDFLAGS)
endif

# Opt-in libcurl (URL includes).
ifneq ($(filter-out 0,$(CCCC_HAS_CURL)),)
	CFLAGS += -DCCCC_HAS_CURL=1 $(shell $(PKG_CONFIG) --cflags libcurl 2>/dev/null)
	LDFLAGS += $(or $(shell $(PKG_CONFIG) --libs libcurl 2>/dev/null),-lcurl)
endif

# Opt-in Intel BID decimal FP (tools/fetch_intel_bid.sh builds it).
ifneq ($(filter-out 0,$(CCCC_HAS_DECIMAL)),)
	CCCC_BID_PREFIX ?= build/intel-bid
	ifneq ($(wildcard $(CCCC_BID_PREFIX)/lib/libbid.a),)
		CFLAGS += -DCCCC_HAS_DECIMAL=1 -I$(CCCC_BID_PREFIX)/src
		LDFLAGS += $(CCCC_BID_PREFIX)/lib/libbid.a
	else
		$(warning CCCC_HAS_DECIMAL=1 but $(CCCC_BID_PREFIX)/lib/libbid.a is missing; run tools/fetch_intel_bid.sh. Building without decimal FP.)
	endif
endif

# Opt-in ndbm on Linux (libgdbm-compat); macOS ships ndbm in libc.
ifneq ($(filter-out 0,$(CCCC_HAS_NDBM)),)
	CFLAGS += -DCCCC_HAS_NDBM=1
	ifneq ($(UNAME_S),Darwin)
		LDFLAGS += -lgdbm_compat
	endif
endif

# --version stamp; absent in release tarballs (no .git).
GIT_DESC := $(shell git describe --tags --always --dirty 2>/dev/null)
ifneq ($(GIT_DESC),)
	CFLAGS += -DCCCC_GIT_DESC='"$(GIT_DESC)"'
endif

# libbacktrace (nicer crash traces): vendor/libbacktrace plus the hand-written
# config in src/backtrace/. Built into build/libbacktrace.a.
BT_DIR := vendor/libbacktrace
BT_OUT := build/libbacktrace
ifneq ($(CCCC_HAS_BACKTRACE),0)
ifneq ($(wildcard $(BT_DIR)/backtrace.c),)
BT_FMT := $(if $(filter Darwin,$(UNAME_S)),macho,elf)
BT_SRCS := $(addsuffix .c,$(addprefix $(BT_DIR)/,backtrace atomic dwarf fileline mmap mmapio posix print simple sort state $(BT_FMT)))
BT_OBJS := $(patsubst $(BT_DIR)/%.c,$(BT_OUT)/%.o,$(BT_SRCS))
BT_CFLAGS := -O2 -g -std=c11 -Wno-unused-parameter -Wno-unused-variable \
	-Wno-missing-field-initializers -Wno-shift-count-overflow \
	-Wno-implicit-function-declaration -Wno-deprecated-declarations \
	-Isrc/backtrace -I$(BT_DIR)
ifneq ($(UNAME_S),Darwin)
	BT_CFLAGS += -D_GNU_SOURCE
endif
CFLAGS += -DCCCC_HAS_BACKTRACE=1 -Isrc/backtrace -I$(BT_DIR)
BT_LIB := $(BT_OUT)/libbacktrace.a
endif
endif

.PHONY: default
default: cccc$(EXE)

cccc$(EXE): $(SRCS) src/ops.c $(wildcard src/*.h src/stdlib/*.h) $(HEADERS) $(SHIMS) $(BT_LIB)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(BT_LIB) $(LDFLAGS)

$(BT_OUT)/%.o: $(BT_DIR)/%.c
	@mkdir -p $(BT_OUT)
	$(CC) $(BT_CFLAGS) -c -o $@ $<

$(BT_OUT)/libbacktrace.a: $(BT_OBJS)
	$(AR) rcs $@ $^
