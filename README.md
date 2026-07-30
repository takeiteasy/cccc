
# cccc

> **WARNING!** Work in progress.

`CCCC` (**C**omprehensive **C** **C**ompensation **C**ompiler) is a C bytecode compiler + VM interpreter. C is compiled to custom bytecode, then interpreted in a built-in VM. CCCC is not designed to be a replacement for existing compilers (`cc` / `clang` / `gcc`), instead it's a drop-in frontend for them.

CCCC adds compile-time macro expansion and AST building capabilities on top of the existing toolchain by adding a new 'comptime' pass that runs between the preprocessor and the AST parser. Functions marked with `[[cccc::comptime]]` are run and expanded inside the VM during compilation and can then be forwarded to your native compiler.

Currently targets **MacOS** (aarch64/x86_64) and **Linux** (aarch64/x86_64). Windows support is planned but not started.

## Usage

```text
CCCC: Comprehensiev C Compensation Compiler
https://git.sr.ht/~takeiteasy/cccc

Usage: ./cccc [options] file...

Options:
	-h/--help                Show this message
	-I <path>                Add <path> to include search paths
	-i/--isystem <path>      Add <path> to system include paths (for non-standard headers)
	   --use-system-headers  Prefer SDK headers over CCCC polyfills for non-owned standard headers
	   --no-builtin-includes Do not fall back to CCCC's ./include for non-owned standard headers (requires --use-system-headers)
	   --sysroot <path>      Set SDK root; adds <path>/usr/include to system include paths and implies --use-system-headers
	-L/--library-path <path> Add <path> to dynamic library search paths
	-l/--library <name>      Link dynamic library by name or path
	-D <macro>[=def]         Define a macro
	-U <macro>               Undefine a macro
	-a/--ast                 Dump AST
	-p/--print-tokens        Print preprocessed tokens to stdout
	-E/--preprocess          Output preprocessed source code (traditional C -E)
	-m/--dump-expanded       Output macro-expanded source code (for gcc compatibility)
	-G/--emit-generated      Serialize runtime TU + macro-generated objects to C
	   --emit-only           With -G: only emit explicitly tagged content ([[cccc::emit]], $publish)
	   --attr-target=TARGET  Attribute spelling in generated output: auto, c23, gnu, msvc, strip
	-j/--json                Emit JSON for all eligible output (diagnostics, header declarations, --fusion-candidates, etc.)
	-J/--ffi-decls           Emit parsed function/struct/enum declarations as JSON (for FFI wrapper generation)
	-X/--no-preprocess       Disable preprocessing step
	-S/--no-stdlib           Do not link standard library
	-c[FMT]/--compile[=FMT]  Compile only; do not execute. FMT: bytecode (default), native
	                         bytecode: write .c4 (to -o file, or stdout if -o omitted
	                                   and stdout is not a TTY)
	                         native: require -o file; build a native executable via
	                                 CCCC_NATIVE_CC (cc, clang, or gcc)
	                         Use -cnative or --compile=native (short form must be
	                         attached; long form may use '=' or separate arg).
	-o/--out <file>          Output file. Required for -c=native. For -c=bytecode, writes
	                         bytecode to <file>; if omitted, writes to stdout
	-d/--disassemble         Disassemble bytecode to stdout
	-v/--verbose             Enable debug logging
	-g/--debug               Enable interactive debugger
	   --no-debug-on-crash   Disable auto-drop into debugger on crash (for test harnesses)
	-r/--repl                Start an interactive read-eval-print loop (no input file)
	-e/--entry <name>        Set the entry-point function (default: main)
	   --vm-profile          Count executed VM opcodes and print a report
	                         Combine with --json to also dump the profile as JSON to stdout

Testing Options:
	-t/--testing             Discover and run [[cccc::test]] functions
	   --test-c4             Bytecode round-trip: compile, save .c4, reload, then run tests
	                         (implies --testing; exercises FFI-table and bytecode persistence)
	   --test=GLOB           Run only tests whose name matches GLOB (implies --testing)
	   --test-suite=NAME     Run tests in NAME and its sub-suites (prefix match);
	                         glob metacharacters (*?[) switch to fnmatch (implies --testing)
	   --list-tests          List test names without running (implies --testing)
	   --fail-fast           Stop after the first failing test
	   --test-timeout=N      Per-test timeout in seconds (0 = no timeout;
	                         individual tests may override via
	                         [[cccc::test(timeout = ms)]])
	   --test-format=FMT     Output format for test results: tap (default), plain, json

Build Options:
	-b/--build               Run the input as a build script (declares native targets)
	   --build-entry=NAME    Build entry function to invoke (default: build_main)
	   --build-out-dir=PATH  Output directory for build artifacts (default: build/)
	   --build-dry-run       Print the toolchain command lines without executing them
	   --build-target=NAME   Build only the named target and its transitive dependencies
	   --build-tool-allow=N  Allowlist of tool names runnable via RunCustom/HaveTool/PkgConfig/CaptureCommand
	                         Accepts comma-separated or repeated flags. Default: allow all.
	   --build-jobs=N        Compile up to N source files in parallel per target (default: 1)
	   --build-keep-going    Continue building independent targets after a failure
	   --build-quiet         Suppress per-step command lines; only show errors and summary
	   --build-verbose       Print per-target headers and all command lines
	   --build-list-targets  List [[cccc::build_target]] factory names and exit
	   --build-profile=NAME  Set build profile: debug | release | relwithdebinfo | minsizerel
	   --build-triple=TRIPLE Cross-compile target triple (e.g. aarch64-linux-gnu; clang only)
	   --build-cc=COMPILER   Override CC binary for all targets (e.g. aarch64-linux-gnu-gcc)
	   --build-cache[=PATH]  Enable incremental builds: mtime+content-hash cache.
	                         Default cache dir: <out-dir>/.cccc-cache
	   --build-option=K=V    Pass a typed build option to the build script (GetBuildOption/HaveBuildOption).
	                         Accepts repeated flags: --build-option=foo=bar --build-option=baz=1
	   --build-install       After a successful build copy artifacts registered with InstallArtifact
	                         to the install prefix (default: PREFIX env var or /usr/local).
	   -- [args...]          Forward positional args to the build entry (BuildArgc/BuildArgv).

Warning Options:
	-Wall               Enable common warning categories
	-Wextra             Enable extra warning categories
	-W<name>            Enable a warning category
	-Wno-<name>         Disable a warning category
	-w/--Werror         Treat enabled warnings as errors
	-Werror=<name>      Treat one warning category as an error
	-Wno-error=<name>   Do not promote one warning category

Safety Levels (preset flag combinations):
	-0/--safety=none     No safety checks (VM heap stays on by default; add -V to also use the host allocator)
	-1/--safety=basic    Essential low-overhead checks (~5-10% overhead)
	-2/--safety=standard Comprehensive development safety (~20-40% overhead)
	-3/--safety=max      All safety features for deep debugging (~60-100%+ overhead)

Memory Safety Options (can be combined with safety levels):
	-B/--bounds-checks           Runtime array bounds checking
	   --uaf-detection           Use-after-free detection
	   --control-flow-integrity  Control-flow integrity (indirect call validation)
	   --type-checks             Runtime type checking on pointer dereferences
	   --uninitialized-detection Uninitialized variable detection
	   --overflow-checks         Detect signed integer overflow
	   --stack-canaries          Stack overflow protection
	   --heap-canaries           Heap overflow protection
	-M/--memory-leak-detection   Track allocations and report leaks at exit
	   --stack-instrumentation   Track stack variable lifetimes and accesses
	   --stack-errors            Enable runtime errors for stack instrumentation
	-P/--pointer-sanitizer       Enable all pointer checks (bounds, UAF, type)
	   --dangling-pointers       Detect use of stack pointers after function return
	   --alignment-checks        Validate pointer alignment for type
	   --provenance-tracking     Track pointer origin and validate operations
	   --invalid-arithmetic      Detect pointer arithmetic outside object bounds
	   --format-string-checks    Validate format strings in printf-family functions
	   --random-canaries         Use random stack canaries (prevents predictable bypass)
	   --memory-poisoning        Poison allocated/freed memory (0xCD/0xDD patterns)
	   --memory-tagging          Temporal memory tagging (track pointer generation tags)
	-T/--thread-safety           Threading safety diagnostics: race detection, lock-order
	                             inversion, double-lock, and atomic cast warnings
	-V/--vm-heap                 VM heap is on by default; pass -V to route malloc/free
	                             through the host allocator instead. Not compatible with
	                             -1/-2/-3 (or --safety=basic/standard/max), which require it

FFI Safety Options:
	   --ffi-allow=list       Allow only comma-separated native function names
	   --ffi-deny=list        Deny comma-separated native function names
	-F/--disable-ffi          Block all registered and dynamic native calls
	   --ffi-errors-fatal     Abort execution on FFI policy violations
	   --trap-fp-divzero      Abort on float division by zero instead of IEEE +-Inf/NaN
	   --ffi-type-checking    Validate registered FFI call arity at runtime

Language Standard:
	-s/--std=<std>       Select C language standard (default: gnu17)
	                     Supported: c99, c11, c17/c18, c23/c2x
	                     GNU variants: gnu99, gnu11, gnu17/gnu18, gnu23/gnu2x
	                     Note: -s/--std currently affects predefined macros only

Preprocessor Options:
	   --embed-limit=SIZE         Set #embed file size warning limit (e.g., 50MB, 100mb, default: 10MB)
	   --embed-hard-limit         Make #embed limit a hard error instead of warning
	   --macro-recursion-limit=N  Limit recursive pragma macro expansion (default: 256, 0=unlimited)
	-n/--max-errors=N             Cap diagnostics at N (default: 20)
	-C/--no-comptime              Skip the comptime/macro phase entirely (for
	                              large TUs that don't use [[cccc::comptime]])
	   --comptime-include-all     Forward all #include'd declarations to the
	                              comptime pass (legacy behavior; default is
	                              runtime-only; use #include @shared to opt in
	                              individual headers)
	   --allow-comptime-pp-bleed  Allow #define/#undef inside one
	                              [[cccc::comptime]] function body to remain
	                              visible to other comptime function bodies
	                              (pre-#283 behavior; default is isolated)

Optimization:
	-O/--optimize[=LEVEL]        Enable bytecode optimization (default: disabled)
	                             LEVEL: 0=none, 1=basic, 2=standard, 3=aggressive, 4=fused
	                             1: constant folding (-ffold)
	                             2: +peephole, +CSE (-fpeephole -fcse)
	                             3: +copy-prop, +DCE (-fcopy-prop -fdce)
	                             4: +opcode fusion, +redundant extension elimination (-ffuse -felim-ext)
	-f<pass>                     Enable a single optimisation pass regardless of -O level.
	-fno-<pass>                  Disable a pass even if enabled by -O.
	                             Passes: fold, peephole, copy-prop, dce, cse, fuse, elim-ext
	                             Examples: -O3 -fno-cse, -O0 -fpeephole, -ffold -fdce
	--fma                        Enable single-rounding FMA (-ffuse implied; may change FP results)
	--posix-emulation            Enable lossy/approximate emulation of POSIX functions the
	                             host doesn't natively support (e.g. ppoll() on macOS). Off
	                             by default: such functions are undeclared/unregistered,
	                             matching a native compiler on the same host. VM-only.
	--inline-limit=N             Limit inlining to N AST nodes (default: 256)

Static Bytecode Analysis (compile or load input, walk text segment, exit):
	--ngrams[=N]            Static opcode n-gram analysis (N=2 or 3, default 2)
	--ngrams-top=N          Show top N sequences (default 25)
	--ngrams-per-file       Print a per-input section in addition to the aggregate
	--fusion-candidates[=N] Use-def fusion candidate analysis (top N, default 50)
	                        JSON output via -j/--json

Inline Assembly:
	-A/--asm-passthru   Compile asm("...") statements via native C compiler
	                    and execute them via FFI (default: no-op)

Example:
	./cccc -o hello hello.c
	./cccc -I ./include -D DEBUG -o prog prog.c
	echo 'int main() { return 42; }' | ./cccc -
```

