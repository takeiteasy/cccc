# Makefile -- bare-minimum bootstrap (#842 Step 4).
#
# This builds just enough of a `cccc` to run `./cccc --build build.c`, which
# is the real build system from here on:
#
#   make                       # produce ./cccc
#   ./cccc --build build.c     # everything else: test, clean, sanitizers,
#                               # afl, bench, profile-*, the Linux Colima
#                               # targets, ...
#   ./cccc --build build.c --build-list-targets
#
# No libbacktrace (nicer crash traces) and no readline (REPL line editing)
# here -- both are optional features, gated behind their own #ifdefs, not
# needed to run build.c. build.c's own targets link both properly; this file
# only has to satisfy the stage0 invariant: a fresh clone with nothing but a
# system `cc` (and libffi) can always produce a working compiler.
#
# `make -f tools/Makefile.backup <target>` is the pre-cut, full-featured
# Makefile, kept as an escape hatch for when there is no working cccc yet
# (e.g. bisecting a bootstrap regression) -- see that file's header.

CC ?= cc
PKG_CONFIG ?= pkg-config

EXE :=
ifeq ($(OS),Windows_NT)
	EXE := .exe
endif
UNAME_S := $(shell uname -s)

# std.c is the generated embedded-stdlib table (see build.c's default
# two-pass build). A fresh clone has no src/std.c until a build has run
# once; src/std_stub.c is a small committed stand-in whose accessors return
# nothing, used only to bootstrap the comptime machinery
# tools/generate_stdlib.c needs to produce the real src/std.c (the private
# headers it needs are found on disk via -I./include instead). Once std.c
# exists on disk it always wins (self-correcting: no explicit step needed to
# switch back). See man/BUILD.md.
ifeq ($(wildcard src/std.c),)
STDLIB_SRC := src/std_stub.c
else
STDLIB_SRC := src/std.c
endif
SRCS := $(filter-out src/ops.c src/std.c src/std_stub.c, $(wildcard src/*.c src/stdlib/*.c)) $(STDLIB_SRC)

# MODE=release optimizes the stage0 binary itself (mirrors build.c's release
# target, #883); does not affect the optimization level of any binary stage0
# goes on to produce via `./cccc --build build.c`, which is compiled by the
# host cc directly through build.c's own target flags.
MODE ?= debug
ifeq ($(MODE),release)
CFLAGS := -Wall -O2 -g -DNDEBUG -std=c23 -Wno-deprecated-declarations -Wno-switch -pthread
else
CFLAGS := -Wall -O0 -g -std=c23 -Wno-deprecated-declarations -Wno-switch -pthread
endif
LDFLAGS := -pthread

ifeq ($(UNAME_S),Darwin)
	# iconv() is declared in libSystem's <iconv.h> but the symbols only
	# resolve at link time via libiconv (verified: link fails without it,
	# succeeds with it). glibc ships iconv in libc itself, so no extra flag
	# is needed on Linux.
	LDFLAGS += -liconv
else
	# Clang -O1/-O2/-O3 miscompiles glibc's extern-inline pthread.h/wchar.h
	# functions (pthread_equal, btowc, wctob, mbrlen) as strong symbols
	# instead of discardable ones, causing "multiple definition" link errors
	# -- see build.c's add_cccc_flags_opt() for the full writeup (#883).
	# Only matters for MODE=release here (stage0 debug is always -O0), but
	# harmless to always pass.
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

.PHONY: default
default: cccc$(EXE)

cccc$(EXE): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# bootstrap (#857): the full stage0 dance in one target -- link against
# src/std_stub.c, regenerate the real src/std.c, then relink against it.
# Needs a *recursive* $(MAKE), not three lines run back to back: STDLIB_SRC
# above is chosen by $(wildcard src/std.c) at make-parse time, so switching
# from the stub to the real stdlib only takes effect in a fresh make
# invocation. The `rm -f` between the two links is required, not cosmetic:
# make's prerequisite check treats an *equal* mtime as up to date (not
# stale), and regen_stdlib.sh's mv() can land src/std.c's mtime in the same
# clock tick as the just-linked cccc binary on a fast filesystem/CI runner --
# observed intermittently as "make: `cccc' is up to date." even though
# src/std.c had just switched from the stub to the real stdlib, leaving the
# stage0 binary permanently linked against std_stub.c. Removing the binary
# first forces an unconditional relink regardless of mtime comparison.
.PHONY: bootstrap
bootstrap:
	$(MAKE) cccc$(EXE)
	sh tools/regen_stdlib.sh ./cccc$(EXE)
	rm -f cccc$(EXE)
	$(MAKE) cccc$(EXE)
