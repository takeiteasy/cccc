# CCCC Build System (`--build`)

A Zig/Rust-style build system embedded in C. The build script is a normal `.c`
source file that CCCC compiles and runs at build time; the script declares
targets (executables, static libraries, dynamic libraries) and the CCCC build
runner compiles and links them with the system toolchain (`cc` / `ar` / `ld`).

There is no DSL and no separate config language: the build script is C, compiled
by the same preprocessor / parser / VM as every other CCCC source file. The mode
mirrors `--testing` (see [TESTING.md](TESTING.md)) and reuses the
`[[cccc::comptime]]` interception machinery (see [MACROS.md](MACROS.md)).

> Native and bytecode output is supported. See [Scope](#scope) for what is deferred.

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
| `--build-cache[=PATH]` | (off) | Enable incremental builds. Two-level strategy: (1) mtime fast path — skips recompile when the existing output is newer than all sources; (2) content-hash CAS — on a mtime miss, looks up `hash(source_content + compile_flags)` in a content-addressable store and restores the cached output without recompiling. Native targets cache at per-source (`.o`) granularity; bytecode targets cache at per-target granularity (all sources hashed together). Outputs compiled fresh are stored in the CAS for future reuse. Default cache directory: `<out-dir>/.cccc-cache`. Pass `=PATH` to use a shared or cross-build cache directory. |
| `--build-option=KEY=VALUE` | (none) | Pass a typed build option to the build script. Queried via `GetBuildOption(ctx, key)` / `HaveBuildOption(ctx, key)`. Repeated flags accumulate. (#559) |
| `--build-install` | off | After a successful build, copy artifacts registered with `InstallArtifact` to the install prefix. Default prefix: `PREFIX` env var or `/usr/local`. (#560) |
| `-- [args...]` | (none) | Positional arguments forwarded to the build entry. Accessible via `BuildArgc(ctx)` / `BuildArgv(ctx, i)`. (#558) |

Existing flags forwarded to every target's compile as defaults: `-I`, `-i`, `-D`,
`-U`, `--std=`, `-L`, `-l`. VM-only options (`-c`, `-d`/`--disassemble`,
`-O<n>`/`--optimize`, `--vm-profile`, `-g`/`--debug`, `-o`, `-E`, `-m`, `--ast`)
are rejected in `--build` mode.

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

## Conditional includes (`#include [[cccc::build]]`)

A `#include` with a `[[cccc::build]]` route attribute is only processed when
`--build` is active; otherwise the directive is silently skipped.  All three
attribute forms are accepted:

```c
#include [[cccc::build]]           "build/extra_targets.h"   // C23
#include @build                    "build/extra_targets.h"   // @-prefix
#include __attribute__((build))    "build/extra_targets.h"   // GNU
```

Angle-bracket includes work the same way:

```c
#include [[cccc::build]] <external/build_helpers.h>
```

This is the first-class equivalent of:

```c
#ifdef __CCCC_BUILD_MODE__
#include "build/extra_targets.h"
#endif
```

Use it to split large build scripts across multiple files without polluting
normal compilation.

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

The `kind=` option selects the output backend:

| `kind=` | Backend | Output |
|---------|---------|--------|
| `native` (default) | system `cc`/`ar`/`ld` | platform binary |
| `bytecode` | `cccc` whole-program compile | `.c4` bytecode file |

**`kind=bytecode`** compiles all sources in a single `cccc` invocation, producing
a runnable `.c4` file (default path `bin/<name>.c4`). This uses CCCC's existing
AST-level multi-TU merge (same as `cccc a.c b.c -o out.c4`). The factory must
be invoked via `--build-target=NAME`; the target must call `Executable()`.

```c
[[cccc::build_target(kind=bytecode)]]
BuildTarget *bc_app(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "app");
    AddSource(t, "src/main.c");
    AddSource(t, "src/lib.c");
    AddInclude(t, "include");
    return t;
}
```

```sh
cccc --build build.c --build-target=bc_app
# produces build/bin/app.c4 — run with: cccc build/bin/app.c4
```

Flags forwarded to the `cccc` invocation: `-I`, `-D`, `-U`, `--std`. Native
`cflags`/`ldflags`/profile flags are skipped. Incremental caching is supported
via `--build-cache` at per-target granularity (all sources hashed together).

### Bytecode static libraries (`.c4a`)

A `StaticLib()` returned from a `kind=bytecode` factory produces a `.c4a` file
(default path `lib/<name>.c4a`) — the bytecode equivalent of a native `.a` archive.
The sources are compiled as a single `cccc -c bytecode` invocation (which skips the
`main()` requirement).

```c
[[cccc::build_target(kind=bytecode)]]
BuildTarget *bc_mathlib(Builder *ctx) {
    BuildTarget *lib = StaticLib(ctx, "mathlib");
    AddSource(lib, "src/math.c");
    AddInclude(lib, "include");
    return lib;
}
```

```sh
cccc --build build.c --build-target=bc_mathlib
# produces build/lib/mathlib.c4a
```

### Bytecode dynamic modules (`.c4d`)

A `DynamicLib()` returned from (or created inside) a `kind=bytecode` factory
produces a `.c4d` file (default path `lib/<name>.c4d`) — a bytecode module that
can be loaded into a running CCCC VM at runtime via `cc_load_module()`.

```c
[[cccc::build_target(kind=bytecode)]]
BuildTarget *bc_plugin(Builder *ctx) {
    BuildTarget *plugin = DynamicLib(ctx, "plugin");
    AddSource(plugin, "src/plugin.c");
    return plugin;
}
```

```sh
cccc --build build.c --build-target=bc_plugin
# produces build/lib/plugin.c4d
```

### LinkWith between bytecode targets

`LinkWith(app, lib)` is supported when `app` is a `kind=bytecode` target (#563, #565).

Inside a `kind=bytecode` factory, `StaticLib()` and `DynamicLib()` targets are
automatically treated as bytecode (no separate annotation needed):
- `StaticLib` deps are compiled **standalone** as `.c4a` files and then linked into
  the exe via the bytecode linker pass (`--link`). This uses cross-module symbol
  resolution: the lib exports a symbol table and the exe carries text-relocation
  entries that are patched at link time.
- `DynamicLib` deps are **not linked statically**; they are built as standalone `.c4d`
  modules and loaded at runtime via `cc_load_module()`. Use `DependsOn(exe, plugin)`
  to ensure the `.c4d` is built before the exe.

```c
[[cccc::build_target(kind=bytecode)]]
BuildTarget *bc_app(Builder *ctx) {
    // Static lib dep: compiled as .c4a, linked into exe at build time.
    BuildTarget *lib = StaticLib(ctx, "mylib");
    AddSource(lib, "src/lib.c");
    AddInclude(lib, "include");

    // Dynamic module dep: built as .c4d, loaded at runtime.
    BuildTarget *plugin = DynamicLib(ctx, "plugin");
    AddSource(plugin, "src/plugin.c");

    BuildTarget *app = Executable(ctx, "app");
    AddSource(app, "src/main.c");
    AddInclude(app, "include");
    LinkWith(app, lib);        // lib built as .c4a, linked via --link
    DependsOn(app, plugin);    // plugin.c4d built first, not folded
    return app;
}
```

The build system generates two separate `cccc` invocations:
```sh
cccc src/lib.c --compile=bytecode -o build/lib/mylib.c4a -I include
cccc src/main.c -o build/bin/app.c4 -I include --link build/lib/mylib.c4a
```

`DependsOn` edges are ordering-only (as for native targets). `LinkWith` from a
bytecode target to a source-less target (a `CUSTOM` step or a native FFI library)
is ignored with a warning; native linking into `.c4` goes through FFI, not
`LinkWith`.

Function-pointer decay to cross-module symbols (taking the address of a function
defined in a `.c4a`) is supported: the text-relocation pass resolves both direct
`CALL` sites and address-taken references to exported symbols (#566).

### The `--link` compiler flag

```sh
cccc src/main.c -o app.c4 --link build/lib/mylib.c4a
```

`--link lib.c4a` appends the library's text and data into the compiled exe, resolves
any unresolved `CALL` sites that match exported symbols, and then writes the fully
linked `.c4` file.  Multiple `--link` flags are processed in order.  Any symbol
that remains unresolved after all libraries are linked causes a hard error.

The flag is only meaningful when writing a `.c4` output file (`-o file`).  Using
`--link` with `--compile=bytecode` (library output) emits a warning and is a no-op.

### Runtime module loading — `cc_load_module`

A `.c4d` module built by the build system can be appended into a running VM:

```c
#include "cccc.h"

int main(void) {
    VirtualMachine *vm = cc_init(NULL);
    cc_load_module(vm, "build/lib/plugin.c4d");
    // module's exported symbols are now callable via the resolved text relocations
    cc_destroy(vm);
    return 0;
}
```

`cc_load_module` appends the module's text and data segments into the host VM,
patching all absolute PC operands (jump/call targets) by the pre-append text size,
and re-anchoring data relocations. FFI registrations from the module are merged.
After appending, it also resolves any of the host VM's pending text relocations
whose target names match symbols exported by the loaded module.

The attribute accepts C23 and GNU forms:

```c
[[cccc::build_target]]
[[cccc::build_target(kind=native)]]
[[cccc::build_target(kind=bytecode)]]
__attribute__((build_target))
__attribute__((build_target(kind=native)))
__attribute__((build_target(kind=bytecode)))
__attribute__((cccc::build_target))
```

## Target kinds

| Kind | Factory | Default output | Backend |
|------|---------|----------------|---------|
| Executable | `Executable(name)` | `bin/<name>` | system `cc` |
| Static library | `StaticLib(name)` | `lib/lib<name>.a` | `ar rcs` |
| Dynamic library | `DynamicLib(name)` | `lib/lib<name>.{so,dylib}` | `cc -shared` |
| Custom step | `RunCustom(name, cmd)` | (none) | vendored shell |
| Bytecode executable | `Executable(name)` + `kind=bytecode` | `bin/<name>.c4` | `cccc` |
| Bytecode static lib | `StaticLib(name)` + `kind=bytecode` | `lib/<name>.c4a` | `cccc -c bytecode` |
| Bytecode dynamic mod | `DynamicLib(name)` + `kind=bytecode` | `lib/<name>.c4d` | `cccc -c bytecode` |

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

// Environment and filesystem
const char  *GetEnv(Builder *ctx, const char *name);         // env var value or NULL
const char  *CaptureCommand(Builder *ctx, const char *cmd);  // stdout of sh -c cmd (stripped), or NULL
int          FileExists(Builder *ctx, const char *path);     // 1 if path exists (any node type)
int          DirExists(Builder *ctx, const char *path);      // 1 if path exists and is a directory (#561)
const char **GlobFiles(Builder *ctx, const char *pattern);   // NULL-terminated array of matched paths (#561)
const char  *ReadFile(Builder *ctx, const char *path);       // file contents as string (≤4 MB) or NULL (#561)
int          WriteFile(Builder *ctx, const char *path, const char *content); // write string to file (#561)
int          SetCwd(Builder *ctx, const char *path);         // chdir; saves original CWD for auto-restore (#569)
const char  *GetCwd(Builder *ctx);                           // current working directory (#569)
int          CopyFile(Builder *ctx, const char *src, const char *dst); // copy file (#569)
int          MoveFile(Builder *ctx, const char *src, const char *dst); // rename/move file, EXDEV fallback (#569)
int          DeleteFile(Builder *ctx, const char *path);     // delete file (#569)
int          MkDir(Builder *ctx, const char *path);          // mkdir -p (#569)
int          DeleteDir(Builder *ctx, const char *path);      // rm -rf, no symlink follow (#569)

// Toolchain probing (#543, #559)
int          HaveTool(Builder *ctx, const char *name);       // 1 if tool in PATH + allowed
const char  *FindTool(Builder *ctx, const char *name);       // full path if found + allowed, or NULL (#559)
int          PkgConfig(BuildTarget *t, const char *pkg);     // run pkg-config, add flags
void         AddFramework(BuildTarget *t, const char *name); // macOS -framework Name shorthand (#559)

// Build options (#559)
const char  *GetBuildOption(Builder *ctx, const char *name); // value of --build-option=key=value or NULL
int          HaveBuildOption(Builder *ctx, const char *name); // 1 if --build-option=key[=...] was passed

// User args (#558) — positional args after -- on the CLI
int          BuildArgc(Builder *ctx);                // number of args after --
const char  *BuildArgv(Builder *ctx, int i);         // i-th arg (0-based), or NULL

// Install (#560)
void SetInstallPrefix(Builder *ctx, const char *path); // override install root (default: PREFIX or /usr/local)
void InstallArtifact(Builder *ctx, BuildTarget *t);    // register t for installation (requires --build-install)
int  BuildWantsInstall(Builder *ctx);                  // 1 if --build-install was passed

// Custom steps (#544)
BuildTarget *RunCustom(Builder *ctx, const char *name, const char *cmd);

// Profile (#548)
void        SetProfile(BuildTarget *t, const char *profile); // per-target profile override
const char *BuildProfile(Builder *ctx);  // global profile name (or NULL)

// Cross-compilation (#547)
void        SetTargetTriple(BuildTarget *t, const char *triple); // --target=<triple> (clang-style)
void        SetToolchain(BuildTarget *t, const char *cc);        // override CC binary per-target
const char *BuildTargetTriple(Builder *ctx); // global triple from --build-triple (or NULL)

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

### Working directory and file operations (#569)

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

### Toolchain probing (#543, #559)

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

### Build options (#559)

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

### Passing arguments to the build entry (#558)

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

### Install (#560)

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

> **v1 limitation (#568):** the vendored shell's `die()` helper calls `exit()` on
> OOM or a failed `open()` call inside a redirected step, which terminates the
> whole CCCC process rather than cleanly failing the build step.

### Cross-compilation (#547)

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
target-level parallel `-j` across DAG nodes (#557),
`[[cccc::build_target]]` discoverable factory functions with `--build-list-targets`
and `BuildTargetCount` / `BuildTargetName` reflection (#540),
build profiles (`debug` / `release` / `relwithdebinfo` / `minsizerel`) via
`--build-profile` and `SetProfile` (#548),
cross-compilation via `--build-triple` / `SetTargetTriple` and
`--build-cc` / `SetToolchain` (#547),
`GetEnv` / `CaptureCommand` / `FileExists` environment and filesystem helpers,
`--build-cache[=PATH]` incremental builds with mtime + content-hash CAS (#546),
incremental per-target caching for `kind=bytecode` targets (#562),
`kind=bytecode` build targets producing `.c4` executables via whole-program cccc
compilation (#545),
`LinkWith` between bytecode targets via source-folding (#563),
`StaticLib(kind=bytecode)` producing `.c4a` files and `DynamicLib(kind=bytecode)`
producing `.c4d` modules, runtime `cc_load_module()` API for appending `.c4d`
modules into a running VM (#564),
`FindTool` / `AddFramework` / `GetBuildOption` / `HaveBuildOption` /
`--build-option=KEY=VALUE` (#559),
`InstallArtifact` / `SetInstallPrefix` / `BuildWantsInstall` / `--build-install` (#560),
`DirExists` / `GlobFiles` / `ReadFile` / `WriteFile` filesystem helpers (#561),
a self-hosting `build.c` that builds cccc, libcccc, sanitizer variants,
`fuzz_harness`, stdlib regeneration, and bench (#549),
`__CCCC_BUILD_MODE__` / `__CCCC_TEST_MODE__` / `__CCCC_COMP_MODE__` predefined
mode macros (#575), and `#include [[cccc::build]]` / `#include [[cccc::test]]`
conditional include directives (#570).

## See also

- [docs/TESTING.md](TESTING.md) — `--testing` mode, the working analog this mode mirrors.
- [docs/MACROS.md](MACROS.md) — the `[[cccc::comptime]]` system reused for interception.
- [docs/COVERAGE.md](COVERAGE.md) — the C surface a build script can use.