## Features

- **Compile-time macros** — C functions annotated with `[[cccc::macro]]`, `__attribute__((macro))`, or the `@macro` shorthand that run during compilation (see [MACROS.md](docs/MACROS.md))
  - Inline pre-parse generators for parser-visible functions and declarations (`[[cccc::macro(inline)]]`)
  - File-scope macro calls for explicit source-order generation
  - Call-site expansion for expression and statement rewriting
  - Backtick quasi-quoting with `${...}` interpolation, `Quote(...)` templates, hygienic type/symbol reflection, `__cccc_gensym`, and AST construction helpers
- **Native compilation pipeline** — `-c=native` runs the CCCC frontend (preprocessor, compile-time macros) and hands the resulting C to `CCCC_NATIVE_CC` (or `cc` / `clang` / `gcc`) for an actual native build
  - This is the production path: full toolchain performance, system libraries, no VM overhead
  - `-o <file>` is required to name the produced executable; the temporary C source is removed after the build
  - `-I`, `-i`, `-D`, `-U`, `-L`, `-l`, and `--std=` are forwarded to the underlying compiler
- **Register-based bytecode VM** — compiles C to a portable instruction set with 32 integer and 32 floating-point registers, then executes it in a built-in interpreter (see [VM.md](docs/VM.md))
  - Powers compile-time macro execution
  - Serves as a toolchain-free, introspectable runtime for the safety suite, debugger, and profiler
  - Use it for portability, sandboxing, and quick iteration without a system compiler
  - Supports POSIX `pthread` programs through a correctness-first VM GIL; bytecode execution is serialized, while blocking pthread calls release the GIL
