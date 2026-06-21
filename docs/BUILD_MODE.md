# CCCC Build System

> **Status:** Design — decisions locked, ready for ticketing. This document
> reflects the design decisions made before implementation. Where v1 scope is
> deliberately narrow, the deferred work is called out under
> [v1 vs Later](#v1-vs-later).

A Zig/Rust-style build system embedded in C. The build script is a normal
`.c` source file that CCCC compiles and runs at build time; the script
declares targets (executables, static libraries, dynamic libraries) and the
CCCC build runner compiles and links them via the system toolchain.

The design reuses the `[[cccc::comptime]]` machinery: the build entry is
intercepted by the preprocessor exactly like a `[[cccc::comptime]]` function,
compiled into the CCCC VM, and invoked there. The mode mirrors the existing
`--testing` mode (`src/testing.c`), which already discovers tagged functions,
auto-injects a private header, and runs them in the VM.

## Key design decisions

These are settled; the rest of the document elaborates them.

1. **Hybrid entry model, entry-first.** v1 ships a single imperative
   `[[cccc::build]]` entry (a.k.a. `build_main`). Inside it you create and wire
   targets with full C control flow. The discoverable `[[cccc::build_target]]`
   factory attribute is optional sugar added in #540; see
   [Entry Model — Factory Functions](#entry-model--factory-functions).
2. **Host-side runner.** The VM script only *declares* the target graph;
   the host compiles it. `cc`/`ar`/`ld` are invoked by host runner code
   outside the VM, so progress can be streamed and parallelism added later
   without touching the script. See [Execution Model](#execution-model).
3. **Inverted FFI default in build mode.** Build mode exists to call tools,
   so FFI is **allow-all by default**. Passing `--ffi-allow=list` switches
   build mode into allowlist mode: only the listed tools are callable,
   everything else is blocked. `cc`/`ar`/`ld` are the host runner's job and
   are always available regardless. See [FFI Policy](#ffi-policy).
4. **Native output only.** The runner produces native executables and
   libraries via the system toolchain. Bytecode (`.c4`) targets are **not**
   in scope: there is no multi-source → single-`.c4` link step in the
   compiler today (`-c=bytecode` is single-TU only), so a bytecode target
   would first require building a bytecode linker. Deferred. See
   [Target Kinds](#target-kinds).
5. **VM/CCCC options are out of this plan.** Source-level VM configuration
   already exists as `#pragma cccc config(...)` / `#pragma cccc link(...)`
   (ticket #357). The build system forwards ordinary `-O2`/`-g`/`-std=` to
   `cc` and otherwise stays toolchain-agnostic. The old "CCCC Pragma
   Extensions" section of this plan proposed a *different*, now-superseded
   per-option pragma syntax and has been removed.

## Goals

- **Self-contained builds** — one `cccc` binary, no Make, no `build.zig`,
  no `Cargo.toml`. The build script is C the same way the rest of the
  project is C.
- **Familiar shape** — a designated `build_main` / `[[cccc::build]]`
  entry, a builder API for sources / flags / dependencies, and a top-level
  "run" call. Mirrors `zig build` ergonomically without a DSL.
- **Compatible with the existing toolchain** — the build script is still
  C. The same file compiles and runs as a normal program when `--build` is
  not set (the build entry is simply not invoked), which makes it
  syntax-checkable with the regular modes.

## Non-Goals (v1)

- **Incremental builds.** The runner always rebuilds from scratch. Cache
  invalidation and content-addressable storage are deferred.
- **Parallel jobs.** v1's runner is serial. `-j` is a clean host-side
  add-on once the serial runner is proven.
- **Cross-compilation.** Implemented in #547 via `--build-triple` / `SetTargetTriple`
  (clang-style `--target` flag) and `--build-cc` / `SetToolchain` (prefixed GCC binary);
  see [BUILDING.md](BUILDING.md).
- **Remote / distributed builds.** Local only.
- **Build profiles (release/debug).** Implemented in #548; see
  [BUILDING.md](BUILDING.md).
- **Bytecode targets.** Native output only (decision 4).
- **Discoverable target factories.** `[[cccc::build_target]]` is deferred
  (decision 1).

## CLI

```
./cccc --build build.c                     # run the build entry in build.c
./cccc --build build.c --build-target=foo  # build only target `foo`
./cccc --build build.c -O build/           # output directory
./cccc --build build.c --build-dry-run     # print command lines, run nothing
```

| Flag | Default | Meaning |
|------|---------|---------|
| `-b`/`--build` | (off) | Switch to build mode. The input file is treated as a build script; `main()` is not required. |
| `--build-entry=NAME` | `build_main` | Symbol to invoke as the build entry. |
| `--build-target=NAME` | (all) | Build only the named target (matched against the *registered* target name) and its dependencies. |
| `-O` / `--build-out-dir=PATH` | `build/` | Output directory. |
| `--build-dry-run` | off | Topo-sort and print the resolved command lines without executing them. |

Deferred to later releases: `-j`/`--jobs`, `--build-keep-going`,
`--build-quiet`, `--build-verbose`.

Existing flags forwarded to the per-target compile:

- `-I`, `-i`, `-D`, `-U` apply to every target the build script does not
  explicitly override.
- `--std=` is forwarded to the underlying compiler.
- `-l`, `-L`, `--library`, `--library-path` are forwarded as link flags.

VM-only options (`-0`/`-1`/`-2`/`-3`, `--optimize`, `--vm-profile`,
`--debug`, `--disassemble`, the bytecode-output modes) are **rejected** in
`--build` mode — the runner does not execute VM code itself and the build
script never needs them. Mixing them is a CLI error.

## FFI Policy

Build mode inverts the normal FFI default:

- **Default (no `--ffi-allow`):** all FFI is permitted. The mode exists to
  shell out to tools (`pkg-config`, `hyperfine`, `headerdoc2html`, …);
  guarding that by default would be pointless friction.
- **With `--ffi-allow=a,b,c`:** build mode switches to allowlist mode. Only
  the named tools are callable from the script; anything else is blocked.
  This is opt-in *restriction*, the opposite of the normal-mode default.
- **`cc` / `ar` / `ld`** are invoked by the host runner, not the VM script,
  so they are always available and are **not** subject to `--ffi-allow`.

A build script that calls no tools at all (e.g. a "dump the build graph"
subcommand implemented inside the entry) runs fine under either policy.

## Entry Model

v1 is **entry-first**. The build entry is the function the runner invokes;
it imperatively creates targets, wires their dependencies, and calls a
`run` function. It is identified in three ways, in order of precedence:

1. **Explicit flag:** `--build-entry=NAME` overrides everything.
2. **Attribute:** a function tagged `[[cccc::build]]`. If exactly one
   exists it is the entry; more than one is an ambiguity error pointing at
   all candidates.
3. **Default name:** a function named `build_main`.

GNU attribute syntax is accepted alongside the C23 form, exactly as for
`[[cccc::comptime]]`:

```c
[[cccc::build]]                                       // C23
__attribute__((build))                                // GNU
__attribute__((cccc::build))                          // GNU namespace form
```

The attribute is intercepted by the preprocessor the same way
`[[cccc::comptime]]` is — it never reaches the general attribute parser.

### Signature

```c
int  build_main(Builder *ctx);   // non-zero return = build failure
void build_main(Builder *ctx);   // success iff all targets built
```

The entry takes only the context pointer in v1. Config comes from CLI flags
and env vars, not the entry signature.

### Entry Model — Factory Functions (#540)

`[[cccc::build_target]]` tags a factory function as an individually-buildable
named target. Unlike `[[cccc::build]]` entries (which declare the full target
graph and must be run to completion), a factory is self-contained:

```c
[[cccc::build_target]]
BuildTarget *app(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "app");
    AddSource(t, "src/main.c");
    return t;
}
// ./cccc --build build.c --build-target=app  → calls app(ctx) directly; build_main not invoked
```

**Invocation model:** when `--build-target=NAME` matches a factory name the
runner calls that factory *directly*, skipping `build_main`. This avoids the
DAG-dedup problem (the original deferred design would have had the entry
re-create targets the factories already made). If `--build-target=NAME` does
not match any factory, the runner falls back to the entry-based flow and
matches `NAME` against registered target names (zig model).

**Reflection:** `--build-list-targets` prints factory names without running the
entry. Inside a build entry, `BuildTargetCount(ctx)` and `BuildTargetName(ctx, i)`
enumerate them programmatically.

**`kind=` option:** selects the output backend. `kind=native` (default) uses
the system toolchain. `kind=bytecode` is reserved for #545 (bytecode linker)
and is rejected at compile time with a diagnostic pointing at that ticket.

## Target Kinds

Three kinds, all native.

| Kind | Function | Default output | Backend |
|------|----------|----------------|---------|
| Executable | `Executable(ctx, name)` | `bin/<name>` | system toolchain (`cc` / `clang` / `gcc`) |
| Static library | `StaticLib(ctx, name)` | `lib<name>.a` | `ar rcs` |
| Dynamic library | `DynamicLib(ctx, name)` | `lib<name>.{so,dylib}` | `cc -shared` |

The backend is the system toolchain selected by `CCCC_NATIVE_CC` (or
`cc` / `clang` / `gcc`). Adding a backend later is a new
`cccc_backend_t` registration against `BuildTarget`; the entry signature
does not change. A bytecode backend is a candidate for a future release once
a bytecode linker exists (decision 4).

## Builder API

All APIs are exposed via a private `building.h` header that the runner
auto-injects into the build script the same way `reflection.h` is
auto-injected into `[[cccc::comptime]]` code and `testing.h` into `--testing`
mode. The header is **not** on the public include path.

```c
// Build context
const char *BuildRoot(Builder *ctx);
const char *BuildOutDir(Builder *ctx);
const char *BuildHost(Builder *ctx);    // "darwin" | "linux" | ...
int         BuildVerbose(Builder *ctx);

// Target factories — all return a target owned by `ctx`
BuildTarget *Executable(Builder *ctx, const char *name);
BuildTarget *StaticLib(Builder *ctx, const char *name);
BuildTarget *DynamicLib(Builder *ctx, const char *name);

// Output
void SetOutput(BuildTarget *t, const char *path);

// Sources
void AddSource(BuildTarget *t, const char *path);

// Flags
void AddInclude(BuildTarget *t, const char *path);
void AddDefine(BuildTarget *t, const char *name, const char *value);
void AddUndef(BuildTarget *t, const char *name);
void AddCFlag(BuildTarget *t, const char *flag);
void AddLdFlag(BuildTarget *t, const char *flag);

// Dependencies
void LinkWith(BuildTarget *t, BuildTarget *dep);
void AddLib(BuildTarget *t, const char *name);   // -l
void AddLibPath(BuildTarget *t, const char *path); // -L

// Run
int Build(Builder *ctx, BuildTarget *t);
int BuildAll(Builder *ctx);     // topological order
int BuildDefault(Builder *ctx); // run_all + summary
```

Deferred to later releases: `AddSources_glob` (glob expansion),
`AddSource_str` (`#embed`-style), `cccc_target_exclude_source`,
`cccc_probe_toolchain` (pkg-config/libffi detection),
`Build_custom` (run an arbitrary command as a build step).

## Execution Model

```
            ./cccc --build build.c
                       │
                       ▼
   ┌─────────────────────────────────────────┐
   │  1. Preprocess build.c (inject           │
   │     building.h)                         │
   │  2. Parse; intercept [[cccc::build]]      │
   │  3. Find the build entry (flag/attr/name) │
   │  4. Compile the entry into the CCCC VM    │
   │  5. Invoke the entry inside the VM:       │
   │     ├─ entry calls factories, builds      │
   │     │   BuildTarget graph (declarative  │
   │     │   data — no compilation yet)        │
   │     └─ entry calls Build_*(ctx)  │
   │  6. HOST runner takes the registered      │
   │     graph and:                            │
   │     - topo-sorts the target DAG           │
   │     - (--build-target) prunes to target   │
   │       + deps                              │
   │     - computes command lines; spawns      │
   │       cc/ar/ld (serial in v1)             │
   │     - streams progress                    │
   │  7. Exit with the entry's return value    │
   │     (or 0 on success)                     │
   └─────────────────────────────────────────┘
```

The VM-side entry only *builds data* (the target graph). The actual
compilation happens host-side in step 6 — this is the resolution of the
plan's old runner-location ambiguity, and it is what lets `cc`/`ar`/`ld` run
without going through the VM's FFI (decision 2/3). `Build_*` is the
hand-off point: it records that the entry wants the graph built, and the host
runner does the work after the entry returns (or synchronously, behind the
FFI boundary — an implementation detail of the run call).

## Constraints

- The build script is compiled with the same parser / preprocessor / VM as
  every other CCCC source file. **No DSL, no TOML/YAML, no separate
  interpreter.**
- The entry runs in the CCCC VM, so the CCCC-supported C surface is what the
  script can use (see [COVERAGE.md](../docs/COVERAGE.md)).
- The build script must not define `int main(void)` **in `--build` mode**
  (the parser rejects it). In non-`--build` modes a `main` is allowed, so the
  same file can be compiled and run as an ordinary program for testing.
- `--build` is mutually exclusive with `--vm-profile`, `--disassemble`,
  `--debug`, and the bytecode-output modes (CLI error).

## Interaction With Existing Modes

| Mode | What happens to the build script |
|------|----------------------------------|
| `./cccc build.c` (default) | Compiles and runs the script as a normal C program. `[[cccc::build]]` is consumed; the entry is not called. Useful for syntax-checking. |
| `./cccc -c=bytecode -o build.c.c4 build.c` | Emits a `.c4` of every definition except the build entry. |
| `./cccc -c=native -o build.c.o build.c` | Hands the file (entry elided) to `cc` for a normal native build. |
| `./cccc --build build.c` | Build mode. Calls the entry in the VM; the host runner compiles the declared targets. |

## Full Example

```
.
├── build.c
├── include/greet.h
└── src/
    ├── main.c
    ├── greet.c
    └── lib/sum.c
```

`build.c`:

```c
[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *core = StaticLib(ctx, "core");
    SetOutput(core, "lib/libcore.a");
    AddSource(core, "src/lib/sum.c");
    AddInclude(core, "include");

    BuildTarget *greet = StaticLib(ctx, "greet");
    SetOutput(greet, "lib/libgreet.a");
    AddSource(greet, "src/greet.c");
    AddInclude(greet, "include");

    BuildTarget *app = Executable(ctx, "app");
    SetOutput(app, "bin/app");
    AddSource(app, "src/main.c");
    AddInclude(app, "include");
    LinkWith(app, core);
    LinkWith(app, greet);
    AddDefine(app, "GREET_DEFAULT", "world");

    return BuildDefault(ctx);
}
```

Run:

```sh
$ ./cccc --build build.c
[1/4] cc -c -Iinclude src/lib/sum.c -o build/obj/core/sum.o
[2/4] ar rcs build/lib/libcore.a build/obj/core/sum.o
[3/4] cc -c -Iinclude src/greet.c -o build/obj/greet/greet.o
[3/4] ar rcs build/lib/libgreet.a build/obj/greet/greet.o
[4/4] cc -o build/bin/app build/obj/app/main.o -Lbuild/lib -lcore -lgreet
build succeeded (3 targets, 0 errors)
```

## Makefile Replacement (dogfood milestone — later)

The eventual `build.c` that replaces CCCC's own `Makefile` is the ultimate
validation of the system, but it is the **last** milestone, not part of the
initial v1. It needs several deferred features (glob sources, custom-command
build steps, toolchain probing for libffi, platform branches via
`BuildHost`) before it can express the existing targets
(`cccc`, `libcccc.{so,dylib}`, the sanitizer variants, `fuzz_harness`,
`docs`, `stdlib`, `bench`). The migration stays gradual: the first `build.c`
is compiled by the Makefile-built `cccc`; the Makefile is kept for one
release as a fallback; it is deleted only once `build.c` is stable.

## v1 vs Later

**Current:**

- `--build` mode branch in `src/main.c` (beside the native/testing blocks).
- `[[cccc::build]]` entry interception in `preprocess.c`; `build_main`
  default name; `--build-entry=NAME`.
- Auto-injected `building.h`; `BuildTarget` / `Builder` owned
  by the VM.
- Three native target kinds (executable / static / dynamic) via `cc`/`ar`/`ld`.
- Core builder API (sources, includes, defines, undef, cflags, ldflags,
  `link_with`, `add_lib`/`add_libpath`, `set_output`, context getters).
- Host-side runner with topo-sort, parallel source compilation (`--build-jobs`).
- `--build-target=NAME` (registered-name match + dep pruning).
- `-O`/`--build-out-dir`, `--build-dry-run`, `--build-keep-going`,
  `--build-quiet`, `--build-verbose`.
- Inverted FFI default (allow-all; `--ffi-allow` ⇒ allowlist).
- `AddSourcesGlob` / `AddSourceStr` / `ExcludeSource` (#542).
- `HaveTool` / `PkgConfig` / `--build-tool-allow` (#543).
- `RunCustom` / `DependsOn` (#544).
- `[[cccc::build_target]]` factory functions with factory-direct `--build-target`,
  `--build-list-targets`, and `BuildTargetCount` / `BuildTargetName` (#540).
- Build profiles (`debug` / `release` / `relwithdebinfo` / `minsizerel`) via
  `--build-profile` and `SetProfile` / `BuildProfile` (#548).
- One runnable example (`examples/build_demo/`) + `tests/test_build_*.c`.
- Docs: README "Build" section; `BUILDING.md` full API reference.

**Later:**

- Bytecode targets (`kind=bytecode` in `[[cccc::build_target]]`, pending #545).
- Incremental / caching.
- Self-hosting `build.c` replacing the Makefile (dogfood milestone).

## See Also

- [docs/TESTING.md](../docs/TESTING.md) — the built-in test framework and
  `--testing` mode, the working analog this mode mirrors.
- [docs/MACROS.md](../docs/MACROS.md) — the `[[cccc::comptime]]` system the
  build entry reuses for interception/compilation/execution.
- [docs/COVERAGE.md](../docs/COVERAGE.md) — the C surface a build script can use.
- `#pragma cccc config(...)` / `#pragma cccc link(...)` (ticket #357) — the
  source-level VM-option mechanism that replaces this plan's removed
  "CCCC Pragma Extensions" section.
