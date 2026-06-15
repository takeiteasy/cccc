# CCCC Build System

> **Status:** Design draft — not implemented yet. Open questions are flagged with
> **[Q:]**. This document is intended to be reviewed before any code is written.

A Zig/Rust-style build system embedded in C. The build script is a normal
`.c` source file that CCCC compiles and runs at build time; the script
declares targets (executables, static libraries, dynamic libraries) and the
CCCC build runner compiles and links them.

The design reuses the `[[cccc::comptime]]` machinery wherever possible: the
build script compiles and runs in the CCCC VM the same way compile-time
macros do, and target factories can themselves be `[[cccc::comptime]]`
helpers.

## Goals

- **Self-contained builds** — one `cccc` binary, no Make, no `build.zig`, no
  `Cargo.toml`. The build script is C the same way the rest of the project
  is C.
- **Familiar shape** — a designated `build_main` (or `[[cccc::build]]`-
  tagged) function, target factories decorated with `[[cccc::build_target]]`,
  a builder API for sources / flags / dependencies, and a top-level "run"
  call. Mirrors `zig build` and Cargo's `build.rs` ergonomically without
  dragging in their DSLs.
- **Sandboxed by default** — the build function runs in the CCCC VM, so
  `cc`/`ar`/`ld` can only be invoked through registered FFI entries. The
  toolchain allow-list is part of the FFI policy (`--ffi-allow=cc,ar,ld`).
- **Native-only targets** — every target is a native executable or
  library handed off to the system toolchain (`cc` / `clang` / `gcc`).
  Bytecode / VM targets are out of scope for the build system —
  they're the testing framework's domain
  (see [TESTING_FRAMEWORK.md](TESTING_FRAMEWORK.md)).
