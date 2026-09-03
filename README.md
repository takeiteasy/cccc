
# cccc

> **WARNING!** Work in progress.

`CCCC` (**C**omprehensive **C** **C**ompensation **C**ompiler) is a C bytecode compiler + VM interpreter. C is compiled to custom bytecode, then interpreted in a built-in VM. CCCC is not designed to be a replacement for existing compilers (`cc` / `clang` / `gcc`), instead it's a drop-in frontend for them.

CCCC adds compile-time macro expansion and AST building capabilities on top of the existing toolchain by adding a new 'comptime' pass that runs between the preprocessor and the AST parser. Functions marked with `[[cccc::comptime]]` are run and expanded inside the VM during compilation and can then be forwarded to your native compiler.

Currently targets **MacOS** (aarch64) and **Linux** (aarch64/x86_64). Windows support is planned but not started. Release builds are published at [github.com/takeiteasy/cccc/releases](https://github.com/takeiteasy/cccc/releases).

Generated API docs for the public headers (`building.h`, `reflection.h`, `testing.h`) are published at [takeiteasy.github.io/cccc](https://takeiteasy.github.io/cccc/).

## Comptime example

A `@comptime` function is ordinary C that CCCC runs *during* compilation. It can fold constants, build AST fragments with `Quote(...)` templates (`$1` splices an argument in), and emit whole declarations into the program:

```c
#include <stdio.h>

// (1) A call-site macro: the call below is replaced by this template.
@comptime Node *square(Node *x) { return Quote("($1) * ($1)", x); }

// (2) A comptime helper in a global initializer — folded to a constant.
@comptime Node *pow2(Node *k) { return Quote("1 << ($1)", k); }

// (3) A file-scope macro that emits real declarations into the program.
@comptime Node *emit_vec2(void) {
    return Quote("{ struct Vec2 { int x, y; };"
                 "  int vec2_dot(struct Vec2 a, struct Vec2 b) {"
                 "      return a.x * b.x + a.y * b.y; } }");
}
emit_vec2();

int TABLE_SIZE = pow2(8);              // 256, computed at compile time

int main(void) {
    struct Vec2 a = { 3, 4 };
    struct Vec2 b = { square(2), 5 };  // square(2) -> (2) * (2)
    printf("%d %d\n", TABLE_SIZE, vec2_dot(a, b));   // prints: 256 32
    return 0;
}
```

The same source runs on the VM (`cccc demo.c`) or lowers to a native binary (`cccc -c=native -o demo demo.c`). See [MACROS.md](man/MACROS.md) for the full API.

## Guides

Guides live in [`man/`](man/):

| Guide | Covers |
|---|---|
| [BUILD.md](man/BUILD.md) | Building `cccc` itself — `make` stage0 vs the `--build` full build, `CCCC_HAS_*` feature knobs |
| [BUILD_MODE.md](man/BUILD_MODE.md) | The embedded `--build` system: writing `build.c` scripts, the builder API, targets, profiles, cross-compilation |
| [MACROS.md](man/MACROS.md) | Compile-time macros — `[[cccc::comptime]]` functions that run in the VM during compilation, AST building, quasi-quoting |
| [NATIVE.md](man/NATIVE.md) | `-c=native` — serialising the compiled program back to portable C and handing it to a real host compiler |
| [HEADERS.md](man/HEADERS.md) | `#include` resolution: CCCC's bundled headers, project headers, `--use-system-headers`, host-header hand-off |
| [SAFETY.md](man/SAFETY.md) | The runtime memory-safety suite — safety levels `-0`…`-3` and the individual detectors |
| [TEST_MODE.md](man/TEST_MODE.md) | Writing tests in C with `[[cccc::test]]` and the `Assert*` macros |
| [DEBUGGER.md](man/DEBUGGER.md) | The interactive source-level debugger (`-g`), auto-debug-on-crash, the source-map API |
| [REPL.md](man/REPL.md) | The interactive read-eval-print loop (`-r`) |
| [WARNINGS.md](man/WARNINGS.md) | `-W` warning categories, `-Werror`, pragma suppression, machine-readable output |
| [COVERAGE.md](man/COVERAGE.md) | C language coverage (C89–C23, GNU/MS extensions) — a support table |
| [ATTRIBUTES.md](man/ATTRIBUTES.md) | `__attribute__`, `[[...]]`, and `@name` attribute support |
| [STDLIB.md](man/STDLIB.md) | Standard-library and POSIX header coverage — a support table |
| [TYPES.md](man/TYPES.md) | Type compatibility, `__builtin_types_compatible_p`, `_Generic` arm selection, and the `--compiler-family` gcc/clang policy switch |

## Usage

```text
CCCC: Comprehensive C Compensation Compiler
https://git.sr.ht/~takeiteasy/cccc

Usage: ./build/cccc [options] file...

Options:
	-h/--help                Show this message
	   --version             Print version, git describe, host triple, and enabled features
	-I/--include <path>      Add <path> to include search paths
	-i/--isystem <path>      Add <path> to system include paths (for non-standard headers)
	   --use-system-headers  Prefer SDK headers over CCCC polyfills for non-owned standard headers
	   --no-builtin-includes Do not fall back to CCCC's own bundled headers for non-owned standard headers (requires --use-system-headers)
	   --sysroot <path>      Set SDK root; adds <path>/usr/include to system include paths and implies --use-system-headers
	-L/--library-path <path> Add <path> to dynamic library search paths
	-l/--library <name>      Link dynamic library by name or path
	-D/--define <macro>[=def] Define a macro
	-U/--undef <macro>       Undefine a macro
	-a/--ast                 Dump AST
	-p/--print-tokens        Print preprocessed tokens to stdout
	-E/--preprocess          Output preprocessed source code (traditional C -E)
	-m/--dump-expanded       Output macro-expanded source code (for gcc compatibility)
	   --emit-only           With -c=generated: only emit explicitly tagged content ([[cccc::emit]])
	   --attr-target=TARGET  Attribute spelling in generated output: auto, c23, gnu, msvc, strip
	   --compiler-family=FAM  Host family CCCC's front end models where gcc/clang
	                         disagree (__builtin_types_compatible_p): gcc (default),
	                         clang, auto (probe CCCC_NATIVE_CC)
	   --emit-cccc           Preserve CCCC dialect syntax ([[cccc::...]], @-attrs, checked-pointer
	                         qualifiers, cccc-only #includes) in -E/-m/-c=native/-c=generated output
	                         instead of stripping it to portable C. With -c=native, the usual
	                         cc/clang/gcc PATH search is disabled -- CCCC_NATIVE_CC must name a
	                         compiler that understands the dialect explicitly
	   --no-layout-guards    Suppress the _Static_assert layout guards emitted next to
	                         every aggregate definition in -m/-c=generated/-c=native
	                         output (see man/NATIVE.md); on by default
	-j/--json                Emit JSON for all eligible output (diagnostics, header declarations, etc.)
	-J/--ffi-decls           Emit parsed function/struct/enum declarations as JSON (for FFI wrapper generation)
	-X/--no-preprocess       Disable preprocessing step
	-S/--no-stdlib           Do not link standard library
	-c[FMT]/--compile[=FMT]  Compile only; do not execute. FMT: native (default), generated
	                         native: build a native executable via CCCC_NATIVE_CC
	                                 (cc, clang, or gcc); writes to -o file, or ./a.out
	                                 if -o omitted
	                         generated: serialize the runtime TU + macro-generated
	                                    objects to C; writes to -o file, or ./a.gen.c
	                                    if -o omitted
	                         Aliases: native=n, generated=gen=g. Use
	                         -cnative or --compile=native (short form must be
	                         attached; long form may use '=' or separate arg).
	   --test-run[=LEVEL]    Run the program under the VM (safety=max by default; LEVEL
	                         accepts none/basic/standard/max or 0/1/2/3, same as --safety=)
	                         before compiling. Refuses to compile (nonzero exit, no
	                         artifact written) if the run crashes, hits a VM-detected
	                         safety violation, or hangs; the exit code itself is not
	                         checked. Implies -c=native when no -c is given; an
	                         explicit -c=FMT still picks the format
	-o/--out <file>          Output file. For -c=native, defaults to ./a.out if omitted.
	                         For -c=generated, defaults to ./a.gen.c if omitted.
	-d/--disassemble         Disassemble compiled bytecode to stdout
	-v/--verbose             Enable debug logging
	-g/--debug               Enable interactive debugger
	   --no-debug-on-crash   Disable auto-drop into debugger on crash (for test harnesses)
	-r/--repl                Start an interactive read-eval-print loop (no input file)
	-e/--entry <name>        Set the entry-point function (default: main)
	   --vm-profile          Count executed VM opcodes and print a report
	                         Combine with --json to also dump the profile as JSON to stdout

Testing Options:
	-t/--testing[=vm|native]
	                         Discover and run [[cccc::test]] functions. Bare -t/--testing
	                         (default =vm) runs them in-process; =native serializes the
	                         harness itself and runs
	                         it as a standalone binary via CCCC_NATIVE_CC (implies -c=native;
	                         [[cccc::test_setup/teardown]] hooks and negative tests are not
	                         supported under =native, see man/TEST_MODE.md)
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
	                         Env equivalent: CCCC_BUILD_CC (else cc/clang/gcc PATH search --
	                         separate from -c=native's own CCCC_NATIVE_CC)
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
	   --checked-pointers        Runtime range checks for checked-pointer
	                             ([[cccc::single/array/ntarray]]) accesses
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
	-V/--no-vm-heap             VM heap is on by default; pass -V to route malloc/free
	                             through the host allocator instead. Not compatible with
	                             -1/-2/-3 (or --safety=basic/standard/max), or with
	                             --bounds-checks/--uaf-detection/--type-checks/
	                             --heap-canaries/--memory-leak-detection/--memory-tagging,
	                             which require it

FFI Safety Options:
	   --ffi-allow=list       Allow only comma-separated native function names
	   --ffi-deny=list        Deny comma-separated native function names
	-F/--disable-ffi          Block all registered and dynamic native calls
	   --ffi-errors-fatal     Abort execution on FFI policy violations
	   --ffi-type-checking    Validate registered FFI call arity at runtime

Language Standard:
	-s/--std=<std>       Select C language standard (default: gnu23)
	                     Supported: c89/c90, c99, c11, c17/c18, c23/c2x
	                     GNU variants: gnu89/gnu90, gnu99, gnu11, gnu17/gnu18, gnu23/gnu2x
	                     Gates predefined macros, tokenizer syntax (e.g. C23 attributes/
	                     digit separators), and preprocessor features per standard

Preprocessor Options:
	   --embed-limit=SIZE         Set #embed file size warning limit (e.g., 50MB, 100mb, default: 10MB)
	   --embed-hard-limit         Make #embed limit a hard error instead of warning
	   --macro-recursion-limit=N  Limit recursive comptime macro expansion (default: 256, 0=unlimited)
	-n/--max-errors=N             Cap diagnostics at N (default: 20)
	-C/--no-comptime              Skip the comptime/macro phase entirely (for
	                              large TUs that don't use [[cccc::comptime]])
	   --comptime-include-all     Forward all #define macros to the comptime pass,
	                              and widen the declaration index to include
	                              system headers (both default off; declarations
	                              from non-system headers already resolve
	                              on demand without this flag)
	   --allow-comptime-pp-bleed  Allow #define/#undef inside one
	                              [[cccc::comptime]] function body to remain
	                              visible to other comptime function bodies
	                              (pre-#283 behavior; default is isolated)

Optimization:
	-O/--optimize[=LEVEL]        No effect in VM mode (the VM does not optimise).
	                             Under -c=native it is forwarded verbatim as -O<n>
	                             to the host cc. LEVEL: 0..4.
	--trap-fp-divzero            Abort on float division by zero instead of IEEE +-Inf/NaN
	--posix-emulation            Enable lossy/approximate emulation of POSIX functions the
	                             host doesn't natively support (e.g. ppoll() on macOS). Off
	                             by default: such functions are undeclared/unregistered,
	                             matching a native compiler on the same host. Also restores
	                             raw ioctl() passthrough for request codes outside the
	                             layout-verified allowlist (off by default there too). VM-only.

Inline Assembly:
	-A/--asm-passthru   Compile asm("...") statements via native C compiler
	                    and execute them via FFI (default: no-op)

Example:
	./build/cccc -o hello hello.c
	./build/cccc -I ./include -D DEBUG -o prog prog.c
	echo 'int main() { return 42; }' | ./build/cccc -
```

## Credits 

This project builds on [chibicc](https://github.com/rui314/chibicc) for the C frontend and on ideas from [c4](https://github.com/rswier/c4) / [write-a-C-interpreter](https://github.com/lotabout/write-a-C-interpreter) for the VM-oriented execution model (You can run [test_c4](tests/test_c4.c) inside cccc by running `python3 tools/test.py --match "*c4*"`).

[stb_sprintf.h](https://github.com/nothings/stb/blob/master/stb_sprintf.h) is used as a base for C23 compliant `*printf` functions.

Host C crash backtraces use [libbacktrace](https://github.com/ianlancetaylor/libbacktrace) by Ian Lance Taylor (BSD 3-clause), vendored in `src/backtrace/`.

Other libraries used (linked, not vendored): [libcurl](https://curl.se/libcurl/) by Daniel Stenberg, and [libffi](https://github.com/libffi/libffi) by Anthony Green.

## License

```text
cccc

Copyright (C) 2025 George Watson

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.
```
