# Makefile for the ccccl example -- see README.md.
#
# CCCC can't build itself yet (cccc as a build.c target isn't implemented),
# so this is plain make rather than `cccc --build build.c`.
#
#   make            build every example's binary under build/
#   make check      build + run every example, diff against its .expected
#   make native     one-shot `cccc -c=native` per example (no intermediate
#                   .gen.c, no separate cc invocation) -- the headline path
#   make show-NAME  print the generated C for one example (e.g. show-append)
#   make clean

CCCC ?= ../../cccc
CC   ?= cc
CFLAGS ?= -O2 -Wall

# "library" examples: .lisp defines functions only, a hand-written
# examples/NAME_main.c drives them.  "program" examples: .lisp defines *and*
# runs toplevel forms, cccc synthesizes main(), no host TU exists.
LIB_EXAMPLES  := append lambda_head mutual reverse fib adder letsum
PROG_EXAMPLES := hello
EXAMPLES      := $(LIB_EXAMPLES) $(PROG_EXAMPLES)

# src/ccccl_comptime.c is the file that drives the pass; the reader / IR /
# lowering modules are ordinary .h/.c pairs it pulls in with
# `#include @comptime` and cccc forwards on demand (the cross-file
# comptime-call support). They are compile-time only -- never linked into an
# example binary -- so they appear on the cccc command line but not the `cc`
# link line.
COMPTIME_SRC     := src/ccccl_comptime.c
COMPTIME_MODULES := src/ccccl_ir.c src/ccccl_form.c src/ccccl_lower.c
COMPTIME_HDRS    := include/ccccl/ccccl_ir.h include/ccccl/ccccl_form.h \
                    include/ccccl/ccccl_lower.h
RT_SRC        := runtime/ccccl_rt.c
RT_HDRS       := runtime/ccccl_rt.h runtime/ccccl_rt_internal.h

.PHONY: all check native clean
.PRECIOUS: build/%.gen.c

all: $(EXAMPLES:%=build/%)

build:
	mkdir -p build

# The comptime pass: src/ccccl_comptime.c is the only file ever passed to
# cccc (see its own file comment for why). -D rather than a source #define
# for the input path -- comptime bodies don't see ordinary source #define's.
build/%.gen.c: examples/%.lisp $(COMPTIME_SRC) $(COMPTIME_MODULES) $(COMPTIME_HDRS) $(RT_HDRS) | build
	$(CCCC) -c=generated $(COMPTIME_SRC) $(COMPTIME_MODULES) \
	    -Iinclude/ccccl -Iruntime \
	    -DCCCCL_LISP_PATH='"examples/$*.lisp"' -o $@

# Library examples: examples/%_main.c is never passed to cccc at all -- plain
# C, compiled and linked by the system cc alongside the generated output and
# the runtime.  Program examples link only the generated output (which
# carries its own main()) and the runtime.
#
# -Iinclude/ccccl on the cc line: -c=generated replays the comptime modules'
# own `#include "ccccl_*.h"` lines into the generated C (declaration-only
# headers -- the definitions ran at compile time and are not linked here), so
# the header directory has to be reachable when cc reads the .gen.c.
$(LIB_EXAMPLES:%=build/%): build/%: build/%.gen.c examples/%_main.c $(RT_SRC) $(RT_HDRS)
	$(CC) $(CFLAGS) -Iinclude/ccccl -Iruntime -o $@ examples/$*_main.c build/$*.gen.c $(RT_SRC)

$(PROG_EXAMPLES:%=build/%): build/%: build/%.gen.c $(RT_SRC) $(RT_HDRS)
	$(CC) $(CFLAGS) -Iinclude/ccccl -Iruntime -o $@ build/$*.gen.c $(RT_SRC)

check: all
	@status=0; \
	for ex in $(EXAMPLES); do \
	    got=$$(build/$$ex) || status=1; \
	    want=$$(cat examples/$$ex.expected); \
	    if [ "$$got" = "$$want" ]; then \
	        echo "ok   $$ex -> $$got"; \
	    else \
	        echo "FAIL $$ex: got '$$got', want '$$want'"; \
	        status=1; \
	    fi; \
	done; \
	exit $$status

# One-shot cccc -c=native over the comptime pass + the hand-written main +
# the runtime, in a single invocation -- no intermediate .gen.c, no
# separate cc step. The proof that cccc's own native backend, not just a
# hand-driven cc, can build this program end to end.
native: | build
	@status=0; \
	for ex in $(EXAMPLES); do \
	    main=examples/$${ex}_main.c; \
	    [ -f "$$main" ] || main=; \
	    $(CCCC) -c=native $(COMPTIME_SRC) $(COMPTIME_MODULES) $$main $(RT_SRC) \
	        -Iinclude/ccccl -Iruntime \
	        -DCCCCL_LISP_PATH="\"examples/$$ex.lisp\"" \
	        -o build/$${ex}_native || { status=1; continue; }; \
	    got=$$(build/$${ex}_native) || status=1; \
	    want=$$(cat examples/$$ex.expected); \
	    if [ "$$got" = "$$want" ]; then \
	        echo "ok   $$ex (native) -> $$got"; \
	    else \
	        echo "FAIL $$ex (native): got '$$got', want '$$want'"; \
	        status=1; \
	    fi; \
	done; \
	exit $$status

show-%: build/%.gen.c
	@cat $<

clean:
	rm -rf build