- **CCCC options live in source** — VM-specific options (optimisation
  level, safety levels, debug, profiling) are declared from inside C
  source via `#pragma cccc ...`, not via build-system API calls. This
  keeps the build system toolchain-agnostic and makes source files
  self-describing for VM execution. See
  [CCCC Pragma Extensions](#cccc-pragma-extensions).
- **Compatible with the existing toolchain** — the build script is still
  C. The same file can be compiled with the regular `cccc` modes (with the
  build entry elided) for development; `--build` just runs the build
  function instead of `main()`.

## Non-Goals (v1)

- **Incremental builds.** The runner always rebuilds from scratch. Cache
  invalidation and content-addressable storage are deferred.
- **Cross-compilation.** The build script always uses the host toolchain
  found by `CCCC_NATIVE_CC` (or `cc` / `clang` / `gcc`). Cross targets
  would need a triaged toolchain model.
- **Remote / distributed builds.** Local only.
- **Build profiles, release/debug modes.** Targets take flags
  directly; the build system has no separate "profile" concept in
  v1. A future release/debug profile layer would be added as a
  runner enhancement on top of the existing API.
- **Bytecode targets.** The build system produces only native output;
  bytecode targets are the testing framework's domain. They are not
  supported in v1.

## CLI

```
./cccc --build build.c                     # run build_main in build.c
./cccc --build build.c --build-target=foo  # build only target `foo`
./cccc --build build.c -j8                 # 8 parallel jobs
./cccc --build build.c -O build/            # output directory
```

| Flag | Default | Meaning |
|------|---------|---------|
| `--build` | (off) | Switch to build mode. The input file is treated as a build script; `main()` is not required. |
| `--build-entry=NAME` | `build_main` | Symbol to invoke as the build entry. |
| `--build-target=NAME` | (all) | Build only the named target. |
| `-j` / `--jobs=N` | nproc | Parallel compile / link jobs. |
| `-O` / `--build-out-dir=PATH` | `build/` | Output directory. |
| `--build-keep-going` | off | Continue building other targets after a failure. |
| `--build-quiet` | off | Suppress per-target command lines. |
| `--build-verbose` | off | Print full commands including dep file resolution. |

Existing flags are forwarded to the per-target compile:

- `-I`, `-i`, `-D`, `-U` apply to every target the build script
  does not explicitly override.
- `--std=` is forwarded to the underlying compiler.
- `-l`, `-L`, `--library`, `--library-path` are forwarded as link flags.
- VM-only options (`-0`/`-1`/`-2`/`-3`, `--optimize[=N]`,
  `--vm-profile`, `--debug`, the bytecode-output modes) are
  **rejected** in `--build` mode. The build script never needs
  them, and the build runner does not run VM code itself. Sources
  that need these options declare them via `#pragma cccc ...` (see
  [CCCC Pragma Extensions](#cccc-pragma-extensions)), and the testing
  framework is the right tool to exercise VM-mode behaviour.

## CCCC Pragma Extensions

CCCC-specific options (VM optimisation, safety levels, debug, profiling,
etc.) are declared from **inside C source** with `#pragma cccc ...`.
The pragma is the source-level counterpart to the CLI flag it mirrors;
when CCCC compiles a file, the active pragmas produce the same effect
as the corresponding CLI flag.

```c
// Enable a CCCC-specific option for the rest of this file
#pragma cccc optimise(2)
#pragma cccc safety(1)

// Disable / clear
#pragma cccc optimise(0)
```

The pragmas are not relevant to `--build` mode (the build system
handles only native output). They are the testing framework's
mechanism for source-driven VM configuration — see
[TESTING_FRAMEWORK.md](TESTING_FRAMEWORK.md) for how the runner
collects pragmas from test files and applies them when running
tests in the VM.

### Pragmas

| Pragma | Mirrors | Meaning |
|--------|---------|---------|
| `#pragma cccc optimise(N)` | `--optimize=N` | Bytecode optimisation level (0..3). |
| `#pragma cccc safety(N)` | `-N` | Safety level preset (0..3). |
| `#pragma cccc debug` | `-g` | Enable interactive debugger. |
| `#pragma cccc vm_profile` | `-Y` | Enable VM opcode profiling. |
| `#pragma cccc vm_heap` | `-V` | Route `malloc`/`free` through the VM heap. |
| `#pragma cccc bounds_checks` | `-b` | Array bounds checking. |
| `#pragma cccc uaf_detection` | `-f` | Use-after-free detection. |
| `#pragma cccc type_checks` | `-t` | Runtime type checks on pointer derefs. |
| `#pragma cccc uninitialized_detection` | `-z` | Uninitialized variable detection. |
| `#pragma cccc overflow_checks` | `--overflow-checks` | Signed integer overflow. |
| `#pragma cccc stack_canaries` | `-s` | Stack overflow protection. |
| `#pragma cccc heap_canaries` | `-k` | Heap overflow protection. |
| `#pragma cccc memory_leak_detection` | `-m` | Track allocations and report leaks at exit. |
| `#pragma cccc stack_instrumentation` | `-i` | Track stack variable lifetimes. |
| `#pragma cccc pointer_sanitizer` | `-p` | All pointer checks (bounds, UAF, type). |
| `#pragma cccc memory_tagging` | `-T` | Temporal memory tagging. |
| `#pragma cccc reset` | — | Clear all CCCC options set by previous pragmas. |

### Rules

- Pragmas are file-scope only in v1. A pragma applies to the rest
  of the file from its point of occurrence; later pragmas
  supersede earlier ones. **[Q: do we want `#pragma cccc push` /
  `#pragma cccc pop` for block-scope overrides? My recommendation:
  defer to v2 — none of the existing tests need block-scope
  overrides.]**
- CLI flags override pragmas. A test that wants safety level 2
  declares `#pragma cccc safety(2)`, but `./cccc --testing -3
  tests.c` still wins. This is the right precedence — the
  developer / CI run's flag is the authority.
- Unknown pragma arguments (e.g. `#pragma cccc optimise(99)`) are a
  hard error, not a warning. Spelling typos on pragma names
  (`#pragma cccc optimse(2)`) are a hard error too.
- A file that uses `#pragma cccc ...` compiles to native (via
  `-c=native`) or to bytecode (via the default mode) without
  needing the corresponding CLI flag. The pragmas are simply
  ignored on the native path.

### Why a pragma, not a build-system API call

- **Self-describing source.** A file that needs `-2` to be
  meaningful declares that fact inline. The test framework can run
  it correctly without an out-of-band config.
- **Toolchain-agnostic builds.** The build system forwards
  `-O2` / `-g` to `cc`; it has no business knowing about CCCC's
  `-2` safety preset. A pragma keeps the build system out of the
  CCCC-specific-options loop.
- **No DSL.** Both the build system and the test framework are C.
  Adding a new option is a one-line parser change, not a build
  script API change.

## Build Entry

The build entry is the function the build runner invokes. It is identified
in three ways, in order of precedence:

1. **Explicit flag:** `--build-entry=name` overrides everything.
2. **Attribute:** A function tagged with `[[cccc::build]]` (or
   `__attribute__((build))`). If exactly one such function exists, it is
   the entry. If more than one exists, the runner reports an ambiguity
   and exits with a diagnostic pointing at all candidates.
3. **Default name:** A function named `build_main` with signature
   `void build_main(cccc_build_ctx_t *)` (or `int` return).

GNU attribute syntax is accepted everywhere alongside the C23 form:

```c
[[cccc::build]]                                       // C23
__attribute__((build))                               // GNU
__attribute__((cccc::build))                          // GNU namespace form
```

The build entry is intercepted by the preprocessor the same way
`[[cccc::comptime]]` is — it never reaches the general attribute parser.

### Signature

```c
int build_main(cccc_build_ctx_t *ctx);
// or
void build_main(cccc_build_ctx_t *ctx);
```

Return value:

- `void` — the runner reports success as long as all targets compiled
  and linked without error.
- `int` — non-zero is treated as a build failure; the runner stops,
  prints the value, and exits with the same code (subject to
  `--build-keep-going`).

`build_main` is allowed to have additional parameters if they have
default arguments. **[Q: do we want the entry to be able to take config
values, e.g. `int build_main(cccc_build_ctx_t *, int debug, int release)`?
My instinct is no — config should come from CLI flags and env vars, not
the entry signature. We can revisit if there's a use case.]**

## Target Factories

Functions that build and return a `cccc_target_t *` are decorated with
`[[cccc::build_target]]`. The attribute tells the parser and reflection
tools that the function is a target factory — it returns a configured
target and may have side effects on the build context.

```c
[[cccc::build_target]]
cccc_target_t *make_foo(cccc_build_ctx_t *ctx) {
    cccc_target_t *t = cccc_executable(ctx, "foo");
    cccc_target_set_output(t, "bin/foo");
    cccc_target_add_source(t, "src/main.c");
    cccc_target_add_source(t, "src/util.c");
    cccc_target_add_include(t, "include");
    cccc_target_add_define(t, "DEBUG", "1");
    cccc_target_add_cflag(t, "-O2");
    return t;
}
```

Factories are normal C functions, callable from the build entry. They
can also be `[[cccc::comptime]]` if the target configuration is
constant-foldable — the runner doesn't care, it just calls them at
build time.

Reflection output (`--ffi-decls --json`) lists every
`[[cccc::build_target]]`-tagged function as a build-target descriptor
alongside ordinary function declarations.

## Target Kinds

Three kinds. All native — see [Native Backends](#native-backends) for
why the build system produces only native output.

| Kind | Function | Default output | Backend |
|------|----------|----------------|---------|
| Executable | `cccc_executable(ctx, name)` | `bin/<name>` | system toolchain (`cc` / `clang` / `gcc`) |
| Static library | `cccc_static_lib(ctx, name)` | `lib<name>.a` | `ar` (or `cc -static`) |
| Dynamic library | `cccc_dynamic_lib(ctx, name)` | `lib<name>.{so,dylib}` | `cc -shared` |

### Native Backends

The build system is for native output only. v1's backend is the
system toolchain (`CCCC_NATIVE_CC` or `cc` / `clang` / `gcc`); see
[README.md](../README.md) for the production native path. Adding a
new backend is a new `cccc_backend_t` registration against
`cccc_target_t`; the build entry does not change.

**Bytecode / VM targets are out of scope for the build system.**
A `.c4` file is just `-c=bytecode -o file.c4 source.c`; you
don't need a build system for it. Tests that need VM execution —
VM-only features, safety levels, debugging, profiling — belong in
the testing framework (see [TESTING_FRAMEWORK.md](TESTING_FRAMEWORK.md)),
where a test's `#pragma cccc ...` directives configure the VM
inline.

## Builder API

All APIs are exposed via a private `cccc_build.h` header that the runner
auto-injects into the build script the same way `reflection.h` is
auto-injected into `[[cccc::comptime]]` code. The header is **not** on
the public include path.

```c
// Build context
const char *cccc_build_root(cccc_build_ctx_t *ctx);
const char *cccc_build_out_dir(cccc_build_ctx_t *ctx);
int         cccc_build_jobs(cccc_build_ctx_t *ctx);
int         cccc_build_verbose(cccc_build_ctx_t *ctx);

// Target factories — all return a target owned by `ctx`
cccc_target_t *cccc_executable(cccc_build_ctx_t *ctx, const char *name);
cccc_target_t *cccc_static_lib(cccc_build_ctx_t *ctx, const char *name);
cccc_target_t *cccc_dynamic_lib(cccc_build_ctx_t *ctx, const char *name);

// Output
void cccc_target_set_output(cccc_target_t *t, const char *path);

// Sources
void cccc_target_add_source(cccc_target_t *t, const char *path);
void cccc_target_add_sources_glob(cccc_build_ctx_t *ctx, cccc_target_t *t,
                                 const char *pattern);
void cccc_target_add_source_str(cccc_target_t *t, const char *name,
                               const char *source);  // for #embed-style uses

// Flags
void cccc_target_add_include(cccc_target_t *t, const char *path);
void cccc_target_add_define(cccc_target_t *t, const char *name, const char *value);
void cccc_target_add_undef(cccc_target_t *t, const char *name);
void cccc_target_add_cflag(cccc_target_t *t, const char *flag);
void cccc_target_add_ldflag(cccc_target_t *t, const char *flag);

// Dependencies
void cccc_target_link_with(cccc_target_t *t, cccc_target_t *dep);
void cccc_target_add_lib(cccc_target_t *t, const char *name);   // -l
void cccc_target_add_libpath(cccc_target_t *t, const char *path); // -L

// Run
int cccc_build_run(cccc_build_ctx_t *ctx, cccc_target_t *t);
int cccc_build_run_all(cccc_build_ctx_t *ctx);     // topological order
int cccc_build_run_default(cccc_build_ctx_t *ctx); // alias for run_all + summary
```

Glob expansion uses POSIX `glob(3)` (already on the CCCC standard-library
list) so `*.c` / `**/*.c` patterns work consistently with the rest of
the toolchain.

## Execution Model

```
            ./cccc --build build.c
                       │
                       ▼
   ┌─────────────────────────────────────────┐
   │  1. Preprocess build.c                  │
   │  2. Parse, register comptime helpers    │
   │  3. Find build entry (attr/name/flag)   │
   │  4. Compile entry into the CCCC VM       │
   │  5. Invoke entry inside the VM          │
   │     ├─ entry calls factories,           │
   │     │   registers cccc_target_t *s       │
   │     ├─ entry calls cccc_build_run_*(ctx) │
   │     │   which:                          │
   │     │   - topo-sorts target DAG         │
   │     │   - for each target, computes     │
   │     │     command lines, runs them as   │
   │     │     FFI calls to cc/ar/ld         │
   │     │   - parallelises with -j          │
   │  6. Exit with the entry's return value  │
   │     (or 0 on success)                   │
   └─────────────────────────────────────────┘
```

The build entry is a regular C function compiled into the CCCC VM by
exactly the same path that compiles `[[cccc::comptime]]` functions. The
build runner just changes *which* function is treated as the entry —
instead of `main()` in step 5, it's `build_main` (or whatever
`--build-entry` named).

The runner itself is provided to the entry as a set of FFI wrappers
over `cc`, `ar`, and `ld`. The FFI allow-list controls which of these
the entry can call:

```
./cccc --build build.c --ffi-allow=cc,ar,ld
```

A build entry that does not call any toolchain FFI can run with the
FFI disabled entirely — useful for "list targets" or "dump the build
graph" subcommands implemented inside the build function.

## Full Example

```
.
├── build.c
├── include/
│   └── greet.h
└── src/
    ├── main.c
    ├── greet.c
    └── lib/
        ├── sum.c
        └── sum.h
```

`include/greet.h`:

```c
#ifndef GREET_H
#define GREET_H
void greet(const char *name);
int  sum(int a, int b);
#endif
```

`src/lib/sum.c`:

```c
#include "greet.h"
int sum(int a, int b) { return a + b; }
```

`src/greet.c`:

```c
#include <stdio.h>
#include "greet.h"
void greet(const char *name) { printf("hello, %s!\n", name); }
```

`src/main.c`:

```c
#include <stdio.h>
#include "greet.h"

int main(int argc, char **argv) {
    greet(argc > 1 ? argv[1] : "world");
    return sum(20, 22) == 42 ? 42 : 1;
}
```

`build.c`:

```c
@include <glob.h>            // POSIX glob, only visible to the build entry

[[cccc::build_target]]
cccc_target_t *libcore(void) {
    cccc_target_t *lib = cccc_static_lib(/*ctx=*/(void*)0, "core");
    cccc_target_set_output(lib, "lib/libcore.a");
    cccc_target_add_source(lib, "src/lib/sum.c");
    cccc_target_add_include(lib, "include");
    return lib;
}

[[cccc::build]]
int build_main(cccc_build_ctx_t *ctx) {
    cccc_target_t *core = cccc_static_lib(ctx, "core");
    cccc_target_set_output(core, "lib/libcore.a");
    cccc_target_add_source(core, "src/lib/sum.c");
    cccc_target_add_include(core, "include");

    cccc_target_t *greet = cccc_static_lib(ctx, "greet");
    cccc_target_set_output(greet, "lib/libgreet.a");
    cccc_target_add_source(greet, "src/greet.c");
    cccc_target_add_include(greet, "include");

    cccc_target_t *foo = cccc_executable(ctx, "foo");
    cccc_target_set_output(foo, "bin/foo");
    cccc_target_add_source(foo, "src/main.c");
    cccc_target_add_include(foo, "include");
    cccc_target_link_with(foo, core);
    cccc_target_link_with(foo, greet);
    cccc_target_add_define(foo, "GREET_DEFAULT", "world");

    return cccc_build_run_default(ctx);
}
```

Run:

```sh
$ ./cccc --build build.c --ffi-allow=cc,ar,ld
[1/4] cc -c -Iinclude src/lib/sum.c -o build/obj/core/sum.o
[2/4] ar rcs build/lib/libcore.a build/obj/core/sum.o
[3/4] cc -c -Iinclude src/greet.c -o build/obj/greet/greet.o
[3/4] ar rcs build/lib/libgreet.a build/obj/greet/greet.o
[4/4] cc -o build/bin/foo build/obj/foo/main.o -Lbuild/lib -lcore -lgreet
build succeeded (3 targets, 0 errors)
```

## Constraints

- The build script is C and is compiled with the same parser / preprocessor
  / VM as every other CCCC source file. **No DSL, no TOML/YAML, no
  separate interpreter.**
- The build entry runs in the CCCC VM, so the CCCC-supported C surface is
  what the script can use (see [COVERAGE.md](COVERAGE.md)). It can
  call toolchain binaries only through FFI, which means the FFI allow-
  list is the only way to grant toolchain access.
- The build script does not have access to arbitrary process state.
  `getenv()` works for variables the runner has not blocked.
- Target factories are not allowed to call `cccc_build_run_*` themselves —
  only the build entry is. Calling a runner function from a factory is a
  compile-time error. **[Q: should this be a warning, an error, or a
  silent no-op? My recommendation: error, because mid-factory side
  effects break the "all targets registered, then run" model.]**
- `--build` is mutually exclusive with `--vm-profile`, `--disassemble`,
  `--debug`, and the bytecode-output modes. The build script never
  needs them. Mixing them is a CLI error.
- The build script must not define `int main(void)`. The parser
  rejects a `main` definition in `--build` mode. **[Q: do we want to
  allow `int main(void)` for "I just want to compile this build script
  as a normal program for testing"? My recommendation: yes, but only
  when `--build` is not set.]**

## Makefile Replacement

The eventual `build.c` for the CCCC project replaces the existing
`Makefile`. The targets map roughly as:

| Make target | `build.c` equivalent |
|-------------|----------------------|
| `make` / `make cccc` | `cccc` executable target, run by default |
| `make libcccc.dylib` / `libcccc.so` | `libcccc` shared library target (`cccc_dynamic_lib`) |
| `make cccc-asan` | `cccc-asan` executable target with sanitizer cflags |
| `make cccc-ubsan` | `cccc-ubsan` ... |
| `make cccc-tsan` | `cccc-tsan` ... |
| `make cccc-msan` | `cccc-msan` ... (Linux only) |
| `make cccc-afl` | `cccc-afl` ... (different `cc`) |
| `make cccc-afl-asan` | `cccc-afl-asan` ... |
| `make fuzz_harness` | `fuzz_harness` libFuzzer target |
| `make test` | Depends on `cccc`; runs `./cccc --testing tests.c` (see [TESTING_FRAMEWORK.md](TESTING_FRAMEWORK.md)) |
| `make clean` | Runner subcommand (e.g. `./cccc --build --clean`) or a `clean` target that `rm -rf build/` |
| `make docs` | `docs` target, runs `headerdoc2html` via FFI |
| `make stdlib` | `stdlib` target, runs `./cccc -G ...` |
| `make bench` | `bench` target, runs `hyperfine` via FFI |
| `make profile-cpu` | `profile-cpu` target, runs a gperftools-instrumented binary |
| `make profile-mem` | `profile-mem` target, runs `leaks` / `valgrind` |
| `make fuzz-*` | `fuzz` target with sub-actions, or separate targets |

The build script is `build.c`:

```c
[[cccc::build_target]]
cccc_target_t *cccc(void) {
    cccc_target_t *t = cccc_executable(ctx, "cccc");
    cccc_target_add_sources_glob(ctx, t, "src/*.c");
    cccc_target_add_sources_glob(ctx, t, "src/stdlib/*.c");
    cccc_target_exclude_source(t, "src/ops.c");
    cccc_target_add_include(t, "include");
    cccc_target_add_cflag(t, "-std=c23");
    cccc_target_add_cflag(t, "-Wall");
    cccc_target_add_cflag(t, "-O0");
    cccc_target_add_cflag(t, "-g");
    cccc_target_add_ldflag(t, "-lffi");
    return t;
}

[[cccc::build_target]]
cccc_target_t *cccc_asan(void) {
    cccc_target_t *t = cccc();  // clone the base target
    cccc_target_set_name(t, "cccc-asan");
    cccc_target_add_cflag(t, "-fsanitize=address,undefined");
    return t;
}

// ... ubsan, tsan, msan, afl, afl-asan, fuzz_harness ...

[[cccc::build_target]]
cccc_target_t *cccc_tests(void) {
    cccc_target_t *t = cccc_executable(ctx, "cccc-tests");
    cccc_target_add_include(t, "include");
    cccc_target_add_source(t, "tests.c");
    cccc_target_add_cflag(t, "-DCCCC_TEST_ENTRY=1");
    return t;
}

[[cccc::build]]
int build_main(cccc_build_ctx_t *ctx) {
    cccc_target_t *cccc_t = cccc();
    cccc_target_t *asan_t = cccc_asan();
    // ...ubsan, tsan, msan, afl, afl-asan, fuzz_harness ...
    cccc_target_t *tests_t = cccc_tests();

    if (cccc_build_run_default(ctx) != 0)
        return 1;

    // Run the test suite as a build step
    return cccc_build_run_custom(ctx, tests_t,
                                "./%s --testing tests.c",
                                tests_t->output);
}
```

### Toolchain selection

The Makefile currently detects libffi via `pkg-config`. The build
script does the same with a small block at the top:

```c
[[cccc::comptime]]
int has_libffi(void) {
    // returns 1 if pkg-config finds libffi, else 0
    // implemented via $cccc_pkgconfig_check("libffi") (future)
    return 1;
}

[[cccc::build_target]]
cccc_target_t *cccc(void) {
    cccc_target_t *t = cccc_executable(ctx, "cccc");
    if (has_libffi()) {
        cccc_target_add_cflag(t, $(pkg-config --cflags libffi));
        cccc_target_add_ldflag(t, $(pkg-config --libs libffi));
    }
    // ...
    return t;
}
```

For the v1 migration, the toolchain detection is hard-coded
(Homebrew paths on macOS, `/usr/lib` on Linux) and then
generalised as the build system matures. **[Q: do we want a
`cccc_probe_toolchain()` helper that returns the resolved `cc`,
`ar`, `ld` paths and their cflags, or should the build script do
its own detection? My recommendation: a helper, so all projects
benefit from the same detection logic.]**

### Platform branches

The Makefile's `ifeq ($(UNAME_S),Darwin)` branches become normal
C `if` statements in `build.c`:

```c
const char *host = cccc_build_host(ctx);  // "darwin", "linux", "windows"
if (strcmp(host, "darwin") == 0) {
    cccc_target_add_cflag(t, "-I/opt/homebrew/opt/libffi/include");
} else if (strcmp(host, "linux") == 0) {
    cccc_target_add_cflag(t, "-I/usr/include");
}
```

### Chicken-and-egg

The build system itself must be built before it can build
anything else. The migration is therefore gradual:

1. Implement the build system (`[[cccc::build]]`,
   `[[cccc::build_target]]`, `cccc_executable`, etc.) — this doc.
2. Implement the testing framework — see
   [TESTING_FRAMEWORK.md](TESTING_FRAMEWORK.md). It is
   independent of the build system and can ship first.
3. Hand-write `build.c` for CCCC. The first `build.c` is compiled
   by the existing `cccc` binary (the one built by the Makefile);
   the Makefile is kept around for one release as a fallback.
4. Land `build.c` as the canonical build description. Add
   `make build` as a thin shim that runs `./cccc --build
   build.c` (in case anyone is wedded to the Make CLI).
5. Once `build.c` is stable, delete the Makefile. The build
   instruction becomes `./cccc --build build.c`.

The test framework and the build system can ship in either order;
they don't depend on each other. The Makefile conversion is the
*last* step, not the first.

## Interaction With Existing Modes

| Mode | What happens to the build script |
|------|----------------------------------|
| `./cccc build.c` (default) | Compiles and runs the script as a normal C program. `[[cccc::build]]` and `[[cccc::build_target]]` are silently consumed; the build entry is not called. Useful for syntax-checking. |
| `./cccc -c=bytecode -o build.c.c4 build.c` | Emits a `.c4` containing every definition in the file except the build entry. Loading the `.c4` and calling `main()` works as above. |
| `./cccc -c=native -o build.c.o build.c` | Hands the file (build entry elided) to `cc` for a normal native build. |
| `./cccc --build build.c` | New behaviour. Calls the build entry in the VM; the rest of the file is compiled normally. |

The build script therefore lives in three worlds at once: a
runtime-checkable C program, a bytecode module, and a build
orchestrator. The build entry is the only thing that is *only* meaningful
in the third world.

## Open Questions

The **[Q:]** markers in this doc collect the design questions that
still need a decision. The short list:

1. Should the build entry be able to take config parameters? (My
   recommendation: no.)
2. ~~Should `[[cccc::build_target(kind)]]` accept a kind argument?~~
   **Resolved:** removed. The build system is native-only; bytecode
   targets are the testing framework's domain.
3. Should calling `cccc_build_run_*` from a factory be an error? (My
   recommendation: error.)
4. Should a `main()` definition in a build script be allowed in
   non-`--build` modes? (My recommendation: yes, only in non-`--build`
   modes.)
5. Do we want `--build-graph` / `--build-dry-run` to print the
   resolved command lines without executing? (My recommendation: yes,
   it's almost free since the runner already topo-sorts.)
6. Do we want a `cccc_probe_toolchain()` helper for libffi
   detection, or should each build script roll its own? (My
   recommendation: helper, so all projects share the same detection.)
7. Should `#pragma cccc push` / `#pragma cccc pop` for block-scope
   CCCC-option overrides ship in v1 or v2? (My recommendation: v2 —
   none of the existing tests need it.)

## Implementation Sketch (for the eventual ticket)

Roughly in order of dependency:

1. **`[[cccc::build]]` / `[[cccc::build_target]]` attribute parsing** —
   same machinery as `[[cccc::comptime]]`. The preprocessor intercepts
   them and tags the parsed declaration in the symbol table.
2. **`cccc_build.h` private header** — typedefs and prototypes for
   `cccc_build_ctx_t`, `cccc_target_t`, and the builder API. Auto-injected
   into build scripts the same way `reflection.h` is.
3. **`cccc_target_t` storage on `CCCC *`** — opaque struct, owned by the
   VM, with helpers for adding sources, flags, dependencies.
4. **FFI registrations for `cc`, `ar`, `ld`** — three new
   `ForeignFunc` entries registered by the runner before invoking the
   build entry. Gated by `--ffi-allow`.
5. **`--build` mode in `src/main.c`** — new branch next to the
   `compile_format == COMPILE_NATIVE` block. Locates the build entry,
   compiles it into the VM, invokes it, propagates its return value to
   `exit()`.
6. **Topological sort and parallel runner** — small library, single
   header. Reads the target DAG, walks it breadth-first, spawns `-j`
   workers via `posix_spawn` / `fork+exec`. **[Q: do we want this in
   the VM (parallel work scheduled by the build entry) or in the
   host (runner code outside the VM)? My recommendation: in the host,
   so the entry stays simple and the runner can stream progress.]**
7. **Examples + tests** — a `tests/test_build_*.c` matrix mirroring
   the existing `tests/test_macros_*.c` set. A runnable example
   project in `examples/build_demo/`.
8. **Doc updates** — `README.md` adds a "Build" section. `AGENTS.md`
   reference table gets a row for this doc.

## See Also

- [TESTING_FRAMEWORK.md](TESTING_FRAMEWORK.md) — the built-in test
  framework. Where VM-execution tests live (the build system does
  not produce bytecode targets). Also covers the migration plan for
  the existing `tools/tests.py` test suite.
- [MACROS.md](../docs/MACROS.md) — the `[[cccc::comptime]]` system
  the build runner reuses for entry compilation and execution.
- [COVERAGE.md](../docs/COVERAGE.md) — what C surface area a build
  script has access to.
- [SAFETY.md](../docs/SAFETY.md) — the safety levels (`-0` ... `-3`)
  that `#pragma cccc safety(N)` activates.
