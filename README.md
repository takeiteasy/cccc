# cccc

> **WARNING!** Work in progress. The interpreter and compile times are *long*. But it is able to fully compile itself

`CCCC` (**C**omprehensive **C** **C**ompensation **C**ompiler) is a C bytecode compiler + VM interpreter. C is compiled to custom bytecode, then interpreted in a built-in VM. CCCC is not designed to be a replacement for existing compilers (`cc` / `clang` / `gcc`), instead it's a drop-in frontend for them.

CCCC adds compile-time macro expansion and AST building on top of the existing toolchain: functions marked `@comptime` run inside the VM during compilation, and the result can be forwarded to your native compiler. A test framework and a build system are built in, so a project needs no external test runner or build tool.

Targets **macOS** (aarch64) and **Linux** (aarch64/x86_64). Release builds: [github.com/takeiteasy/cccc/releases](https://github.com/takeiteasy/cccc/releases). Issue tracker: [todo.sr.ht/~takeiteasy/cccc](https://todo.sr.ht/~takeiteasy/cccc). Generated API docs for the public headers: [takeiteasy.github.io/cccc](https://takeiteasy.github.io/cccc/).

## Examples

### Compile-time macros, tests and build recipes

A `@comptime` function is ordinary C that CCCC runs *during* compilation. It can fold constants, build AST fragments with `Quote(...)` templates (`$1` splices an argument in), and emit whole declarations into the program. Tests and build recipes live in the same file.

```c
#include <stdio.h>

// A call-site macro: the call is replaced by the returned template.
@comptime Node *square(Node *x) { return Quote("($1) * ($1)", x); }

// A comptime function in a global initializer folds to a constant.
@comptime Node *pow2(Node *k) { return Quote("1 << ($1)", k); }

// A file-scope macro that emits real declarations into the program.
@comptime Node *emit_vec2(void) {
    return Quote("{ struct Vec2 { int x, y; };"
                 "  int vec2_dot(struct Vec2 a, struct Vec2 b) {"
                 "      return a.x * b.x + a.y * b.y; } }");
}
emit_vec2();

int TABLE_SIZE = pow2(8);                  // 256, computed at compile time

int main(void) {
    struct Vec2 a = { 3, 4 };
    struct Vec2 b = { square(2), 5 };      // square(2) -> (2) * (2)
    printf("%d %d\n", TABLE_SIZE, vec2_dot(a, b));   // 256 32
    return 0;
}

// Discovered and run by `cccc --testing comptime.c`.
@test void dot_product(void) {
    struct Vec2 a = { 3, 4 }, b = { square(2), 5 };
    AssertEq(vec2_dot(a, b), 32);
}

// Run by `cccc --build comptime.c`.
@build_target BuildTarget *demo(Builder *ctx) {
    BuildTarget *t = CcccExecutable(ctx, "demo");
    AddSource(t, "comptime.c");
    return t;
}
```

`cccc comptime.c` runs `main()` on the VM, `cccc -c=native -o demo comptime.c` builds a native binary, `cccc --testing comptime.c` runs the `@test`, and `cccc --build comptime.c` runs the `@build_target`. See [docs/macros.md](docs/macros.md), [docs/testing.md](docs/testing.md) and [docs/build.md](docs/build.md).

### Derived code

Attributes on a type generate functions for it at compile time:

```c
#include <stdio.h>
#include <string.h>
#include <cccc/derive.h>
#include <cccc/json.h>

@to_string
@from_string
typedef enum { RED, GREEN, BLUE } Color;

@to_json
@from_json
@equals
@ctor
typedef struct {
    int id;
    char name[16];
    Color favourite;
} User;

int main(void) {
    User u = User_new(7, "ann", GREEN);

    char buf[128];
    User_to_json(&u, buf, sizeof buf);
    puts(buf);                                // {"id":7,"name":"ann","favourite":1}

    User v;
    if (User_from_json(&v, buf) != 0 || !User_equals(&u, &v))
        return 1;

    printf("%s\n", Color_to_string(v.favourite));   // GREEN
    return 42;
}
```

Output:

```text
{"id":7,"name":"ann","favourite":1}
GREEN
```

More attributes (`@serialize`, `@accessors`, `@hash`, `@bitflags`, ...) are in [docs/derive.md](docs/derive.md).

## Features

### Language

| Feature | Details |
|---|---|
| **Compile-time macros** | `@comptime` functions run during compilation: rewrite call sites, generate functions, globals and types ([macros.md](docs/macros.md)) |
| **Derive attributes** | `@serialize`, `@to_json`, `@to_string`, `@equals`, ... generate code for a type ([derive.md](docs/derive.md)) |
| **Attributes** | GNU `__attribute__`, C23 `[[...]]` and the `@name` shorthand ([attributes.md](docs/attributes.md)) |
| **Operators and methods** | `[[cccc::operator(+)]]` and `[[cccc::method(T)]]` bind operators and `s.f(x)` calls to functions on structs, written free or in the struct body ([operators.md](docs/operators.md), [methods.md](docs/methods.md)) |
| **C23 and extensions** | C89 through C23, GNU and MS extensions ([coverage.md](docs/coverage.md)) |
| **Decimal floating point** | `_Decimal32/64/128` via the Intel BID library, opt-in ([install.md](docs/install.md)) |
| **Kernels** | `[[cccc::kernel]]` and OpenCL C 1.2 over a grid of work-items; `-fopenmp` ([kernels.md](docs/kernels.md), [opencl.md](docs/opencl.md), [openmp.md](docs/openmp.md)) |

### Tooling

| Feature | Details |
|---|---|
| **Testing** | `@test` and `Assert*` in the same file as `main()`, TAP output ([testing.md](docs/testing.md)) |
| **Build system** | `--build` scripts written in C, with targets, profiles, cross-compilation and caching ([build.md](docs/build.md)) |
| **Debugger** | GDB-like breakpoints, watchpoints, edit-and-continue, debug-on-crash ([debugger.md](docs/debugger.md)) |
| **REPL** | Declarations persist across the session; expressions print a typed result ([repl.md](docs/repl.md)) |
| **Watch** | `--watch` reloads a program or rebuilds on save ([watch.md](docs/watch.md)) |
| **Config files** | `.cccc.toml` with a trust prompt ([config.md](docs/config.md)) |
| **Warnings** | gcc/clang-style `-W` categories and `-Werror` ([warnings.md](docs/warnings.md)) |
| **URL includes** | `#include <https://...>` and `#embed <https://...>`, opt-in ([headers-native.md](docs/headers-native.md)) |
| **JSON reflection** | `--ffi-decls` dumps declarations for FFI wrapper generators |

### Safety

| Feature | Details |
|---|---|
| **Safety levels** | `-0` to `-3`, from zero overhead to maximum checking ([safety.md](docs/safety.md)) |
| **Detectors** | Use-after-free, overflows, dangling pointers, uninitialized reads, integer overflow, CFI ([safety-memory.md](docs/safety-memory.md), [safety-pointers.md](docs/safety-pointers.md)) |
| **Checked pointers** | Bounds carried by the pointer declaration ([safety-checked-pointers.md](docs/safety-checked-pointers.md)) |
| **Sandboxing** | `--sandbox` and comptime policy for untrusted code ([security.md](docs/security.md), [comptime-policy.md](docs/comptime-policy.md)) |

### Output

| Feature | Details |
|---|---|
| **Native compilation** | `-c=native` hands the compiled program to a host compiler as portable C ([native.md](docs/native.md)) |
| **Source maps** | JSON export for IDE integrations ([debugger-source-map.md](docs/debugger-source-map.md)) |

## Getting started

Install `cccc`, run a C file on the VM, and pick a mode.

### Install

Download a release build from [github.com/takeiteasy/cccc/releases](https://github.com/takeiteasy/cccc/releases), or build from source (needs a C compiler, `make` and libffi):

```sh
git clone --recurse-submodules https://github.com/takeiteasy/cccc
cd cccc && make        # produces ./cccc
```

Supported: macOS (aarch64) and Linux (aarch64, x86_64). See [install.md](docs/install.md) for optional dependencies.

### Run a file

`cccc file.c` compiles to bytecode and runs `main()` on the built-in VM. The process exit code is `main`'s return value.

```c
#include <stdio.h>
int main(void) { puts("hello"); return 0; }
```

```sh
./cccc hello.c        # hello
```

### Four ways to run one file

| Command | What happens |
|---|---|
| `cccc file.c` | Run `main()` on the VM |
| `cccc -c=native -o out file.c` | Lower to C and build a native binary with `cc` ([native.md](docs/native.md)) |
| `cccc --testing file.c` | Run the `@test` functions ([testing.md](docs/testing.md)) |
| `cccc --build file.c` | Run the `@build_target` recipes ([build.md](docs/build.md)) |

All four work on the same file: tests, build recipes and `main()` live side by side. `--run` runs `main()` after a passing `--testing` or `--build`.

### Compile-time code

A function marked `@comptime` is ordinary C that runs *while compiling*. Call sites can be rewritten, and whole declarations generated. See [macros.md](docs/macros.md).

### Safety checks

Runtime checks are on a scale from `-0` (none) to `-3` (maximum):

```sh
./cccc -2 file.c       # use-after-free, overflows, uninitialized reads, ...
```

See [safety.md](docs/safety.md).

### Next

| To... | Read |
|---|---|
| Generate code from a type definition | [derive.md](docs/derive.md) |
| Write tests | [testing.md](docs/testing.md) |
| Replace `make` for your project | [build.md](docs/build.md) |
| Explore interactively | [repl.md](docs/repl.md), [debugger.md](docs/debugger.md) |
| Set defaults per project | [config.md](docs/config.md) |

## Speed

Compiling cccc's own sources takes about 2 minutes with `cccc -c=native` and 19 seconds with a parallel host `cc -O2` build (Apple M1). The VM is roughly 55x to 870x slower than `gcc -O2`; `-c=native` runs at native speed. Details are in [docs/benchmarks.md](docs/benchmarks.md).

## Documentation

Each guide covers one topic. Hub pages (marked *hub*) link to their sub-pages.

**Start here**

| Guide | Covers |
|---|---|
| [getting-started.md](docs/getting-started.md) | Install, first run, and the four ways to run a file |
| [features.md](docs/features.md) | What CCCC does, one line per feature |
| [install.md](docs/install.md) | Building `cccc` from source: dependencies, `make`, `CCCC_HAS_*` options |
| [config.md](docs/config.md) | `.cccc.toml` files, the trust prompt, pinning an approval by hash |

**Compile-time macros**

| Guide | Covers |
|---|---|
| [macros.md](docs/macros.md) | *hub* — `@comptime` functions, the return model, AST building and quoting |
| [derive.md](docs/derive.md) | *hub* — attributes that generate code for a type: `@serialize`, `@to_json`, `@to_string`, ... |
| [comptime-policy.md](docs/comptime-policy.md) | What comptime code may call, and `--comptime-allow` |
| [comptime-limits.md](docs/comptime-limits.md) | Memory confinement and node-list checks for comptime code |
| [security.md](docs/security.md) | What runs or downloads while compiling, `--sandbox`, untrusted code |

**Testing and building**

| Guide | Covers |
|---|---|
| [testing.md](docs/testing.md) | *hub* — `@test`, suites, assertions, hooks, filtering |
| [build.md](docs/build.md) | *hub* — `--build` scripts: targets, profiles, cross-compilation |
| [watch.md](docs/watch.md) | `--watch`: reload a program or rebuild when a file is saved |
| [repl.md](docs/repl.md) | The interactive read-eval-print loop |
| [debugger.md](docs/debugger.md) | *hub* — the source-level debugger, crash handling, the source-map API |

**Runtime safety**

| Guide | Covers |
|---|---|
| [safety.md](docs/safety.md) | *hub* — safety levels `-0`…`-3` and the individual detectors |
| [warnings.md](docs/warnings.md) | `-W` categories, `-Werror`, pragma suppression |
| [threading.md](docs/threading.md) | Threads, the global lock, `<pthread.h>`, `<threads.h>`, atomics |

**Native output and headers**

| Guide | Covers |
|---|---|
| [native.md](docs/native.md) | `-c=native`: lowering to portable C and handing it to a host compiler |
| [headers.md](docs/headers.md) | *hub* — `#include` resolution, system headers, host-header hand-off |
| [inline-asm.md](docs/inline-asm.md) | Inline `asm` in the VM, `--asm-passthru`, native hand-off |

**Parallel code**

| Guide | Covers |
|---|---|
| [kernels.md](docs/kernels.md) | `[[cccc::kernel]]` over a grid of work-items |
| [opencl.md](docs/opencl.md) | Reading OpenCL C 1.2 kernels |
| [openmp.md](docs/openmp.md) | `-fopenmp` directives and `<omp.h>` |

**Reference**

| Guide | Covers |
|---|---|
| [attributes.md](docs/attributes.md) | *hub* — `__attribute__`, `[[...]]` and `@name` support |
| [methods.md](docs/methods.md) | `[[cccc::method(T)]]` struct methods: `s.f(x)` call-site lowering |
| [operators.md](docs/operators.md) | `[[cccc::operator(...)]]` operator overloading for structs |
| [types.md](docs/types.md) | Type compatibility, `_Generic`, `--compiler-family` |
| [coverage.md](docs/coverage.md) | C89–C23 and extension support table |
| [stdlib.md](docs/stdlib.md) | Standard-library and POSIX header support table |
| [benchmarks.md](docs/benchmarks.md) | Compile speed and VM run speed against a host compiler |

**Contributing**

| Guide | Covers |
|---|---|
| [test-infrastructure.md](docs/test-infrastructure.md) | Test layout, suites, loose tests and harnesses |
| [ci.md](docs/ci.md) | Per-architecture workflows and continuous integration |
| [selfhosting.md](docs/selfhosting.md) | Compiling cccc with itself |
| [releasing.md](docs/releasing.md) | Cutting a release: version stamping, tags, release automation |
| [fuzzing.md](docs/fuzzing.md) | AFL++ and libFuzzer harnesses |
| [vm.md](docs/vm.md) | *hub* — VM architecture, encoding, execution model, opcode reference |
| [comptime-builtins.md](docs/comptime-builtins.md) | Declaring and registering comptime builtins |
| [checked-interfaces-impl.md](docs/checked-interfaces-impl.md) | How checked-pointer call checks and bounds casts are implemented |
| [macros-impl.md](docs/macros-impl.md) | Comptime visibility and forwarding rules |
| [safety-impl.md](docs/safety-impl.md) | Bounds propagation, `CHKAB` and dangling-pointer internals |
| [native-impl.md](docs/native-impl.md) | Replayed includes, host-owned layouts, shared runtimes |

## Usage

```text
CCCC: Comprehensive C Compensation Compiler
https://git.sr.ht/~takeiteasy/cccc

Usage: ./cccc [options] file...

Options:
	-h/--help                Show this message
	   --version             Print version, git describe, host triple, and enabled features
	-x <lang>               Read every input as <lang>: cl (OpenCL C) or c; default is by extension (.cl is OpenCL C)
	-I/--include <path>      Add <path> to include search paths
	-i/--isystem <path>      Add <path> to system include paths (for non-standard headers)
	-F/--framework-path <path> Add <path> to framework search paths (<Name/X.h> -> Name.framework/Headers/X.h)
	   --iframework <path>   Add <path> to system framework search paths
	   --use-system-headers  Prefer SDK headers over CCCC polyfills for non-owned standard headers
	   --no-builtin-includes Do not fall back to CCCC's own bundled headers for non-owned standard headers (requires --use-system-headers)
	   --sysroot <path>      Set SDK root; adds <path>/usr/include to system include paths (and <path>/System/Library/Frameworks to system framework paths) and implies --use-system-headers
	-L/--library-path <path> Add <path> to dynamic library search paths
	-l/--library <name>      Link dynamic library by name or path
	   --link-arg <arg>      With -c=native: pass <arg> verbatim to the host cc link line (repeatable; e.g. -framework X, a .o)
	   --url-cache-dir <path> Directory for caching #include/#embed <https://...> fetches
	   --url-cache-clear     Clear the URL fetch cache and exit
	   --allow-url           Let #include/#embed <https://...> download files that are not already cached
	   --url-timeout=SECONDS Set URL fetch timeout in seconds (default: 30)
	   --url-max-size=SIZE   Cap fetched URL payload size (e.g., 50MB, default: 10MB)
	-D/--define <macro>[=def] Define a macro
	-U/--undef <macro>       Undefine a macro
	-a/--ast                 Dump AST
	-p/--print-tokens        Print preprocessed tokens to stdout
	-E/--preprocess          Output preprocessed source code (traditional C -E)
	-m/--dump-expanded       Output macro-expanded source code (for gcc compatibility)
	   --emit-only           With -c=generated: only emit explicitly tagged content ([[cccc::emit]])
	   --attr-target=TARGET  Attribute spelling in generated output: auto, c23, gnu, msvc, strip
	   --compiler-family=FAM  Host family CCCC's front end models where gcc/clang
	                         disagree (__builtin_types_compatible_p, -Wauto-declarator):
	                         gcc (default), clang, auto (probe CCCC_NATIVE_CC)
	   -mavx, -mavx2, -mavx512f  x86_64 only: define __AVX__ et al, raise gcc's C11
	                         _Alignof(vector) cap to 32/64, forward to -c=native's cc
	   --emit-cccc           Preserve CCCC dialect syntax ([[cccc::...]], @-attrs, checked-pointer
	                         qualifiers, cccc-only #includes) in -E/-m/-c=native/-c=generated output
	                         instead of stripping it to portable C. With -c=native, the usual
	                         cc/clang/gcc PATH search is disabled -- CCCC_NATIVE_CC must name a
	                         compiler that understands the dialect explicitly
	   --no-layout-guards    Suppress the _Static_assert layout guards emitted next to
	                         every aggregate definition in -m/-c=generated/-c=native
	                         output (see docs/native.md); on by default
	-j/--json                Emit JSON for all eligible output (diagnostics, header declarations, etc.)
	-J/--ffi-decls           Emit parsed function/struct/enum declarations as JSON (for FFI wrapper generation)
	-X/--no-preprocess       Disable preprocessing step
	-S/--no-stdlib           Do not link standard library
	-c[FMT]/--compile[=FMT]  Compile only; do not execute. FMT: native (default), object, generated
	                         native: build a native executable via CCCC_NATIVE_CC
	                                 (cc, clang, or gcc); writes to -o file, or ./a.out
	                                 if -o omitted
	                         object: like native but stop before linking; writes
	                                 to -o file, or <input>.o (-o required for
	                                 several inputs); -l/-L/--link-arg are rejected
	                         generated: serialize the runtime TU + macro-generated
	                                    objects to C; writes to -o file, or ./a.gen.c
	                                    if -o omitted
	                         Aliases: native=n, object=obj=o, generated=gen=g. Use
	                         -cnative or --compile=native (short form must be
	                         attached; long form may use '=' or separate arg).
	   --deps-file=PATH      With -c=native/-m/-c=generated: write a make-style
	                         dependency rule (`out: sources + resolved #includes`)
	                         to PATH, for a build system to track header deps.
	   --run                 Run main() in the VM (the default); after --testing or
	                         --build, runs only if they succeed.
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
	   --vm-profile          Count executed VM opcodes, comptime included, and print a report
	                         Combine with --json to also dump the profile as JSON to stdout
	                         (stderr under -c=native, -c=generated, -m)

Testing Options:
	-t/--testing[=vm|native]
	                         Discover and run [[cccc::test]] functions. Bare -t/--testing
	                         (default =vm) runs them in-process; =native serializes the
	                         harness itself and runs
	                         it as a standalone binary via CCCC_NATIVE_CC (implies -c=native;
	                         [[cccc::test_setup/teardown]] hooks and negative tests are not
	                         supported under =native, see docs/testing.md)
	   --test=GLOB           Run only tests whose name matches GLOB (implies --testing)
	   --test-suite=NAME     Run tests in NAME and its sub-suites (prefix match);
	                         glob metacharacters (*?[) switch to fnmatch (implies --testing)
	   --list-tests          List test names without running (implies --testing)
	   --fail-fast           Stop after the first failing test
	   --test-timeout=N      Per-test timeout in seconds (0 = no timeout;
	                         individual tests may override via
	                         [[cccc::test(timeout = ms)]])
	   --test-format=FMT     Output format for test results: tap (default), plain, json
	   --no-emit-tests       -c=native/-c=generated: drop [[cccc::test]] bodies from the
	                         output instead of emitting them inert

Kernel Options:
	   --kernel-max-group-size=N
	                         Largest work-group cccc_launch accepts (default 256)

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

Watch Options:
	--watch              Hot-reload the program when a source file is saved; with --build,
	                    rebuild instead (implied by [[cccc::build]] in the file);
	                    with --repl, reload the files :load read
	--watch-path=PATH    Also watch a file or directory (repeatable)
	--watch-ignore=GLOB  Ignore matching paths (repeatable)
	--watch-debounce=MS  Quiet time that ends a burst of saves (default 100)
	--watch-clear        Clear the terminal before each restart or rebuild
	--watch-restart      Restart the program on a change instead of reloading it

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
	   --disable-ffi         Block all registered and dynamic native calls
	   --comptime-allow=list  Grant comptime code capability classes beyond the safe default:
	                         fs, exec, net, env, dlopen, thread, signal, ipc, all
	   --sandbox              Confine this process with the OS sandbox: no network, writes only to output
	                         and temp directories, reads only inputs, include/library paths and the toolchain,
	                         exec only of cccc and the C toolchain
	   --sandbox-read <path>  Let --sandbox read <path> too (repeatable)
	   --sandbox-exec <path>  Let --sandbox run <path> too (repeatable)
	   --ffi-errors-fatal     Abort execution on FFI policy violations
	   --ffi-type-checking    Validate registered FFI call arity at runtime

Configuration (see docs/config.md):
	   --no-config            Ignore ~/.config/cccc/config.toml and .cccc.toml files
	   --show-config          Print the config files read and the options they add
	   --trust[=path]         Approve a project .cccc.toml's capability grants
	   --untrust[=path]       Withdraw that approval
	   --trust-hash[=on|off]  With --trust: pin the file by its SHA-256

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
	-fopenmp, --openmp           Honour #pragma omp (one thread in the VM, real threads with -c=native)
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
	./cccc -o hello hello.c
	./cccc -I ./include -D DEBUG -o prog prog.c
	echo 'int main() { return 42; }' | ./cccc -
```

## Credits

Third-party license notices: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

This project builds on [chibicc](https://github.com/rui314/chibicc) for the C frontend and on ideas from [c4](https://github.com/rswier/c4) / [write-a-C-interpreter](https://github.com/lotabout/write-a-C-interpreter) for the VM-oriented execution model (You can run [test_c4](tests/test_c4.c) inside cccc by running `./cccc tests/test_c4.c -- tests/test_fortytwo.c`).

[stb_sprintf.h](https://github.com/nothings/stb/blob/master/stb_sprintf.h) is used as a base for C23 compliant `*printf` functions.

`--watch` uses [dmon](https://github.com/septag/dmon) by Sepehr Taghdisian (BSD 2-clause), vendored as `src/dmon.h`.

Config files are parsed with [tomlc17](https://github.com/cktan/tomlc17) by CK Tan (MIT), vendored as `src/tomlc17.c`.

Pinning a trusted config by hash uses the SHA-256 from [libscrypt](https://github.com/technion/libscrypt) by Colin Percival (BSD 2-clause), reduced to the digest in `src/sha256.c`.

Host C crash backtraces use [libbacktrace](https://github.com/ianlancetaylor/libbacktrace) by Ian Lance Taylor (BSD 3-clause), a git submodule in `vendor/libbacktrace`.

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
