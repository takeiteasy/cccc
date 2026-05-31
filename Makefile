SRCS := $(wildcard src/*.c src/stdlib/*.c)
CFLAGS := -Wall -O0 -g -std=c99 -Wno-deprecated-declarations -Wno-switch -Wno-inline-asm
LDFLAGS :=

ifeq ($(OS),Windows_NT)
	EXE := .EXE
	DYLIB := .dll
else
	EXE :=
	UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Darwin)
		DYLIB := .dylib
	else
		DYLIB := .so
	endif
endif
EXE_OUT := jcc$(EXE)
LIB_OUT := libjcc$(DYLIB)

default: $(EXE_OUT)

INCLUDE_HEADERS := $(wildcard include/*.h include/**/*.h)

src/std.c: $(INCLUDE_HEADERS) std.py
	python3 std.py

$(EXE_OUT): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(LIB_OUT): $(SRCS)
	$(CC) -fpic -shared $(CFLAGS) -o $@ $(filter-out src/main.c, $^) $(LDFLAGS)

test: clean $(EXE_OUT)
	@python3 tests.py

all: clean $(EXE_OUT) $(LIB_OUT) test docs

docs:
	@headerdoc2html src/jcc.h include/reflection_api.h -o docs/; \
	gatherheaderdoc docs/; \
	mv docs/masterTOC.html docs/index.html

clean:
	@$(RM) -f $(EXE_OUT) $(LIB_OUT)

.PHONY: default test clean docs all