- **Memory safety suite** — runtime detection of common C bugs (see [SAFETY.md](docs/SAFETY.md))
  - Four preset levels (`-0` through `-3`): zero overhead to paranoid mode
  - Covers use-after-free, buffer overflows, dangling pointers, uninitialized reads, integer overflow, CFI, and more
- **Interactive debugger** — GDB-like source-level debugging (see [TOOLING.md](docs/TOOLING.md))
  - Breakpoints (line, function, conditional), watchpoints, register and memory inspection
  - Source map export API for IDE integrations (`cc_output_source_map_json`)
- **Interactive REPL** — top-level read-eval-print loop for declarations and expressions (see [TOOLING.md](docs/TOOLING.md))
  - `-r`/`--repl`; declarations persist and compile incrementally across the session, expressions print a typed result
  - Multi-line continuation, `:type`/`:load`/`:help`/`:quit` session commands, optional readline history
- **Bytecode optimizer** — optional passes on generated bytecode (see [OPTIMIZATION.md](docs/OPTIMIZATION.md))
  - `-O[N]` / `--optimize[=N]` with levels 1–4: constant folding, peephole, scalar local promotion, indexed load/store lowering, dead code elimination, and automatic fused-op rewriting; individual passes controllable with `-f<pass>` / `-fno-<pass>` (e.g. `-O3 -fno-cse`, `-O0 -fpeephole`)
