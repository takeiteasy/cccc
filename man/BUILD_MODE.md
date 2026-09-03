# CCCC Build System (`--build`)

A Zig/Rust-style build system embedded in C. The build script is a normal `.c`
source file that CCCC compiles and runs at build time; the script declares
targets (executables, static libraries, dynamic libraries) and the CCCC build
runner compiles and links them with the system toolchain (`cc` / `ar` / `ld`).

There is no DSL and no separate config language: the build script is C, compiled
by the same preprocessor / parser / VM as every other CCCC source file. The mode
mirrors `--testing` (see [TEST_MODE.md](TEST_MODE.md)) and reuses the
`[[cccc::comptime]]` interception machinery (see [MACROS.md](MACROS.md)).

This guide is about writing `build.c` scripts for your own project. To build
`cccc` itself, see [BUILD.md](BUILD.md).

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

`build.c` is a naming convention, not a requirement — the build script is
just the path passed to `--build`, so `cccc --build tools/mybuild.c` works
identically.

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
     graph and dispatches it: with `--build-jobs=N > 1` and multiple simultaneously-
     ready targets it forks up to N children in parallel; with only one ready target
     it runs in-process (preserving source-level parallelism from `--build-jobs`).
     Spawns cc/ar/ld to compile and link.
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
cccc --build build.c --build-cache             # enable incremental builds (default cache dir)
cccc --build build.c --build-cache=~/.cache/cccc  # incremental builds with explicit cache path
```

| Flag | Default | Meaning |
|------|---------|---------|
| `-b`/`--build` | (off) | Switch to build mode. The input is a build script; `main()` is not required (and is rejected). |
| `--build-entry=NAME` | `build_main` | Symbol to invoke as the build entry. |
| `--build-out-dir=PATH` | `build/` | Output directory for artifacts. |
| `--build-dry-run` | off | Topo-sort and print the resolved command lines without executing them. |
| `--build-target=NAME` | (all) | Build only the named registered target and its transitive dependencies. Pruning happens at `Build*` call time — the full graph is declared first, then the filter is applied. |
| `--build-tool-allow=NAME[,NAME...]` | (allow all) | Comma-separated allowlist of tool names that may be probed via `HaveTool` / `PkgConfig`, executed via `RunCustom`, or called via `CaptureCommand`. Repeated flags accumulate. `cc`/`ar`/`ld` are always invoked directly by the runner and are not subject to this list. |
| `--build-jobs=N` | `1` | Controls parallelism at two levels (POSIX only; non-POSIX always serial). **Target-level** (N>1, multiple simultaneously-ready targets): forks up to N target children in parallel, each compiling its sources serially. **Source-level** (N>1, only one target ready at a time): compiles up to N `cc -c` invocations within that target in parallel. The `-j` budget covers both modes — at most N compiler processes run at once. |
| `--build-keep-going` | off | Continue building independent targets when one fails, rather than stopping at the first error. Failed target names are listed in the final summary. Targets whose dependency chain includes a failed target are skipped (not attempted) and listed as `skipped:` in the summary. |
| `--build-quiet` | off | Suppress per-step `[N/M] cc ...` lines. Errors and the final summary are still printed. Overridden by `--build-verbose`. |
| `--build-verbose` | off | Print a per-target header (`>> target 'name' [kind, N source(s)]`) before each target and show all command lines. Overrides `--build-quiet`. `-v` also enables this. |
| `--build-list-targets` | off | Print the names of all `[[cccc::build_target]]` factory functions (one per line) and exit without running the build entry. |
| `--build-profile=NAME` | (none) | Set a global build profile for all targets: `debug`, `release`, `relwithdebinfo`, or `minsizerel`. Individual targets can override with `SetProfile`. |
| `--build-cache[=PATH]` | (off) | Enable incremental builds. Two-level strategy: (1) mtime fast path — skips recompile when the existing output is newer than all sources *and* every header prerequisite recorded by `-MMD`, gated by a per-target host-architecture-and-compiler stamp (see below); (2) content-hash CAS — on a mtime miss, looks up `hash(host-arch + source_content + header_content + compile_flags)` in a content-addressable store and restores the cached output without recompiling (the resolved compiler's own path is part of `compile_flags` here, via `argv[0]`, so the CAS already discriminates by compiler). Native targets cache at per-source (`.o`) granularity, plus a separate link/archive-step check. Outputs compiled fresh are stored in the CAS for future reuse. Default cache directory: `<out-dir>/.cccc-cache`. Pass `=PATH` to use a shared or cross-build cache directory — for genuine cross-compiles the target triple set via `--build-cc`/`--build-triple` is folded into the compile flags and thus the key; for two *native* builds sharing an out-dir but differing in host architecture (e.g. arm64 and Rosetta x86_64 macOS binaries) or resolved compiler (e.g. clang then `--build-cc=`/`CCCC_BUILD_CC=` a different gcc), a per-target toolchain stamp (`<out-dir>/obj/<target>/.cccc-toolchain`, arch tag + compiler path) additionally invalidates the mtime fast path on either changing, so a build dir reused across architectures or compilers always recompiles instead of linking mismatched objects. |
| `--build-option=KEY=VALUE` | (none) | Pass a typed build option to the build script. Queried via `GetBuildOption(ctx, key)` / `HaveBuildOption(ctx, key)`. Repeated flags accumulate. |
| `--build-install` | off | After a successful build, copy artifacts registered with `InstallArtifact` to the install prefix. Default prefix: `PREFIX` env var or `/usr/local`. |
| `-- [args...]` | (none) | Positional arguments forwarded to the build entry. Accessible via `BuildArgc(ctx)` / `BuildArgv(ctx, i)`. |

Existing flags forwarded to every target's compile as defaults: `-I`, `-i`, `-D`,
`-U`, `--std=`, `-L`, `-l`. VM-only / output options (`-c`, `-d`/`--disassemble`,
`-O<n>`/`--optimize`, `--vm-profile`, `-g`/`--debug`, `-o`, `-E`, `-m`, `--ast`)
are rejected in `--build` mode.

### How `--std=` is handled

Unlike `-c=native`, a `--build` target compiles the source *you wrote*, not
serializer output — there is no fixed dialect floor to protect, so `--std=`
is honoured far more literally here:

- Each target's own resolved compiler (`SetToolchain`, `--build-cc=`/
  `CCCC_BUILD_CC`, or the system default — see "Cross-compilation" below) is
  probed independently for a `-std=` spelling of the *named* standard: the
  standard you passed is tried first (`c23`), then its other spellings
  (`c2x`), stopping there — it never falls back to the `gnu<NN>` prefix and
  never descends to an older standard. This exists purely to route around
  the same spelling asymmetry `-c=native` handles (a host that rejects
  strict `-std=c23` but accepts `-std=c2x`); it never loosens a strict-ISO
  build to a GNU one the way `-c=native`'s ladder does for its own emitted C.
- If a target's compiler accepts **none** of the named standard's spellings,
  that target fails with an error naming the compiler and the spellings
  tried — the other targets in the build are unaffected, and
  `--build-keep-going` applies normally.
- `--build-dry-run` still runs this probe (a single `-fsyntax-only` spawn
  per distinct compiler), so the printed command lines show the spelling
  that would actually be forwarded, and an unhonourable `--std=` is caught
  during a dry run too.

## Parallel builds

`--build-jobs=N` operates at two levels that share the same `-j` slot budget:

**Target-level (N>1, multiple ready targets):** when two or more targets have all
their dependencies satisfied at the same time, the runner forks up to N child
processes and builds them simultaneously. Each child compiles its sources serially
(`jobs=1`) so the total number of concurrent compiler invocations does not exceed N.
Output from concurrent targets interleaves (same behaviour as `make -j`).

**Source-level (N>1, lone ready target):** when only one target is unblocked, it
runs in-process with the full `-j` budget, allowing up to N `cc -c` processes to
compile that target's sources simultaneously.

**Non-POSIX / `--build-dry-run`:** always uses the serial path regardless of `-j`.

**Known limitation:** idle slots are not redistributed. If two targets build in
parallel under `-j4`, each gets one serial source slot — the two idle slots are not
reclaimed for additional source parallelism within the children.

The toolchain used to compile/link `--build` targets themselves (including
cccc's own bootstrap) is selected via `CCCC_BUILD_CC` (else `cc` / `clang` /
`gcc`); `ar` is selected via `CCCC_BUILD_AR` (else `ar`). This is a separate
env var from `CCCC_NATIVE_CC`, which only selects the compiler `-c=native`
hands its *generated* C to — setting `CCCC_NATIVE_CC` to test
`-c=native` under a different compiler no longer affects which compiler
builds `--build` targets.

A real gcc (e.g. Homebrew's `gcc-16` on macOS) is a supported `CCCC_BUILD_CC`
value: `CCCC_BUILD_CC=/opt/homebrew/bin/gcc-16 ./cccc --build build.c
--build-target=cccc` builds and links cleanly.

## The build entry

The entry is the function the runner invokes. It is identified in precedence
order:

1. **Explicit flag:** `--build-entry=NAME`.
2. **Attribute:** a function tagged `[[cccc::build]]`. Exactly one is required;
   more than one is an ambiguity error listing every candidate.
3. **Default name:** a function named `build_main`.

A `[[cccc::build]]`-tagged function can be named anything — the tag alone
resolves it. The `build_main` fallback only applies when a script has no
`[[cccc::build]]` attribute and no `--build-entry` flag at all; it is not a
second requirement layered on top of the attribute.

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

## Mode predefined macros

When CCCC starts it defines exactly one of three mutually-exclusive macros
depending on the active mode:

| Macro | Defined when |
|---|---|
| `__CCCC_BUILD_MODE__` | `--build` is active |
| `__CCCC_TEST_MODE__` | `--testing` is active |
| `__CCCC_COMP_MODE__` | Neither `--build` nor `--testing` (normal compilation) |

Use `#ifdef` / `#ifndef` to branch on these at compile time:

```c
#ifdef __CCCC_BUILD_MODE__
#include "build/extra_targets.h"
#endif
```

## Conditional directives (`@build`)

Any preprocessor directive can be gated on `--build` mode by prefixing it with
a `@build` route attribute.  When `--build` is active the route token is
stripped and the directive is processed normally; when it is inactive the
directive is silently skipped.  All three attribute spellings are accepted:

```c
#define @build            BUILD_JOBS 8     // @-prefix
#define [[cccc::build]]   BUILD_JOBS 8     // C23
#define __attribute__((build)) BUILD_JOBS 8 // GNU
```

This extends to `#include`, `#ifdef`, `#ifndef`, `#undef`, and every other
preprocessor directive:

```c
#include @build "build/extra_targets.h"
#include @build <external/build_helpers.h>

#define @build BUILD_JOBS 8

#ifdef @build BUILD_JOBS
#  define JOBS BUILD_JOBS
#endif                              // plain #endif — no route needed
```

When `--build` is inactive, an `@build #ifdef`/`#ifndef`/`#if` is rewritten
as `#if 0` internally so the conditional stack stays balanced.  A plain
`#endif` (without `@build`) closes it correctly.  The `#else` branch under an
inactive `@build` conditional **runs** normally.

These are the first-class equivalents of the `__CCCC_BUILD_MODE__` guard style:

```c
// Old style                          // New style
#ifdef __CCCC_BUILD_MODE__            #define @build BUILD_JOBS 8
#define BUILD_JOBS 8
#endif

#ifdef __CCCC_BUILD_MODE__            #include @build "build/extra_targets.h"
#include "build/extra_targets.h"
#endif
```

Use them to split large build scripts across multiple files without polluting
normal compilation, and to define build-only constants cleanly.

## Discoverable factory functions

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

`kind=native` is the only supported value (on-disk bytecode targets were
removed) -- it exists mainly so the attribute has an explicit form to
match its C23/GNU spellings below.

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

// Output path resolution
const char *TargetOutput(BuildTarget *t);          // resolved on-disk output path
void        DeclareOutput(BuildTarget *t, const char *path); // record a RunCustom step's output

// Source-set ergonomics
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
void SetTargetEnv(BuildTarget *t, const char *name, const char *value); //

// Dependencies
void LinkWith(BuildTarget *t, BuildTarget *dep); // build before + -l<dep>
void DependsOn(BuildTarget *t, BuildTarget *dep); // build before, no linker flag
void AddLib(BuildTarget *t, const char *name);     // -l<name>
void AddLibPath(BuildTarget *t, const char *path); // -L<path>

// Environment and filesystem
const char  *GetEnv(Builder *ctx, const char *name);         // env var value or NULL
const char  *CaptureCommand(Builder *ctx, const char *cmd);  // stdout of sh -c cmd (stripped), or NULL
int          FileExists(Builder *ctx, const char *path);     // 1 if path exists (any node type)
int          DirExists(Builder *ctx, const char *path);      // 1 if path exists and is a directory
const char **GlobFiles(Builder *ctx, const char *pattern);   // NULL-terminated array of matched paths
const char  *ReadFile(Builder *ctx, const char *path);       // file contents as string (≤4 MB) or NULL
int          WriteFile(Builder *ctx, const char *path, const char *content); // write string to file
int          SetCwd(Builder *ctx, const char *path);         // chdir; saves original CWD for auto-restore
const char  *GetCwd(Builder *ctx);                           // current working directory
int          CopyFile(Builder *ctx, const char *src, const char *dst); // copy file
int          MoveFile(Builder *ctx, const char *src, const char *dst); // rename/move file, EXDEV fallback
int          DeleteFile(Builder *ctx, const char *path);     // delete file
int          MkDir(Builder *ctx, const char *path);          // mkdir -p
int          DeleteDir(Builder *ctx, const char *path);      // rm -rf, no symlink follow

