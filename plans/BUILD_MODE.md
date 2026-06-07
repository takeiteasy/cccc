# JCC Build System

> **Status:** Design draft — not implemented yet. Open questions are flagged with
> **[Q:]**. This document is intended to be reviewed before any code is written.

A Zig/Rust-style build system embedded in C. The build script is a normal
`.c` source file that JCC compiles and runs at build time; the script
declares targets (executables, static libraries, dynamic libraries) and the
JCC build runner compiles and links them.

The design reuses the `[[jcc::comptime]]` machinery wherever possible: the
build script compiles and runs in the JCC VM the same way compile-time
macros do, and target factories can themselves be `[[jcc::comptime]]`
helpers.

## Goals

- **Self-contained builds** — one `jcc` binary, no Make, no `build.zig`, no
  `Cargo.toml`. The build script is C the same way the rest of the project
  is C.
- **Familiar shape** — a designated `build_main` (or `[[jcc::build]]`-
  tagged) function, target factories decorated with `[[jcc::build_target]]`,
  a builder API for sources / flags / dependencies, and a top-level "run"
  call. Mirrors `zig build` and Cargo's `build.rs` ergonomically without
  dragging in their DSLs.
- **Sandboxed by default** — the build function runs in the JCC VM, so
  `cc`/`ar`/`ld` can only be invoked through registered FFI entries. The
  toolchain allow-list is part of the FFI policy (`--ffi-allow=cc,ar,ld`).
- **Native-only targets** — every target is a native executable or
  library handed off to the system toolchain (`cc` / `clang` / `gcc`),
  or, in a future version, to the planned bytecode-to-LLVM IR backend
  (see [LLVM.md](../docs/LLVM.md)). Bytecode / VM targets are out of
  scope for the build system — they're the testing framework's domain
  (see [TESTING_FRAMEWORK.md](TESTING_FRAMEWORK.md)).
