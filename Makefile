SRCS := $(wildcard src/*.c src/stdlib/*.c)
CFLAGS := -Wall -O0 -g -std=c99 -Wno-deprecated-declarations -Wno-switch -Wno-inline-asm
LDFLAGS :=

# Optional libffi support for variadic foreign functions
# Enable with: make JCC_HAS_FFI=1 or export JCC_HAS_FFI=1
# Disable with: make JCC_HAS_FFI=0
ifdef JCC_HAS_FFI
  ifneq ($(JCC_HAS_FFI),0)
    CFLAGS += -DJCC_HAS_FFI=1
    # Try pkg-config first, fallback to platform-specific paths
    LIBFFI_CFLAGS := $(shell pkg-config --cflags libffi 2>/dev/null)
    LIBFFI_LDFLAGS := $(shell pkg-config --libs libffi 2>/dev/null)

    ifeq ($(LIBFFI_CFLAGS),)
      # Fallback paths for common installations
      ifeq ($(shell uname -s),Darwin)
        # macOS with Homebrew (try both Intel and Apple Silicon paths)
        LIBFFI_CFLAGS := -I/opt/homebrew/opt/libffi/include -I/usr/local/opt/libffi/include
        LIBFFI_LDFLAGS := -L/opt/homebrew/opt/libffi/lib -L/usr/local/opt/libffi/lib -lffi
      else
        # Linux fallback paths
        LIBFFI_CFLAGS := -I/usr/include -I/usr/local/include
        LIBFFI_LDFLAGS := -L/usr/lib -L/usr/local/lib -lffi
      endif
    endif

    CFLAGS += $(LIBFFI_CFLAGS)
    LDFLAGS += $(LIBFFI_LDFLAGS)
  endif
endif

# Optional libcurl support for URL-based #include directives
# Enable with: make JCC_HAS_CURL=1 or export JCC_HAS_CURL=1
# Disable with: make JCC_HAS_CURL=0
ifdef JCC_HAS_CURL
  ifneq ($(JCC_HAS_CURL),0)
    CFLAGS += -DJCC_HAS_CURL=1
    # Try pkg-config first, fallback to platform-specific paths
    LIBCURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
    LIBCURL_LDFLAGS := $(shell pkg-config --libs libcurl 2>/dev/null)

    ifeq ($(LIBCURL_CFLAGS),)
      # Fallback paths for common installations
      ifeq ($(shell uname -s),Darwin)
        # macOS with Homebrew (try both Intel and Apple Silicon paths)
        LIBCURL_CFLAGS := -I/opt/homebrew/opt/curl/include -I/usr/local/opt/curl/include -I/opt/homebrew/include
        LIBCURL_LDFLAGS := -L/opt/homebrew/opt/curl/lib -L/usr/local/opt/curl/lib -L/opt/homebrew/lib -lcurl
      else
        # Linux fallback paths
        LIBCURL_CFLAGS := -I/usr/include -I/usr/local/include
        LIBCURL_LDFLAGS := -L/usr/lib -L/usr/local/lib -lcurl
      endif
    endif

    CFLAGS += $(LIBCURL_CFLAGS)
    LDFLAGS += $(LIBCURL_LDFLAGS)
  endif
endif

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
