# Building CCCC

How to build the `cccc` binary from a source checkout. For writing `build.c`
scripts for your *own* projects, see [build.md](build.md).

## Prerequisites

- A host C compiler (`cc` / `clang` / `gcc`) and `make`.
- **libffi** — required, for the VM's foreign-function interface.
  - macOS: ships with the system / Xcode CLT.
  - Linux: `apt install libffi-dev` (or your distro's equivalent).

Everything else is optional and probed at build time:

| Optional dependency | Enables | Notes |
|---|---|---|
| GNU **readline** | line editing/history in the REPL | probed via `pkg-config`; Homebrew fallback on macOS |
| **CoreServices** (macOS) | `--watch`, through FSEvents | ships with the system; linked on every macOS build |
| **libbacktrace** | symbolic host C stack traces on a crash | git submodule at `vendor/libbacktrace`, **on by default** when the submodule is initialised |
| **libcurl** | URL `#include`/`#embed` and URL-aware `__has_include`/`__has_embed` | off by default |
| **doxygen** (+ graphviz) | the HTML API docs (`docs` target) | off by default, never part of a normal build |
| Intel **BID** library | `_Decimal32/64/128` decimal floating-point | never vendored; fetch first (see below) |

## Quick build

```sh
make                       # one pass: ./cccc
./cccc --build build.c --build-list-targets
```

`make` is the main build and produces `./cccc`; `make MODE=release` optimizes
it. `build.c` is the same build expressed through cccc's own `--build` API: its
default target produces `build/cccc`, and it drives the extra targets below
(`test` builds its own `build/cccc` and tests that).

`make` detects the optional features itself: libbacktrace, readline, and the
`CCCC_HAS_*` opt-ins below. Clone with `--recurse-submodules` (or run
`git submodule update --init`) to get libbacktrace.

The bundled headers under `include/` are embedded into `./cccc` at link time,
so editing one needs a `make` to take effect. A new header needs a row in
`src/std_table.c`.

## Optional feature knobs (`CCCC_HAS_*`)

A few guest-visible features are off by default because they pull in an extra
host dependency. Each is gated behind a `CCCC_HAS_*` environment variable read
by the `Makefile` and `build.c`:

| Env var | Feature | Extra dependency |
|---|---|---|
| `CCCC_HAS_CURL=1` | URL-based `#include`/`#embed` directives and URL-aware `__has_include()`/`__has_embed()` probes; fetch knobs `--url-cache-dir`, `--url-cache-clear`, `--url-timeout`, `--url-max-size` | `libcurl` |
| `CCCC_HAS_DECIMAL=1` | `_Decimal32/64/128` via the Intel BID library | never vendored — run `tools/fetch_intel_bid.sh` first |
| `CCCC_HAS_NDBM=1` | `<ndbm.h>` on Linux (macOS/BSD have it natively) | `libgdbm-compat-dev` (links `-lgdbm_compat`) |
| `CCCC_HAS_BACKTRACE=0` | opts *out* of the vendored libbacktrace | on by default |

```sh
CCCC_HAS_CURL=1 make
tools/fetch_intel_bid.sh && CCCC_HAS_DECIMAL=1 make
```

## Selecting the toolchain

- `CCCC_BUILD_CC` (else `cc` / `clang` / `gcc`) selects the compiler that
  builds `--build` targets (the sanitizer and release builds of `cccc`
  included). `CCCC_BUILD_AR` (else `ar`) selects the archiver. `make` uses `CC`.
- This is separate from `CCCC_NATIVE_CC`, which only selects the compiler
  `-c=native` hands its *generated* C to.
- A real gcc is supported, e.g. on macOS:
  `make CC=/opt/homebrew/bin/gcc-16`.

## Build targets

`build.c` covers what used to be Makefile targets. A few common ones:

| Target | What it does |
|---|---|
| `test` | build `build/cccc`, run the whole test suite |
| `cccc` | full `build/cccc` |
| `docs` | Doxygen HTML for the public headers into `build/docs/html/` (needs `doxygen` + `graphviz`) |
| `cccc_asan` / `cccc_ubsan` / `cccc_tsan` / `cccc_msan` / `sanitizers` | sanitizer builds |
| `afl` / `afl_asan` / `fuzz_harness` | fuzzing binaries |
| `libcccc` | shared/static library |
| `clean` | remove build outputs |

```sh
./cccc --build build.c --build-list-targets   # list them all
```

The published API docs are at <https://takeiteasy.github.io/cccc/>; the `docs`
output is gitignored and regenerated locally on demand.
