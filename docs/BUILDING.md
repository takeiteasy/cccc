# CCCC Build System (`--build`)

A Zig/Rust-style build system embedded in C. The build script is a normal `.c`
source file that CCCC compiles and runs at build time; the script declares
targets (executables, static libraries, dynamic libraries) and the CCCC build
runner compiles and links them with the system toolchain (`cc` / `ar` / `ld`).

There is no DSL and no separate config language: the build script is C, compiled
by the same preprocessor / parser / VM as every other CCCC source file. The mode
mirrors `--testing` (see [TESTING.md](TESTING.md)) and reuses the
`[[cccc::comptime]]` interception machinery (see [MACROS.md](MACROS.md)).

> v1 produces **native** output only. See [Scope](#scope) for what is deferred.

## Quick start

```c
// build.c
[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *core = StaticLib(ctx, "core");
    AddSource(core, "src/lib/sum.c");
    AddInclude(core, "include");

    BuildTarget *app = Executable(ctx, "app");
    AddSource(app, "src/main.c");
    AddInclude(app, "include");
    LinkWith(app, core);

    return BuildDefault(ctx);
}
```

```sh
$ cccc --build build.c
[1/3] cc -c src/lib/sum.c -o build/obj/core/sum.o -Iinclude
[2/3] ar rcs build/lib/libcore.a build/obj/core/sum.o
[3/3] cc -c src/main.c -o build/obj/app/main.o -Iinclude
[3/3] cc -o build/bin/app build/obj/app/main.o -Lbuild/lib -lcore
build succeeded (2 targets, 0 errors)
```

A runnable example lives in [`examples/build_demo/`](../examples/build_demo).

## How it works

```
        cccc --build build.c
                 │
                 ▼
  1. Preprocess build.c (inject building.h)
  2. Parse; intercept [[cccc::build]]
  3. Resolve the build entry (flag / attribute / name)
  4. Compile the entry into the CCCC VM
  5. Invoke the entry inside the VM:
       ├─ entry calls factories → builds the BuildTarget graph (data only)
       └─ entry calls Build*()
  6. The host runner (native C, behind the FFI boundary) topologically sorts the
     graph and spawns cc/ar/ld to compile and link the targets (parallel source
     compilation with `--build-jobs=N`).
  7. Exit with the entry's return value (non-zero = build failure).
```

The VM-side entry only *declares* data; the actual compilation happens host-side
in step 6. `cc`/`ar`/`ld` are spawned by the runner outside the VM, so they never
go through the VM's FFI layer and are not subject to FFI policy.

## CLI

```
cccc --build build.c                           # run the build entry in build.c
cccc --build build.c --build-entry=foo         # use `foo` as the entry
cccc --build build.c --build-out-dir=out       # output directory (default: build/)
cccc --build build.c --build-dry-run           # print command lines, run nothing
cccc --build build.c --build-target=NAME       # build only NAME and its transitive deps
cccc --build build.c --build-tool-allow=a,b,c  # tool allowlist for probing / custom steps
cccc --build build.c --build-jobs=8            # compile up to 8 sources in parallel
cccc --build build.c --build-keep-going        # continue past failures to build independent targets
cccc --build build.c --build-quiet             # suppress per-step command lines
cccc --build build.c --build-verbose           # show per-target headers and all command lines
```

| Flag | Default | Meaning |
|------|---------|---------|
| `-b`/`--build` | (off) | Switch to build mode. The input is a build script; `main()` is not required (and is rejected). |
| `--build-entry=NAME` | `build_main` | Symbol to invoke as the build entry. |
| `--build-out-dir=PATH` | `build/` | Output directory for artifacts. |
| `--build-dry-run` | off | Topo-sort and print the resolved command lines without executing them. |
| `--build-target=NAME` | (all) | Build only the named registered target and its transitive dependencies. Pruning happens at `Build*` call time — the full graph is declared first, then the filter is applied. |
| `--build-tool-allow=NAME[,NAME...]` | (allow all) | Comma-separated allowlist of tool names that may be probed via `HaveTool` / `PkgConfig` or executed via `RunCustom`. Repeated flags accumulate. `cc`/`ar`/`ld` are always invoked directly by the runner and are not subject to this list. |
| `--build-jobs=N` | `1` | Compile up to N source files in parallel within each target. Uses `fork()`+`exec()` on POSIX; falls back to serial on non-POSIX. |
| `--build-keep-going` | off | Continue building independent targets when one fails, rather than stopping at the first error. All failed target names are listed in the final summary. |
| `--build-quiet` | off | Suppress per-step `[N/M] cc ...` lines. Errors and the final summary are still printed. Overridden by `--build-verbose`. |
| `--build-verbose` | off | Print a per-target header (`>> target 'name' [kind, N source(s)]`) before each target and show all command lines. Overrides `--build-quiet`. `-v` also enables this. |
| `--build-list-targets` | off | Print the names of all `[[cccc::build_target]]` factory functions (one per line) and exit without running the build entry. |
| `--build-profile=NAME` | (none) | Set a global build profile for all targets: `debug`, `release`, `relwithdebinfo`, or `minsizerel`. Individual targets can override with `SetProfile`. |

Existing flags forwarded to every target's compile as defaults: `-I`, `-i`, `-D`,
`-U`, `--std=`, `-L`, `-l`. VM-only options (`-c`, `-d`/`--disassemble`,
`-O<n>`/`--optimize`, `--vm-profile`, `-g`/`--debug`, `-o`, `-E`, `-m`, `--ast`)
are rejected in `--build` mode.

The toolchain is selected via `CCCC_NATIVE_CC` (else `cc` / `clang` / `gcc`);
`ar` is selected via `CCCC_NATIVE_AR` (else `ar`).

## The build entry

The entry is the function the runner invokes. It is identified in precedence
order:

1. **Explicit flag:** `--build-entry=NAME`.
2. **Attribute:** a function tagged `[[cccc::build]]`. Exactly one is required;
   more than one is an ambiguity error listing every candidate.
3. **Default name:** a function named `build_main`.

The attribute accepts the C23 and GNU forms, like `[[cccc::comptime]]`:

```c
[[cccc::build]]                     // C23
__attribute__((build))              // GNU
__attribute__((cccc::build))        // GNU namespaced
```

Signature (the build context is passed as the first parameter):

```c
int  build_main(Builder *ctx);   // non-zero return = build failure
void build_main(Builder *ctx);   // success iff all targets built
```

The same file is still valid C: in default mode (`cccc build.c`) the
`[[cccc::build]]` attribute is consumed and the entry is simply not called.

## Discoverable factory functions (#540)

`[[cccc::build_target]]` tags a *factory function* — an alternative to putting
everything inside `build_main`. Each factory is a self-contained, individually
invocable target builder:

```c
[[cccc::build_target]]                // kind=native is the default
BuildTarget *app(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "app");
    AddSource(t, "src/main.c");
    return t;
}

[[cccc::build_target(kind=native)]]   // explicit
BuildTarget *lib(Builder *ctx) {
    BuildTarget *t = StaticLib(ctx, "core");
    AddSource(t, "src/lib.c");
    return t;
}
```

**Factory-direct invocation:** when `--build-target=NAME` matches a factory name
the runner calls that factory *directly* — `build_main` is skipped entirely.
This avoids the DAG-dedup problem that would arise if the entry re-created the
same targets the factories already made.

```sh
cccc --build build.c --build-target=app   # calls app(ctx) directly; build_main not invoked
```

**Listing factories:** `--build-list-targets` prints all factory names and exits:

```sh
cccc --build build.c --build-list-targets
# app
# lib
```

**Programmatic enumeration** inside the build entry:

```c
int n = BuildTargetCount(ctx);
for (int i = 0; i < n; i++)
    printf("factory: %s\n", BuildTargetName(ctx, i));
```

The `kind=` option selects the output backend. Currently only `kind=native` is
supported (the system toolchain, which is the default). `kind=bytecode` is
reserved and will be added when the bytecode linker is implemented (#545).

The attribute accepts C23 and GNU forms:

```c
[[cccc::build_target]]
[[cccc::build_target(kind=native)]]
__attribute__((build_target))
__attribute__((build_target(kind=native)))
__attribute__((cccc::build_target))
```

## Target kinds

| Kind | Factory | Default output | Backend |
|------|---------|----------------|---------|
| Executable | `Executable(name)` | `bin/<name>` | system `cc` |
| Static library | `StaticLib(name)` | `lib/lib<name>.a` | `ar rcs` |
| Dynamic library | `DynamicLib(name)` | `lib/lib<name>.{so,dylib}` | `cc -shared` |
| Custom step | `RunCustom(name, cmd)` | (none) | vendored shell |

## Builder API

All of the following are declared in the auto-injected `building.h`; do not
include it directly.

```c
// Build context accessors
const char *BuildRoot(Builder *ctx);      // launch directory
const char *BuildOutDir(Builder *ctx);    // output directory
const char *BuildHost(Builder *ctx);      // "darwin" | "linux" | ...
int         BuildVerbose(Builder *ctx);

// Target factories — each returns a target owned by the context
BuildTarget *Executable(Builder *ctx, const char *name);
BuildTarget *StaticLib(Builder *ctx, const char *name);
BuildTarget *DynamicLib(Builder *ctx, const char *name);

// Output / sources
void SetOutput(BuildTarget *t, const char *path);
void AddSource(BuildTarget *t, const char *path);

// Source-set ergonomics (#542)
void AddSourcesGlob(BuildTarget *t, const char *pattern); // POSIX glob(3)
void AddSourceStr(BuildTarget *t, const char *name, const char *content);
                                         // write content to <out_dir>/gen/<name>
void ExcludeSource(BuildTarget *t, const char *pattern);  // fnmatch or exact path

// Flags
void AddInclude(BuildTarget *t, const char *path);
void AddDefine(BuildTarget *t, const char *name, const char *value);
void AddUndef(BuildTarget *t, const char *name);
void AddCFlag(BuildTarget *t, const char *flag);
void AddLdFlag(BuildTarget *t, const char *flag);

// Dependencies
void LinkWith(BuildTarget *t, BuildTarget *dep); // build before + -l<dep>
void DependsOn(BuildTarget *t, BuildTarget *dep); // build before, no linker flag (#544)
void AddLib(BuildTarget *t, const char *name);     // -l<name>
void AddLibPath(BuildTarget *t, const char *path); // -L<path>

// Toolchain probing (#543)
int  HaveTool(Builder *ctx, const char *name);     // 1 if tool in PATH + allowed
int  PkgConfig(BuildTarget *t, const char *pkg);   // run pkg-config, add flags

// Custom steps (#544)
BuildTarget *RunCustom(Builder *ctx, const char *name, const char *cmd);

// Profile (#548)
void        SetProfile(BuildTarget *t, const char *profile); // per-target profile override
const char *BuildProfile(Builder *ctx);  // global profile name (or NULL)

// Factory reflection (#540)
int         BuildTargetCount(Builder *ctx);     // number of [[cccc::build_target]] factories
const char *BuildTargetName(Builder *ctx, int i); // name of factory i (0-based)

// Run (synchronous; returns 0 on success)
int Build(Builder *ctx, BuildTarget *t);  // t + its deps
int BuildAll(Builder *ctx);               // every target
int BuildDefault(Builder *ctx);           // run_all + summary
```

`Build*` compiles and links synchronously inside the call, so the
entry's return value reflects the real build status (this is why
`return BuildDefault();` is the idiomatic last line).

### Source-set ergonomics (#542)

**`AddSourcesGlob(t, pattern)`** expands a POSIX `glob(3)` pattern relative to
the current working directory and adds each match as a source file:

```c
AddSourcesGlob(lib, "src/lib/**/*.c");    // all .c under src/lib/
AddSourcesGlob(app, "src/platform/*.c");  // platform-specific sources
```

**`ExcludeSource(t, pattern)`** removes sources matching an `fnmatch` glob
pattern (or an exact path). Exclusions are applied at compile time regardless
of call order relative to `AddSource` / `AddSourcesGlob`:

```c
AddSourcesGlob(lib, "src/*.c");
ExcludeSource(lib, "src/test_main.c");  // drop test harness from lib
```

**`AddSourceStr(t, name, content)`** writes `content` to `<out_dir>/gen/<name>`
and adds the generated file as a source. Useful for compile-time code
generation:

```c
AddSourceStr(core, "version.c",
    "const char *version(void) { return \"1.0\"; }\n");
```

### Toolchain probing (#543)

**`HaveTool(ctx, name)`** returns 1 when `name` is executable (found in `$PATH`)
and not blocked by `--build-tool-allow`:

```c
if (HaveTool(ctx, "pkg-config")) {
    PkgConfig(greet, "zlib");
}
```

**`PkgConfig(t, pkg)`** runs `pkg-config --cflags <pkg>` and `pkg-config --libs
<pkg>`, then adds the tokens to `t->cflags` and `t->ldflags` respectively.
Returns 0 on success. Requires `pkg-config` in `$PATH`.

Both functions are subject to `--build-tool-allow`: if an allowlist is set, the
tool name must appear in it or the probe/spawn is refused.

### Build profiles (#548)

Four named profiles expose standard flag presets for compile steps:

| Profile | Compiler flags | Defines added |
|---|---|---|
| `debug` | `-g -O0` | — |
| `release` | `-O2` | `-DNDEBUG` |
| `relwithdebinfo` | `-O2 -g` | `-DNDEBUG` |
| `minsizerel` | `-Os` | `-DNDEBUG` |

Set a global default via `--build-profile=NAME`:

```sh
cccc --build build.c --build-profile=release
```

Or override per target inside the build script:

```c
BuildTarget *lib = StaticLib(ctx, "core");
SetProfile(lib, "release");   // this target uses release regardless of global

BuildTarget *app = Executable(ctx, "app");
// no SetProfile → inherits global profile (or no profile if none set)
AddCFlag(app, "-O3");         // target cflags come after profile flags, so this wins
```

Profile flags are prepended before the target's own `AddCFlag` entries so
per-target flags can override them (last `-O` wins; `-DNDEBUG` can be unset
with `AddUndef(t, "NDEBUG")`).

`BuildProfile(ctx)` returns the global profile name (or `NULL` if none is set).

### Custom steps (#544)

**`RunCustom(ctx, name, cmd)`** registers an arbitrary shell command as a build
step in the DAG. The command runs through a vendored bourne-compatible shell
(POSIX; pipes and redirections work). The step's exit code is propagated — a
non-zero exit stops the build.

**`DependsOn(t, dep)`** creates an ordering-only edge from `t` to `dep`: `dep`
is built first but no `-l<dep>` linker flag is added. This is the correct way
to express that a compile target depends on a codegen step:

```c
BuildTarget *gen = RunCustom(ctx, "gen-headers",
    "python3 tools/gen.py > include/gen.h");
DependsOn(core, gen);   // core waits for gen-headers; no -lgen-headers

BuildTarget *core = StaticLib(ctx, "core");
AddSourcesGlob(core, "src/*.c");
LinkWith(app, core);    // app gets -lcore (ordinary link dep)
```

> **v1 limitation:** the vendored shell's `die()` helper calls `exit()` on OOM
> or a failed `open()` call inside a redirected step, which terminates the whole
> CCCC process rather than cleanly failing the build step.  See the `BUILDMODE`
> tracker for the planned improvement.

## Tool allowlist (`--build-tool-allow`)

By default, build mode allows all tools to be probed and run. Passing
`--build-tool-allow` switches to allowlist mode: only named tools may be used
by `HaveTool`, `PkgConfig`, or `RunCustom`. Comma-separated names or repeated
flags both work:

```sh
cccc --build build.c --build-tool-allow=pkg-config,python3
cccc --build build.c --build-tool-allow=pkg-config --build-tool-allow=python3
```

The native build-runtime tools (`cc`/`ar`/`ld`) are invoked directly by the
host runner via `fork`+`execvp` and are never affected by this list.

## FFI policy

Build mode exists to call tools, so FFI is **allow-all by default**: a build
script may call any native function without ceremony. Passing `--ffi-allow=a,b,c`
switches build mode into FFI allowlist mode — only the named C functions are
callable. The builder API (`__builtin_build_*` / PascalCase macros) and the
host-spawned `cc`/`ar`/`ld` are the build runtime itself and are always
available regardless of `--ffi-allow`/`--ffi-deny`/`--disable-ffi`.

For gating which **tool executables** may be probed or run via `RunCustom`,
use `--build-tool-allow` (see above).

## Scope

**Current release:** the `--build` mode, `[[cccc::build]]` entry resolution,
the auto-injected `building.h`, the three native target kinds, the core builder
API, a host-side runner with topological sort, `--build-out-dir`,
`--build-dry-run`, `--build-target=NAME` registered-name selection with
transitive dependency pruning, the inverted FFI default,
`AddSourcesGlob` / `AddSourceStr` / `ExcludeSource` (#542),
`HaveTool` / `PkgConfig` / `--build-tool-allow` (#543),
`RunCustom` / `DependsOn` (#544),
`--build-jobs` / `--build-keep-going` / `--build-quiet` / `--build-verbose` (#541),
`[[cccc::build_target]]` discoverable factory functions with `--build-list-targets`
and `BuildTargetCount` / `BuildTargetName` reflection (#540),
build profiles (`debug` / `release` / `relwithdebinfo` / `minsizerel`) via
`--build-profile` and `SetProfile` (#548).

**Deferred to later releases:** target-level parallel `-j` across DAG nodes (#557);
bytecode targets (`kind=bytecode` in `[[cccc::build_target]]`, pending #545);
incremental / caching; cross-compilation; a self-hosting
`build.c` replacing the Makefile.

## See also

- [docs/TESTING.md](TESTING.md) — `--testing` mode, the working analog this mode mirrors.
- [docs/MACROS.md](MACROS.md) — the `[[cccc::comptime]]` system reused for interception.
- [docs/COVERAGE.md](COVERAGE.md) — the C surface a build script can use.
