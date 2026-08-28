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

EXAMPLES := append lambda_head mutual reverse fib adder letsum

COMPTIME_SRC  := src/ccccl_comptime.c
COMPTIME_HDRS := include/ccccl/ccccl_ir.h include/ccccl/ccccl_form.h \
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
build/%.gen.c: examples/%.lisp $(COMPTIME_SRC) $(COMPTIME_HDRS) $(RT_HDRS) | build
	$(CCCC) -c=generated $(COMPTIME_SRC) -Iinclude/ccccl -Iruntime \
	    -DCCCCL_LISP_PATH='"examples/$*.lisp"' -o $@

# examples/%_main.c is never passed to cccc at all -- plain C, compiled and
# linked by the system cc alongside the generated output and the runtime.
build/%: build/%.gen.c examples/%_main.c $(RT_SRC) $(RT_HDRS)
	$(CC) $(CFLAGS) -Iruntime -o $@ examples/$*_main.c build/$*.gen.c $(RT_SRC)

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
	    $(CCCC) -c=native $(COMPTIME_SRC) examples/$${ex}_main.c $(RT_SRC) \
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