// Toolchain probing
int          HaveTool(Builder *ctx, const char *name);       // 1 if tool in PATH + allowed
const char  *FindTool(Builder *ctx, const char *name);       // full path if found + allowed, or NULL
int          PkgConfig(BuildTarget *t, const char *pkg);     // run pkg-config, add flags
void         AddFramework(BuildTarget *t, const char *name); // macOS -framework Name shorthand

// Build options
const char  *GetBuildOption(Builder *ctx, const char *name); // value of --build-option=key=value or NULL
int          HaveBuildOption(Builder *ctx, const char *name); // 1 if --build-option=key[=...] was passed

// User args — positional args after -- on the CLI
int          BuildArgc(Builder *ctx);                // number of args after --
const char  *BuildArgv(Builder *ctx, int i);         // i-th arg (0-based), or NULL

// Install
void SetInstallPrefix(Builder *ctx, const char *path); // override install root (default: PREFIX or /usr/local)
void InstallArtifact(Builder *ctx, BuildTarget *t);    // register t for installation (requires --build-install)
int  BuildWantsInstall(Builder *ctx);                  // 1 if --build-install was passed

// Custom steps
BuildTarget *RunCustom(Builder *ctx, const char *name, const char *cmd);

// Profile
void        SetProfile(BuildTarget *t, const char *profile); // per-target profile override
const char *BuildProfile(Builder *ctx);  // global profile name (or NULL)

// Cross-compilation
void        SetTargetTriple(BuildTarget *t, const char *triple); // --target=<triple> (clang-style)
void        SetToolchain(BuildTarget *t, const char *cc);        // override CC binary per-target
const char *BuildTargetTriple(Builder *ctx); // global triple from --build-triple (or NULL)

// Factory reflection
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

### Referencing a target's own output

**`TargetOutput(t)`** returns the on-disk path `t` will produce, so a
`RunCustom` command can reference a binary a dependency target just built
instead of hardcoding a path:

```c
BuildTarget *tool = Executable(ctx, "codegen");
AddSource(tool, "src/codegen.c");

char cmd[256];
snprintf(cmd, sizeof(cmd), "%s --out gen/api.h", TargetOutput(tool));
BuildTarget *gen = RunCustom(ctx, "gen-api", cmd);
DependsOn(gen, tool);
```

For `Executable`/`StaticLib`/`DynamicLib` targets this is always
`<out_dir>/<path>` — the explicit `SetOutput()` path if given, else the
kind-appropriate default (`bin/<name>`, `lib/lib<name>.a`, ...). A
`RunCustom` target has no such convention (its command can write anywhere,
e.g. straight into the source tree), so `TargetOutput()` on one returns
whatever **`DeclareOutput(t, path)`** recorded — verbatim, not joined onto
`out_dir` — or `""` if `DeclareOutput()` was never called. `DeclareOutput`
may be called more than once on the same target (e.g. a codegen step that
produces two files); `TargetOutput()` returns the **first** one recorded.

**`AddInput(t, path)`** records a file a `RunCustom` step reads.
Combined with `DeclareOutput`, this gives `build_target()` a real "up to
date" skip check: if the target has at least one declared input and one
declared output, and every output exists and is at least as new as every
input, the command is skipped — printed as `(up to date)` rather than
`(custom)`. A target with no `AddInput` calls keeps the old behavior
(always runs); this check is **not** gated on `--build-cache` since it
reflects the build script's own declared intent, not a heuristic cache:

```c
BuildTarget *gen = RunCustom(ctx, "gen-api",
    "python3 tools/gen_api.py include/api.h > gen/api.inc");
AddInput(gen, "include/api.h");
AddInput(gen, "tools/gen_api.py");
DeclareOutput(gen, "gen/api.inc");
```

Without `AddInput`, `DeclareOutput()` alone is invalidation metadata only —
it just lets `TargetOutput()` resolve a path for downstream consumers; the
step still runs every build.

**`SetTargetEnv(t, name, value)`** sets an environment variable for `t`'s
compiler/linker child process only — e.g. `AFL_USE_ASAN=1` for a target
whose toolchain is an AFL++ wrapper that reads it at invocation time:

```c
SetToolchain(t, "/usr/bin/afl-clang-fast");
SetTargetEnv(t, "AFL_USE_ASAN", "1");
```