- **URL includes** — fetch headers directly from URLs with `#include <https://...>`; build with `make CCCC_HAS_CURL=1` (optional, requires libcurl)
- **Real IEEE-754-2008 decimal floating-point** — `_Decimal32/64/128` arithmetic, `<decimal_math.h>` transcendentals (`sqrtd64`, `powd128`, ...), `strtod32/64/128`, `fesetround()`-aware rounding and `fetestexcept()`-visible exceptions, and compile-time constant folding, via the Intel BID library; build with `make CCCC_HAS_DECIMAL=1` after running `tools/fetch_intel_bid.sh` (optional, never vendored; see [VM.md](docs/VM.md#decimal-floating-point))
- **Built-in test framework** — `[[cccc::test]]`, `__attribute__((test))`, or `@test` attribute and `Assert*` macros for writing tests in C (see [TESTING.md](docs/TESTING.md))
  - Run with `--testing`; outputs TAP 13 format; no external dependencies or includes needed
  - `#include [[cccc::test]] "fixtures.h"` conditionally includes a file only in `--testing` mode
- **Mode predefined macros** — `__CCCC_BUILD_MODE__`, `__CCCC_TEST_MODE__`, or `__CCCC_COMP_MODE__` is defined at compile time to reflect the active execution mode, enabling `#ifdef`-based mode branching
- **Attribute support** — GNU `__attribute__((...))`, C23 `[[...]]`, and `@name` shorthand with partial semantic support (see [COVERAGE.md](docs/COVERAGE.md))
  - Covers `packed`, `aligned`, `unused`/`maybe_unused`, `deprecated`, and CCCC-specific `macro`/`comptime`/`test`
  - `@comptime`, `@test`, `@packed`, `@nodiscard`, etc. are sugar for the longer attribute forms
- **Warning controls** — gcc/clang-style `-W` categories and `-Werror` promotion (see [TOOLING.md](docs/TOOLING.md))
  - Warnings are disabled by default and can be enabled with `-Wall`, `-Wextra`, or individual categories
- **JSON reflection output** — dump all function, struct, union, enum, and global definitions
  - `./cccc --ffi-decls -o lib.json lib.h` — useful for generating FFI wrappers
- **VM heap** — built-in allocator that intercepts `malloc`/`free`/`calloc`/`realloc` at compile time
  - On by default at every safety level, including `-0`; pass `-V`/`--vm-heap` to opt back into the
    host allocator (only valid at safety level 0 — `-1`/`-2`/`-3` require the VM heap)

## Building

```bash
make          # Build cccc
make all      # Build cccc + the platform shared library, then run tests and build docs
```

Produces:
- `cccc` — compiler executable (C source → VM bytecode, or → native via `-c=native`)
- `libcccc.dylib` (macOS) or `libcccc.so` (Linux) — shared library for embedding CCCC in other applications

CCCC requires libffi for native FFI calls. The Makefile uses `pkg-config
libffi` when available, with Homebrew and common Unix fallbacks.

URL includes are an optional feature requiring libcurl: `make CCCC_HAS_CURL=1`.

Host C stack traces on crash (the `src/backtrace/` library) are on by default
and require no extra system dependencies — they are vendored directly in the
repo. Disable with `make CCCC_HAS_BACKTRACE=0`. On macOS, run `make dsym`
after building to unlock full `file:line` resolution in crash traces (see
[TOOLING.md](docs/TOOLING.md#host-c-backtrace-on-crash)).

Real decimal floating-point is an optional feature requiring the Intel BID
library, which is fetched and built on demand (never vendored):
`tools/fetch_intel_bid.sh && make CCCC_HAS_DECIMAL=1` (see
[VM.md](docs/VM.md#decimal-floating-point)).

### Compile Natively (production)

`-c=native` is the production path: CCCC preprocesses, expands compile-time macros, then hands the resulting C to a real system compiler. `-o <file>` is **required** to name the output executable; the temporary C source is removed after the build.

```bash
# Write a native executable (build only — does not run)
./cccc -c=native -o program program.c

# Override compiler selection
CCCC_NATIVE_CC=clang ./cccc -c=native -o program program.c

# Forward include / define / library flags to the underlying compiler
./cccc -c=native -I./include -DDEBUG -L./lib -lz -o app app.c
```

Native mode runs CCCC's preprocessing and compile-time macro stages first, then passes serialized C to `CCCC_NATIVE_CC` when set, otherwise `cc`, `clang`, or `gcc`. `-I`, `-i`, `-D`, `-U`, `-L`, `-l`, `--std=`, and generated-output attribute policy from `--attr-target=` are forwarded through the frontend; VM-only options (bytecode output, disassembler, `--optimize`, debugger, profiler, `-0`…`-3` safety levels) are rejected in this mode. To run the binary afterwards, invoke it directly: `./program`.

### Run in the VM

Without `-c=native`, CCCC compiles C to portable bytecode and runs it in its built-in interpreter. Use this when you want a toolchain-free, introspectable, or sandboxed runtime — for macro bodies, quick iteration, the debugger, the safety suite, or `--vm-profile`.

```bash
# Compile and run immediately on the VM
./cccc program.c

# Compile to a bytecode file for later execution
./cccc -o program.bin program.c

# Multiple input files
./cccc -o app.bin main.c utils.c helpers.c

# With preprocessor flags
./cccc -I./include -DDEBUG -o debug.bin main.c
```

Bytecode uses 32-bit instruction words; 64-bit immediates are split across two consecutive words. Saved `.c4` files include relocation and ABI metadata so loaded programs re-anchor global pointers, function-pointer offsets, FFI entries, and aggregate return buffers to the new VM instance. See [VM.md](docs/VM.md) for the full instruction set, ABI, and file format.

## Running Tests

```bash
python3 tools/tests.py                     # Full test suite
python3 tools/tests.py --match "*embed*"   # Run only tests matching a glob pattern
python3 tools/tests.py -j 4                # Run with 4 parallel workers
python3 tools/tests.py -2                  # Run all tests under safety level 2
python3 tools/tests.py --leaks             # Enable leak detection (leaks on macOS, valgrind on Linux)
python3 tools/tests.py --c4               # Bytecode round-trip: compile each positive test to .c4, then run it
# I recommend running --leaks with -j (takes a long time synchronously)
```

`make test` calls `tools/run_tests.py`, the unified orchestrator that runs:
source-mode suite, `.c4` bytecode round-trip, the macOS host-signal debugger
integration (skipped on other platforms), the REPL and conditional-breakpoint
PTY integrations, the SQLite smoke test, and the `src/stdlib` FFI registration
audit (`make audit-ffi`). Run sub-suites standalone with `python3 tools/tests.py`
or `python3 tools/tests.py --c4`. See [TESTING.md](docs/TESTING.md) for details.

### macOS x86_64 with Rosetta 2

On Apple Silicon, the x86_64 workflow uses Apple Clang, the macOS SDK's
universal libffi, and Rosetta 2. Each stage has a one-command Make target:

```bash
make macos-x86_64-build
make macos-x86_64-smoke
make macos-x86_64-test
```

The smoke target asserts `uname -m` and `file` output, runs a VM program and
`--asm-passthru`, and confirms that `-c=native` produces an executable x86_64
child process. The test target runs the source suite, `.c4` round-trip, and
macOS host-signal debugger integration using `cccc-macos-x86_64`.

### Linux with Colima

Two Linux targets are supported, each using a named Colima profile.

**Linux/amd64 (VZ/Rosetta)** — create the profile once:

```bash
colima start cccc-linux-amd64 --runtime containerd --arch aarch64 \
  --vm-type vz --vz-rosetta --cpu 4 --memory 4
make linux-x86_64-build
make linux-x86_64-smoke
make linux-x86_64-test
```

The amd64 image is tagged `cccc-linux-amd64`. Override `COLIMA_PROFILE` or
`LINUX_AMD64_IMAGE` when using different names.

**Linux/aarch64 (native arm64)** — create the profile once:

```bash
colima start cccc-linux-arm64 --runtime containerd --arch aarch64 \
  --vm-type vz --cpu 4 --memory 4
make linux-aarch64-build
make linux-aarch64-smoke
make linux-aarch64-test
```

The arm64 test target runs `tools/run_tests.py` in one unbatched pass (no
Rosetta binfmt limit on native arm64). Override `LINUX_ARM64_PROFILE` or
`LINUX_ARM64_IMAGE` when using different names.

Architecture-specific results and known failures are in [TESTING.md](docs/TESTING.md).

### Sanitizer Builds

```bash
make asan     # Build cccc-asan with AddressSanitizer + UBSan
make ubsan    # Build cccc-ubsan with UndefinedBehaviorSanitizer
make tsan     # Build cccc-tsan with ThreadSanitizer
make msan     # Build cccc-msan with MemorySanitizer (Linux only)

# Run the test suite with a sanitizer binary
python3 tools/tests.py --asan -j 4
python3 tools/tests.py --ubsan -j 4
```

On macOS, `cccc-asan` carries a built-in suppression for a heap over-read
inside Apple's own `strfmon()` (not a CCCC bug -- confirmed with a standalone
`clang -fsanitize=address` program; glibc is unaffected). See the
`__asan_default_suppressions` hook in `src/stdlib/posix.c`.

## Credits 

This project builds on [chibicc](https://github.com/rui314/chibicc) for the C frontend and on ideas from [c4](https://github.com/rswier/c4) / [write-a-C-interpreter](https://github.com/lotabout/write-a-C-interpreter) for the VM-oriented execution model (You can run [test_c4](tests/test_c4.c) inside cccc by running `python3 tools/test.py --match "*c4*"`).

[stb_sprintf.h](https://github.com/nothings/stb/blob/master/stb_sprintf.h) is used as a base for C23 compliant `*printf` functions.

Host C crash backtraces use [libbacktrace](https://github.com/ianlancetaylor/libbacktrace) by Ian Lance Taylor (BSD 3-clause), vendored in `src/backtrace/`.

Other libraries used (linked, not vendored): [libcurl](https://curl.se/libcurl/) by Daniel Stenberg, and [libffi](https://github.com/libffi/libffi) by Anthony Green.

## License

```text
    cccc

    Copyright (C) 2025  George Watson

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
```