- **JCC options live in source** — VM-specific options (optimisation
  level, safety levels, debug, profiling) are declared from inside C
  source via `#pragma jcc ...`, not via build-system API calls. This
  keeps the build system toolchain-agnostic and makes source files
  self-describing for VM execution. See
  [JCC Pragma Extensions](#jcc-pragma-extensions).
- **Compatible with the existing toolchain** — the build script is still
  C. The same file can be compiled with the regular `jcc` modes (with the
  build entry elided) for development; `--build` just runs the build
  function instead of `main()`.

## Non-Goals (v1)

- **Incremental builds.** The runner always rebuilds from scratch. Cache
  invalidation and content-addressable storage are deferred.
- **Cross-compilation.** The build script always uses the host toolchain
  found by `JCC_NATIVE_CC` (or `cc` / `clang` / `gcc`). Cross targets
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
./jcc --build build.c                     # run build_main in build.c
./jcc --build build.c --build-target=foo  # build only target `foo`
./jcc --build build.c -j8                 # 8 parallel jobs
./jcc --build build.c -O build/            # output directory
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

- `-I`, `-isystem`, `-D`, `-U` apply to every target the build script
  does not explicitly override.
- `--std=` is forwarded to the underlying compiler.
- `-l`, `-L`, `--library`, `--library-path` are forwarded as link flags.
- VM-only options (`-0`/`-1`/`-2`/`-3`, `--optimize[=N]`,
  `--vm-profile`, `--debug`, the bytecode-output modes) are
  **rejected** in `--build` mode. The build script never needs
  them, and the build runner does not run VM code itself. Sources
  that need these options declare them via `#pragma jcc ...` (see
  [JCC Pragma Extensions](#jcc-pragma-extensions)), and the testing
  framework is the right tool to exercise VM-mode behaviour.

## JCC Pragma Extensions

JCC-specific options (VM optimisation, safety levels, debug, profiling,
etc.) are declared from **inside C source** with `#pragma jcc ...`.
The pragma is the source-level counterpart to the CLI flag it mirrors;
when JCC compiles a file, the active pragmas produce the same effect
as the corresponding CLI flag.

```c
// Enable a JCC-specific option for the rest of this file
#pragma jcc optimise(2)
#pragma jcc safety(1)

// Disable / clear
#pragma jcc optimise(0)
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
| `#pragma jcc optimise(N)` | `--optimize=N` | Bytecode optimisation level (0..3). |
| `#pragma jcc safety(N)` | `-N` | Safety level preset (0..3). |
| `#pragma jcc debug` | `-g` | Enable interactive debugger. |
| `#pragma jcc vm_profile` | `-Y` | Enable VM opcode profiling. |
| `#pragma jcc vm_heap` | `-V` | Route `malloc`/`free` through the VM heap. |
| `#pragma jcc bounds_checks` | `-b` | Array bounds checking. |
| `#pragma jcc uaf_detection` | `-f` | Use-after-free detection. |
| `#pragma jcc type_checks` | `-t` | Runtime type checks on pointer derefs. |
| `#pragma jcc uninitialized_detection` | `-z` | Uninitialized variable detection. |
| `#pragma jcc overflow_checks` | `--overflow-checks` | Signed integer overflow. |
| `#pragma jcc stack_canaries` | `-s` | Stack overflow protection. |
| `#pragma jcc heap_canaries` | `-k` | Heap overflow protection. |
| `#pragma jcc memory_leak_detection` | `-m` | Track allocations and report leaks at exit. |
| `#pragma jcc stack_instrumentation` | `-i` | Track stack variable lifetimes. |
| `#pragma jcc pointer_sanitizer` | `-p` | All pointer checks (bounds, UAF, type). |
| `#pragma jcc memory_tagging` | `-T` | Temporal memory tagging. |
| `#pragma jcc reset` | — | Clear all JCC options set by previous pragmas. |

### Rules

- Pragmas are file-scope only in v1. A pragma applies to the rest
  of the file from its point of occurrence; later pragmas
  supersede earlier ones. **[Q: do we want `#pragma jcc push` /
  `#pragma jcc pop` for block-scope overrides? My recommendation:
  defer to v2 — none of the existing tests need block-scope
  overrides.]**
- CLI flags override pragmas. A test that wants safety level 2
  declares `#pragma jcc safety(2)`, but `./jcc --testing -3
  tests.c` still wins. This is the right precedence — the
  developer / CI run's flag is the authority.
- Unknown pragma arguments (e.g. `#pragma jcc optimise(99)`) are a
  hard error, not a warning. Spelling typos on pragma names
  (`#pragma jcc optimse(2)`) are a hard error too.
- A file that uses `#pragma jcc ...` compiles to native (via
  `-c=native`) or to bytecode (via the default mode) without
  needing the corresponding CLI flag. The pragmas are simply
  ignored on the native path.

### Why a pragma, not a build-system API call

- **Self-describing source.** A file that needs `-2` to be
  meaningful declares that fact inline. The test framework can run
  it correctly without an out-of-band config.
- **Toolchain-agnostic builds.** The build system forwards
  `-O2` / `-g` to `cc`; it has no business knowing about JCC's
  `-2` safety preset. A pragma keeps the build system out of the
  JCC-specific-options loop.
- **No DSL.** Both the build system and the test framework are C.
  Adding a new option is a one-line parser change, not a build
  script API change.

## Build Entry

The build entry is the function the build runner invokes. It is identified
in three ways, in order of precedence:

1. **Explicit flag:** `--build-entry=name` overrides everything.
2. **Attribute:** A function tagged with `[[jcc::build]]` (or
   `__attribute__((build))`). If exactly one such function exists, it is
   the entry. If more than one exists, the runner reports an ambiguity
   and exits with a diagnostic pointing at all candidates.
3. **Default name:** A function named `build_main` with signature
   `void build_main(jcc_build_ctx_t *)` (or `int` return).

GNU attribute syntax is accepted everywhere alongside the C23 form:

```c
[[jcc::build]]                                       // C23
__attribute__((build))                               // GNU
__attribute__((jcc::build))                          // GNU namespace form
```

The build entry is intercepted by the preprocessor the same way
`[[jcc::comptime]]` is — it never reaches the general attribute parser.

### Signature

```c
int build_main(jcc_build_ctx_t *ctx);
// or
void build_main(jcc_build_ctx_t *ctx);
```

Return value:

- `void` — the runner reports success as long as all targets compiled
  and linked without error.
- `int` — non-zero is treated as a build failure; the runner stops,
  prints the value, and exits with the same code (subject to
  `--build-keep-going`).

`build_main` is allowed to have additional parameters if they have
default arguments. **[Q: do we want the entry to be able to take config
values, e.g. `int build_main(jcc_build_ctx_t *, int debug, int release)`?
My instinct is no — config should come from CLI flags and env vars, not
the entry signature. We can revisit if there's a use case.]**

## Target Factories

Functions that build and return a `jcc_target_t *` are decorated with
`[[jcc::build_target]]`. The attribute tells the parser and reflection
tools that the function is a target factory — it returns a configured
target and may have side effects on the build context.

```c
[[jcc::build_target]]
jcc_target_t *make_foo(jcc_build_ctx_t *ctx) {
    jcc_target_t *t = jcc_executable(ctx, "foo");
    jcc_target_set_output(t, "bin/foo");
    jcc_target_add_source(t, "src/main.c");
    jcc_target_add_source(t, "src/util.c");
    jcc_target_add_include(t, "include");
    jcc_target_add_define(t, "DEBUG", "1");
    jcc_target_add_cflag(t, "-O2");
    return t;
}
```

Factories are normal C functions, callable from the build entry. They
can also be `[[jcc::comptime]]` if the target configuration is
constant-foldable — the runner doesn't care, it just calls them at
build time.

Reflection output (`--ffi-decls --json`) lists every
`[[jcc::build_target]]`-tagged function as a build-target descriptor
alongside ordinary function declarations.

## Target Kinds

Three kinds. All native — see [Native Backends](#native-backends) for
why the build system produces only native output.

| Kind | Function | Default output | Backend |
|------|----------|----------------|---------|
| Executable | `jcc_executable(ctx, name)` | `bin/<name>` | system toolchain (`cc` / `clang` / `gcc`); v2 plans LLVM IR |
| Static library | `jcc_static_lib(ctx, name)` | `lib<name>.a` | `ar` (or `cc -static`) |
| Dynamic library | `jcc_dynamic_lib(ctx, name)` | `lib<name>.{so,dylib}` | `cc -shared` |

### Native Backends

The build system is for native output only. v1's backend is the
system toolchain (`JCC_NATIVE_CC` or `cc` / `clang` / `gcc`); see
[README.md](../README.md) for the production native path. The
planned v2 backend is the bytecode-to-LLVM IR lowering (see
[LLVM.md](../docs/LLVM.md)) — it would let the build system emit
native code without going through an external compiler, while
keeping the same target API. Adding a new backend is a new
`jcc_backend_t` registration against `jcc_target_t`; the build
entry does not change.

**Bytecode / VM targets are out of scope for the build system.**
A `.jbc` file is just `-c=bytecode -o file.jbc source.c`; you
don't need a build system for it. Tests that need VM execution —
VM-only features, safety levels, debugging, profiling — belong in
the testing framework (see [TESTING_FRAMEWORK.md](TESTING_FRAMEWORK.md)),
where a test's `#pragma jcc ...` directives configure the VM
inline.

## Builder API

All APIs are exposed via a private `jcc_build.h` header that the runner
auto-injects into the build script the same way `reflection.h` is
auto-injected into `[[jcc::comptime]]` code. The header is **not** on
the public include path.

```c
// Build context
const char *jcc_build_root(jcc_build_ctx_t *ctx);
const char *jcc_build_out_dir(jcc_build_ctx_t *ctx);
int         jcc_build_jobs(jcc_build_ctx_t *ctx);
int         jcc_build_verbose(jcc_build_ctx_t *ctx);

// Target factories — all return a target owned by `ctx`
jcc_target_t *jcc_executable(jcc_build_ctx_t *ctx, const char *name);
jcc_target_t *jcc_static_lib(jcc_build_ctx_t *ctx, const char *name);
jcc_target_t *jcc_dynamic_lib(jcc_build_ctx_t *ctx, const char *name);

// Output
void jcc_target_set_output(jcc_target_t *t, const char *path);

// Sources
void jcc_target_add_source(jcc_target_t *t, const char *path);
void jcc_target_add_sources_glob(jcc_build_ctx_t *ctx, jcc_target_t *t,
                                 const char *pattern);
void jcc_target_add_source_str(jcc_target_t *t, const char *name,
                               const char *source);  // for #embed-style uses

// Flags
void jcc_target_add_include(jcc_target_t *t, const char *path);
void jcc_target_add_define(jcc_target_t *t, const char *name, const char *value);
void jcc_target_add_undef(jcc_target_t *t, const char *name);
void jcc_target_add_cflag(jcc_target_t *t, const char *flag);
void jcc_target_add_ldflag(jcc_target_t *t, const char *flag);

// Dependencies
void jcc_target_link_with(jcc_target_t *t, jcc_target_t *dep);
void jcc_target_add_lib(jcc_target_t *t, const char *name);   // -l
void jcc_target_add_libpath(jcc_target_t *t, const char *path); // -L

// Run
int jcc_build_run(jcc_build_ctx_t *ctx, jcc_target_t *t);
int jcc_build_run_all(jcc_build_ctx_t *ctx);     // topological order
int jcc_build_run_default(jcc_build_ctx_t *ctx); // alias for run_all + summary
```

Glob expansion uses POSIX `glob(3)` (already on the JCC standard-library
list) so `*.c` / `**/*.c` patterns work consistently with the rest of
the toolchain.

## Execution Model

```
            ./jcc --build build.c
                       │
                       ▼
   ┌─────────────────────────────────────────┐
   │  1. Preprocess build.c                  │
   │  2. Parse, register comptime helpers    │
   │  3. Find build entry (attr/name/flag)   │
   │  4. Compile entry into the JCC VM       │
   │  5. Invoke entry inside the VM          │
   │     ├─ entry calls factories,           │
   │     │   registers jcc_target_t *s       │
   │     ├─ entry calls jcc_build_run_*(ctx) │
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

The build entry is a regular C function compiled into the JCC VM by
exactly the same path that compiles `[[jcc::comptime]]` functions. The
build runner just changes *which* function is treated as the entry —
instead of `main()` in step 5, it's `build_main` (or whatever
`--build-entry` named).

The runner itself is provided to the entry as a set of FFI wrappers
over `cc`, `ar`, and `ld`. The FFI allow-list controls which of these
the entry can call:

```
./jcc --build build.c --ffi-allow=cc,ar,ld
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
#include_comptime <glob.h>   // POSIX glob, only visible to the build entry

[[jcc::build_target]]
jcc_target_t *libcore(void) {
    jcc_target_t *lib = jcc_static_lib(/*ctx=*/(void*)0, "core");
    jcc_target_set_output(lib, "lib/libcore.a");
    jcc_target_add_source(lib, "src/lib/sum.c");
    jcc_target_add_include(lib, "include");
    return lib;
}

[[jcc::build]]
int build_main(jcc_build_ctx_t *ctx) {
    jcc_target_t *core = jcc_static_lib(ctx, "core");
    jcc_target_set_output(core, "lib/libcore.a");
    jcc_target_add_source(core, "src/lib/sum.c");
    jcc_target_add_include(core, "include");

    jcc_target_t *greet = jcc_static_lib(ctx, "greet");
    jcc_target_set_output(greet, "lib/libgreet.a");
    jcc_target_add_source(greet, "src/greet.c");
    jcc_target_add_include(greet, "include");

    jcc_target_t *foo = jcc_executable(ctx, "foo");
    jcc_target_set_output(foo, "bin/foo");
    jcc_target_add_source(foo, "src/main.c");
    jcc_target_add_include(foo, "include");
    jcc_target_link_with(foo, core);
    jcc_target_link_with(foo, greet);
    jcc_target_add_define(foo, "GREET_DEFAULT", "world");

    return jcc_build_run_default(ctx);
}
```

Run:

```sh
$ ./jcc --build build.c --ffi-allow=cc,ar,ld
[1/4] cc -c -Iinclude src/lib/sum.c -o build/obj/core/sum.o
[2/4] ar rcs build/lib/libcore.a build/obj/core/sum.o
[3/4] cc -c -Iinclude src/greet.c -o build/obj/greet/greet.o
[3/4] ar rcs build/lib/libgreet.a build/obj/greet/greet.o
[4/4] cc -o build/bin/foo build/obj/foo/main.o -Lbuild/lib -lcore -lgreet
build succeeded (3 targets, 0 errors)
```

## Constraints

- The build script is C and is compiled with the same parser / preprocessor
  / VM as every other JCC source file. **No DSL, no TOML/YAML, no
  separate interpreter.**
- The build entry runs in the JCC VM, so the JCC-supported C surface is
  what the script can use (see [COVERAGE.md](COVERAGE.md)). It can
  call toolchain binaries only through FFI, which means the FFI allow-
  list is the only way to grant toolchain access.
- The build script does not have access to arbitrary process state.
  `getenv()` works for variables the runner has not blocked.
- Target factories are not allowed to call `jcc_build_run_*` themselves —
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

The eventual `build.c` for the JCC project replaces the existing
`Makefile`. The targets map roughly as:

| Make target | `build.c` equivalent |
|-------------|----------------------|
| `make` / `make jcc` | `jcc` executable target, run by default |
| `make libjcc.dylib` / `libjcc.so` | `libjcc` shared library target (`jcc_dynamic_lib`) |
| `make jcc-asan` | `jcc-asan` executable target with sanitizer cflags |
| `make jcc-ubsan` | `jcc-ubsan` ... |
| `make jcc-tsan` | `jcc-tsan` ... |
| `make jcc-msan` | `jcc-msan` ... (Linux only) |
| `make jcc-afl` | `jcc-afl` ... (different `cc`) |
| `make jcc-afl-asan` | `jcc-afl-asan` ... |
| `make fuzz_harness` | `fuzz_harness` libFuzzer target |
| `make test` | Depends on `jcc`; runs `./jcc --testing tests.c` (see [TESTING_FRAMEWORK.md](TESTING_FRAMEWORK.md)) |
| `make clean` | Runner subcommand (e.g. `./jcc --build --clean`) or a `clean` target that `rm -rf build/` |
| `make docs` | `docs` target, runs `headerdoc2html` via FFI |
| `make generate-std` | `generate-std` target, runs `./jcc -G ...` |
| `make bench` | `bench` target, runs `hyperfine` via FFI |
| `make profile-cpu` | `profile-cpu` target, runs a gperftools-instrumented binary |
| `make profile-mem` | `profile-mem` target, runs `leaks` / `valgrind` |
| `make fuzz-*` | `fuzz` target with sub-actions, or separate targets |

The build script is `build.c`:

```c
[[jcc::build_target]]
jcc_target_t *jcc(void) {
    jcc_target_t *t = jcc_executable(ctx, "jcc");
    jcc_target_add_sources_glob(ctx, t, "src/*.c");
    jcc_target_add_sources_glob(ctx, t, "src/stdlib/*.c");
    jcc_target_exclude_source(t, "src/ops.c");
    jcc_target_add_include(t, "include");
    jcc_target_add_cflag(t, "-std=c23");
    jcc_target_add_cflag(t, "-Wall");
    jcc_target_add_cflag(t, "-O0");
    jcc_target_add_cflag(t, "-g");
    jcc_target_add_ldflag(t, "-lffi");
    return t;
}

[[jcc::build_target]]
jcc_target_t *jcc_asan(void) {
    jcc_target_t *t = jcc();  // clone the base target
    jcc_target_set_name(t, "jcc-asan");
    jcc_target_add_cflag(t, "-fsanitize=address,undefined");
    return t;
}

// ... ubsan, tsan, msan, afl, afl-asan, fuzz_harness ...

[[jcc::build_target]]
jcc_target_t *jcc_tests(void) {
    jcc_target_t *t = jcc_executable(ctx, "jcc-tests");
    jcc_target_add_include(t, "include");
    jcc_target_add_source(t, "tests.c");
    jcc_target_add_cflag(t, "-DJCC_TEST_ENTRY=1");
    return t;
}

[[jcc::build]]
int build_main(jcc_build_ctx_t *ctx) {
    jcc_target_t *jcc_t = jcc();
    jcc_target_t *asan_t = jcc_asan();
    // ...ubsan, tsan, msan, afl, afl-asan, fuzz_harness ...
    jcc_target_t *tests_t = jcc_tests();

    if (jcc_build_run_default(ctx) != 0)
        return 1;

    // Run the test suite as a build step
    return jcc_build_run_custom(ctx, tests_t,
                                "./%s --testing tests.c",
                                tests_t->output);
}
```

### Toolchain selection

The Makefile currently detects libffi and (optionally) LLVM via
`pkg-config` / `llvm-config`. The build script does the same with
a small block at the top:

```c
[[jcc::comptime]]
int has_libffi(void) {
    // returns 1 if pkg-config finds libffi, else 0
    // implemented via $jcc_pkgconfig_check("libffi") (future)
    return 1;
}

[[jcc::build_target]]
jcc_target_t *jcc(void) {
    jcc_target_t *t = jcc_executable(ctx, "jcc");
    if (has_libffi()) {
        jcc_target_add_cflag(t, $(pkg-config --cflags libffi));
        jcc_target_add_ldflag(t, $(pkg-config --libs libffi));
    }
    // ...
    return t;
}
```

For the v1 migration, the toolchain detection is hard-coded
(Homebrew paths on macOS, `/usr/lib` on Linux) and then
generalised as the build system matures. **[Q: do we want a
`jcc_probe_toolchain()` helper that returns the resolved `cc`,
`ar`, `ld` paths and their cflags, or should the build script do
its own detection? My recommendation: a helper, so all projects
benefit from the same detection logic.]**

### Platform branches

The Makefile's `ifeq ($(UNAME_S),Darwin)` branches become normal
C `if` statements in `build.c`:

```c
const char *host = jcc_build_host(ctx);  // "darwin", "linux", "windows"
if (strcmp(host, "darwin") == 0) {
    jcc_target_add_cflag(t, "-I/opt/homebrew/opt/libffi/include");
} else if (strcmp(host, "linux") == 0) {
    jcc_target_add_cflag(t, "-I/usr/include");
}
```

### Chicken-and-egg

The build system itself must be built before it can build
anything else. The migration is therefore gradual:

1. Implement the build system (`[[jcc::build]]`,
   `[[jcc::build_target]]`, `jcc_executable`, etc.) — this doc.
2. Implement the testing framework — see
   [TESTING_FRAMEWORK.md](TESTING_FRAMEWORK.md). It is
   independent of the build system and can ship first.
3. Hand-write `build.c` for JCC. The first `build.c` is compiled
   by the existing `jcc` binary (the one built by the Makefile);
   the Makefile is kept around for one release as a fallback.
4. Land `build.c` as the canonical build description. Add
   `make build` as a thin shim that runs `./jcc --build
   build.c` (in case anyone is wedded to the Make CLI).
5. Once `build.c` is stable, delete the Makefile. The build
   instruction becomes `./jcc --build build.c`.

The test framework and the build system can ship in either order;
they don't depend on each other. The Makefile conversion is the
*last* step, not the first.

## Interaction With Existing Modes

| Mode | What happens to the build script |
|------|----------------------------------|
| `./jcc build.c` (default) | Compiles and runs the script as a normal C program. `[[jcc::build]]` and `[[jcc::build_target]]` are silently consumed; the build entry is not called. Useful for syntax-checking. |
| `./jcc -c=bytecode -o build.c.jbc build.c` | Emits a `.jbc` containing every definition in the file except the build entry. Loading the `.jbc` and calling `main()` works as above. |
| `./jcc -c=native -o build.c.o build.c` | Hands the file (build entry elided) to `cc` for a normal native build. |
| `./jcc --build build.c` | New behaviour. Calls the build entry in the VM; the rest of the file is compiled normally. |

The build script therefore lives in three worlds at once: a
runtime-checkable C program, a bytecode module, and a build
orchestrator. The build entry is the only thing that is *only* meaningful
in the third world.

## Open Questions

The **[Q:]** markers in this doc collect the design questions that
still need a decision. The short list:

1. Should the build entry be able to take config parameters? (My
   recommendation: no.)
2. ~~Should `[[jcc::build_target(kind)]]` accept a kind argument?~~
   **Resolved:** removed. The build system is native-only; bytecode
   targets are the testing framework's domain.
3. Should calling `jcc_build_run_*` from a factory be an error? (My
   recommendation: error.)
4. Should a `main()` definition in a build script be allowed in
   non-`--build` modes? (My recommendation: yes, only in non-`--build`
   modes.)
5. Do we want `--build-graph` / `--build-dry-run` to print the
   resolved command lines without executing? (My recommendation: yes,
   it's almost free since the runner already topo-sorts.)
6. Do we want a `jcc_probe_toolchain()` helper for libffi / LLVM
   detection, or should each build script roll its own? (My
   recommendation: helper, so all projects share the same detection.)
7. Should `#pragma jcc push` / `#pragma jcc pop` for block-scope
   JCC-option overrides ship in v1 or v2? (My recommendation: v2 —
   none of the existing tests need it.)

## Implementation Sketch (for the eventual ticket)

Roughly in order of dependency:

1. **`[[jcc::build]]` / `[[jcc::build_target]]` attribute parsing** —
   same machinery as `[[jcc::comptime]]`. The preprocessor intercepts
   them and tags the parsed declaration in the symbol table.
2. **`jcc_build.h` private header** — typedefs and prototypes for
   `jcc_build_ctx_t`, `jcc_target_t`, and the builder API. Auto-injected
   into build scripts the same way `reflection.h` is.
3. **`jcc_target_t` storage on `JCC *`** — opaque struct, owned by the
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
- [MACROS.md](../docs/MACROS.md) — the `[[jcc::comptime]]` system
  the build runner reuses for entry compilation and execution.
- [LLVM.md](../docs/LLVM.md) — the planned bytecode-to-LLVM IR
  backend, which is the v2 native backend for the build system
  (v1 uses the system toolchain).
- [COVERAGE.md](../docs/COVERAGE.md) — what C surface area a build
  script has access to.
- [SAFETY.md](../docs/SAFETY.md) — the safety levels (`-0` ... `-3`)
  that `#pragma jcc safety(N)` activates.