Has no effect on a `RunCustom` target — its command runs through the
vendored build shell (`src/build_shell.c`), not through `t`'s compiler
invocation. That shell also has no `VAR=value cmd` env-prefix syntax (a
real POSIX shell feature it doesn't implement); use `env VAR=value cmd
args...` inside a `RunCustom` command instead.

### Source-set ergonomics

**`AddSourcesGlob(t, pattern)`** expands a POSIX `glob(3)` pattern relative to
the current working directory and adds each match as a source file,
**immediately** — at the point this call is made, before the rest of the
build entry runs. Matches are returned in sorted order (deterministic across
machines/runs):

```c
AddSourcesGlob(lib, "src/lib/**/*.c");    // all .c under src/lib/
AddSourcesGlob(app, "src/platform/*.c");  // platform-specific sources
```

**`AddSourcesGlobDeferred(t, pattern)`** is the same expansion, but
deferred to `build_target()` time — after `t`'s dependencies (e.g. a
`RunCustom` codegen step) have already run. Use this when a pattern needs to
match a file a dependency creates during this same build; `AddSourcesGlob`
expands too early to see it:

```c
BuildTarget *gen = RunCustom(ctx, "gen-sources",
    "python3 tools/codegen.py --out-dir gen/sources");
BuildTarget *app = Executable(ctx, "app");
AddSourcesGlobDeferred(app, "gen/sources/*.c"); // doesn't exist yet at this line
DependsOn(app, gen);
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

### Incremental builds and header dependencies

Under `--build-cache`, every native compile also gets `-MMD -MF
<objdir>/<stem>.d`, giving the incremental cache visibility into headers a
source `#include`s — not just the `.c`/`.cpp` file itself:

- **Level 1 (mtime).** An existing `.o` is only considered current if it is
  at least as new as its source **and** every header prerequisite listed in
  its `.d`. A source with no `.d` yet (a first build, or an objdir from
  before this tracking existed) is never trusted by mtime alone — it
  self-heals on its next build rather than silently serving a stale object.
- **Level 2 (content-hash CAS).** The cache key folds in the *content* of
  every header prerequisite, not just the `.c` file's — a header edit that
  doesn't happen to change the source's own mtime relationship still
  produces a different key. Crucially, a source **with no `.d` file present
  in its objdir never touches the CAS at all** (neither lookup nor store):
  `--build-cache=PATH` is a *global* content-addressable store shared across
  every objdir that points at it, while `.d` files are per-objdir, so a
  fresh objdir sharing a warm cache could otherwise "hit" an object some
  other objdir compiled against different header content. The very first
  compile of a source in a given objdir always runs for real and stores
  under the key computed from the `.d` it just wrote.
- **Link/archive-step staleness.** The link (`cc -o ...`) or archive (`ar
  rcs ...`) step is skipped — reported as `(up to date) <target>` — when the
  output already exists, is at least as new as every object file, and an
  argv hash stamped in the objdir (`<objdir>/.cccc-link`) matches the
  current link command line. Before this, no target had any incremental
  check at the link step; a native EXE/STATIC/DYNAMIC target relinked
  unconditionally every build even when nothing changed.

All of this is gated on `ctx->cache_dir` (i.e. `--build-cache` must be
passed) — without it, every step rebuilds unconditionally, which the
two-pass stdlib regeneration in `build.c` explicitly relies on (see its
`stdlib_regen_step` comment).

### Environment and filesystem

**`GetEnv(ctx, name)`** returns the value of an environment variable, or `NULL`
if it is unset. Useful for respecting user-set variables like `CC`, `CFLAGS`,
or `PREFIX`:

```c
const char *cc = GetEnv(ctx, "CC");
if (cc) SetToolchain(app, cc);
```

**`CaptureCommand(ctx, cmd)`** runs `cmd` via `sh -c`, captures its stdout,
strips trailing whitespace, and returns a pointer valid until the build entry
returns. Returns `NULL` if the command exits with a non-zero status. Useful
for probing tool versions or generating values at build time:

```c
const char *ver = CaptureCommand(ctx, "git rev-parse --short HEAD");
if (ver) AddDefine(app, "GIT_REV", ver);
```

**`FileExists(ctx, path)`** returns 1 if `path` exists (as a file, directory,
or any other filesystem node), 0 otherwise. Useful for detecting vendored
vs. system dependencies:

```c
if (FileExists(ctx, "vendor/zlib/zlib.h")) {
    AddSource(lib, "vendor/zlib/inflate.c");
    AddInclude(lib, "vendor/zlib");
} else {
    AddLib(lib, "z");
}
```

**`DirExists(ctx, path)`** returns 1 if `path` exists and is a directory:

```c
if (DirExists(ctx, "vendor/zlib")) {
    AddInclude(lib, "vendor/zlib");
} else {
    AddLib(lib, "z");
}
```

**`GlobFiles(ctx, pattern)`** expands a POSIX `glob(3)` pattern and returns a
`NULL`-terminated `const char **` array of matching paths, or `NULL` if there
are no matches or the platform is not POSIX. Paths are valid until the build
entry returns:

```c
const char **headers = GlobFiles(ctx, "include/**/*.h");
for (int i = 0; headers && headers[i]; i++)
    printf("header: %s\n", headers[i]);
```

**`ReadFile(ctx, path)`** reads the file at `path` into a `NUL`-terminated
string and returns it. Returns `NULL` on error or if the file exceeds 4 MB.
The pointer is valid until the build entry returns:

```c
const char *ver = ReadFile(ctx, "VERSION");
if (ver) AddDefine(app, "APP_VERSION", ver);
```

**`WriteFile(ctx, path, content)`** writes `content` to `path`, creating parent
directories as needed. Returns 0 on success, -1 on error. Useful for generating
header files or version stubs at build time without going through `AddSourceStr`:

```c
WriteFile(ctx, "build/gen/config.h",
    "#define CCCC_VERSION \"1.0\"\n");
```

### Working directory and file operations

**`SetCwd(ctx, path)`** changes the process working directory to `path`. The
original CWD is saved on the first call and automatically restored when the build
entry returns, so accidental CWD leakage between targets is prevented. Returns 0
on success, -1 on error.

> **Note:** `cd` inside a `RunCustom` shell script does **not** affect the parent
> process CWD (RunCustom runs in a forked child). `SetCwd` changes the real
> process CWD and is visible to all subsequent build steps.

```c
const char *saved = GetCwd(ctx);
SetCwd(ctx, "third_party/zlib");
// ... relative path operations ...
SetCwd(ctx, saved);  // or rely on auto-restore at entry exit
```

**`GetCwd(ctx)`** returns the current process working directory as an interned
string valid until the build entry returns, or `NULL` on error.

**`CopyFile(ctx, src, dst)`** copies the file at `src` to `dst`. Returns 0 on
success, -1 on error.

**`MoveFile(ctx, src, dst)`** renames/moves `src` to `dst`. Automatically falls
back to copy + delete on cross-device moves (`EXDEV`). Returns 0 on success,
-1 on error.

**`DeleteFile(ctx, path)`** deletes the file at `path` (`unlink`). Returns 0 on
success, -1 on error.

**`MkDir(ctx, path)`** creates `path` and all intermediate directories
(`mkdir -p` semantics). Returns 0 on success, -1 on error.

**`DeleteDir(ctx, path)`** recursively removes `path` and all its contents
(`rm -rf` semantics). Does not follow symlinks out of the tree. Returns 0 on
success, -1 on error.

```c
// Example: generate a build artifact and clean up on error
MkDir(ctx, "build/gen");
if (WriteFile(ctx, "build/gen/config.h", contents) != 0) {
    DeleteDir(ctx, "build/gen");
    return 1;
}
```

### Toolchain probing

**`HaveTool(ctx, name)`** returns 1 when `name` is executable (found in `$PATH`)
and not blocked by `--build-tool-allow`:

```c
if (HaveTool(ctx, "pkg-config")) {
    PkgConfig(greet, "zlib");
}
```

**`FindTool(ctx, name)`** is like `HaveTool` but returns the full executable
path instead of 1, or `NULL` if not found or not allowed. Useful when the path
must be embedded in a `-D` define or passed to `RunCustom`:

```c
const char *cmake = FindTool(ctx, "cmake");
if (cmake) AddDefine(app, "CMAKE_BIN", cmake);
```

**`PkgConfig(t, pkg)`** runs `pkg-config --cflags <pkg>` and `pkg-config --libs
<pkg>`, then adds the tokens to `t->cflags` and `t->ldflags` respectively.
Returns 0 on success. Requires `pkg-config` in `$PATH`.

**`AddFramework(t, name)`** adds a macOS `-framework Name` linker flag pair to
`t`. This is a cleaner alternative to two `AddLdFlag` calls and works correctly
because the framework name is passed as a separate linker token:

```c
if (strcmp(BuildHost(ctx), "darwin") == 0) {
    AddFramework(app, "CoreFoundation");
    AddFramework(app, "Security");
}
```

All tool probing functions are subject to `--build-tool-allow`: if an allowlist
is set, the tool name must appear in it or the probe/spawn is refused.
`CaptureCommand` is also gated by the allowlist — include the literal string
`"CaptureCommand"` to allow it when an allowlist is active. `cc`/`ar`/`ld`
invocations by the host runner bypass the allowlist entirely.

### Build options

Zig-style typed build options let users parameterise a build script from the
command line without hacking environment variables.  Pass one or more
`--build-option=key=value` flags; the build script queries them at runtime:

```bash
cccc --build build.c --build-option=sanitize=asan --build-option=lto=1
```

```c
[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *app = Executable(ctx, "myapp");
    AddSource(app, "src/main.c");

    if (HaveBuildOption(ctx, "sanitize")) {
        const char *san = GetBuildOption(ctx, "sanitize");
        if (strcmp(san, "asan") == 0) AddCFlag(app, "-fsanitize=address");
    }
    if (HaveBuildOption(ctx, "lto"))
        AddCFlag(app, "-flto");

    return BuildDefault(ctx);
}
```

**`GetBuildOption(ctx, name)`** returns the value part of
`--build-option=name=value`, or `NULL` if `name` was not given.

**`HaveBuildOption(ctx, name)`** returns 1 if `--build-option=name` (with or
without a value) was passed.

### Passing arguments to the build entry

User-facing build steps (e.g. `install`, `test`, `clean`) can be passed as
positional arguments after `--` on the command line:

```bash
cccc --build build.c -- install --prefix=/usr/local
```

Inside the build script, read them with `BuildArgc` and `BuildArgv`:

```c
[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *app = Executable(ctx, "myapp");
    AddSource(app, "src/main.c");

    int n = BuildArgc(ctx);
    for (int i = 0; i < n; i++) {
        if (strcmp(BuildArgv(ctx, i), "install") == 0)
            InstallArtifact(ctx, app);
    }

    return BuildDefault(ctx);
}
```

**`BuildArgc(ctx)`** returns the number of arguments after `--`
(0 if `--` was not given).

**`BuildArgv(ctx, i)`** returns the `i`-th argument (0-based), or `NULL` if
`i` is out of range.

### Install

The install API copies built artifacts to a system prefix after a successful
build.  Activate it with `--build-install` on the command line; without that
flag, `InstallArtifact` is a no-op.

```bash
cccc --build build.c --build-install          # installs to /usr/local
PREFIX=/opt/myapp cccc --build build.c --build-install
```

**`SetInstallPrefix(ctx, path)`** overrides the install root.  The default is
the `PREFIX` environment variable, falling back to `/usr/local`.

**`InstallArtifact(ctx, t)`** registers `t` for installation.  After the build
entry returns and the build succeeded, registered artifacts are copied to the
appropriate subdirectory of the install prefix:

| Target kind   | Destination                  |
|---------------|------------------------------|
| executable    | `{prefix}/bin/{name}`        |
| static lib    | `{prefix}/lib/lib{name}.a`   |
| dynamic lib   | `{prefix}/lib/lib{name}.{so\|dylib}` |

**`BuildWantsInstall(ctx)`** returns 1 if `--build-install` was passed — useful
for skipping registration when the user did not request an install:

```c
[[cccc::build]]
int build_main(Builder *ctx) {
    SetInstallPrefix(ctx, "/opt/myapp");

    BuildTarget *app = Executable(ctx, "myapp");
    AddSource(app, "src/main.c");
    InstallArtifact(ctx, app);

    return BuildDefault(ctx);
}
```

The `--build-dry-run` flag is respected: install steps print their destination
paths without copying.

### Build profiles

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

### Custom steps

**`RunCustom(ctx, name, cmd)`** registers an arbitrary shell command as a build
step in the DAG. The command runs through a vendored bourne-compatible shell
(`src/build_shell.c`) supporting pipes, `<`/`>` redirection, `;`/`&`
sequencing, and `&&`/`||` with real short-circuit semantics. The
step's exit code is propagated — a non-zero exit stops the build, and a
malformed command is itself a non-zero exit (never a silent no-op).

Word splitting follows POSIX quote-removal rules: `'...'` is fully literal
(no escapes, no expansion); `"..."` allows `\"`, `\\`, `\$` and `\<newline>`
escapes plus `$VAR`/`${VAR}` expansion; unquoted text allows the same
escapes and expansion. `$VAR`/`${VAR}` expands to the value from the
process environment (empty if unset) as a single literal chunk — the
expansion is never re-split into multiple words and never globbed, so
`$CFLAGS`-style "one variable, several arguments" splitting is unavailable
by design. Everything else stays literal and unsupported: no `$(...)` or
backtick command substitution, no `VAR=value cmd` env-prefix syntax, no
globbing (`*`, `?`), no `~` expansion, no positional/special parameters
(`$1`, `$@`, `$?`, ...), no `for`/`if`/`while`. For anything needing those,
either use `env VAR=value cmd args...` (a plain command, not special syntax)
or delegate to a real shell/script:
`RunCustom(ctx, "regen", "sh tools/some_script.sh arg")`. `SetToolchain(t,
cc)` has the same constraint in a different spot — its argument is a single
executable path, not a command line, so `SetToolchain(t, "clang -arch
x86_64")` fails at execvp time ("No such file or directory"); use
`AddCFlag`/`SetTargetTriple` for extra compiler arguments instead.

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

Add `AddInput`/`DeclareOutput` (see "Referencing a target's own output"
above) to let a `RunCustom` step skip itself once its declared inputs stop
changing, instead of re-running on every build:

```c
BuildTarget *gen = RunCustom(ctx, "gen-headers",
    "python3 tools/gen.py > include/gen.h");
AddInput(gen, "tools/gen.py");
DeclareOutput(gen, "include/gen.h");
```

### Cross-compilation

Two mechanisms let a build script target a different architecture or OS than the
host.  They can be used independently or together.

**Clang-style triple (`--target`)** — use when your compiler accepts
`--target=<triple>` (clang, clang-cl).  Set once globally or per target:

```sh
# Global: all targets get --target=aarch64-linux-gnu
cccc --build build.c --build-triple=aarch64-linux-gnu

# Or per-target from inside the script:
SetTargetTriple(t, "aarch64-linux-gnu");
```

**Toolchain override** — use when your cross-compiler is a prefixed GCC binary
(e.g. `aarch64-linux-gnu-gcc`).  The override replaces the CC binary entirely;
`--target` is not added in this case.

```sh
# Global: every target uses the prefixed compiler
cccc --build build.c --build-cc=aarch64-linux-gnu-gcc

# Or per-target from inside the script:
SetToolchain(t, "aarch64-linux-gnu-gcc");
```

**Combining both** — set the toolchain for the compiler binary *and* the triple
for the clang-style `--target` flag when targeting a foreign sysroot with a
wrapper script that delegates to clang:

```c
SetToolchain(t, "clang");
SetTargetTriple(t, "aarch64-apple-macosx14.0");
```

**Precedence** for CC binary: `SetToolchain(t, …)` > `--build-cc` > system CC.
**Precedence** for triple: `SetTargetTriple(t, …)` > `--build-triple` (no triple = no `--target` flag).

**`BuildTargetTriple(ctx)`** returns the global triple set by `--build-triple`,
or `NULL` if none was passed.  Use it to conditionally add target-specific
sources or defines:

```c
const char *triple = BuildTargetTriple(ctx);
if (triple && strstr(triple, "aarch64"))
    AddCFlag(t, "-march=armv8-a");
```

> Cross-compilation tests use `--build-dry-run` to verify the correct flags
> appear in output without requiring a cross-toolchain on the host.

## Bootstrapping

`--build` is also how CCCC builds itself. That path — the stage0 `make`
compiler, the two-pass `src/std.c` regen, and the full target list `build.c`
exposes — is covered in [BUILD.md](BUILD.md).

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

> **Bootstrap caveat:** the two-pass stdlib regen (`stdlib_gen`, and the
> default build's own regen step) invokes the freshly-built `cccc` binary
> through `RunCustom` (`sh tools/regen_stdlib.sh <cccc-path>`). Under
> `--build-tool-allow`, `sh` must be in the allowlist or the bootstrap
> silently breaks with a permission diagnostic instead of regenerating
> `src/std.c`.

## FFI policy

Build mode exists to call tools, so FFI is **allow-all by default**: a build
script may call any native function without ceremony. Passing `--ffi-allow=a,b,c`
switches build mode into FFI allowlist mode — only the named C functions are
callable. The builder API (`__builtin_build_*` / PascalCase macros) and the
host-spawned `cc`/`ar`/`ld` are the build runtime itself and are always
available regardless of `--ffi-allow`/`--ffi-deny`/`--disable-ffi`.

For gating which **tool executables** may be probed or run via `RunCustom`,
use `--build-tool-allow` (see above).

## Not yet supported

Everything the builder API declares is implemented for the three native
target kinds. There is no self-hosting (compiling `cccc` with `cccc`), no
on-disk bytecode-target output, and idle `-j` slots are not redistributed
between a parallel target build and its children (see
[Parallel builds](#parallel-builds)).

## See also

- [BUILD.md](BUILD.md) — building `cccc` itself (`make` stage0 vs `--build`).
- [TEST_MODE.md](TEST_MODE.md) — `--testing` mode, the working analog this mode mirrors.
- [MACROS.md](MACROS.md) — the `[[cccc::comptime]]` system reused for interception.
- [COVERAGE.md](COVERAGE.md) — the C surface a build script can use.
- [NATIVE.md](NATIVE.md) — `-c=native` lowering scope and limitations.
