# Build Mode

`cccc --build` runs a `build.c` script that describes targets, tests and installs.

A Zig/Rust-style build system embedded in C. The build script is a normal `.c`
source file that CCCC compiles and runs at build time; the script declares
targets (executables, static libraries, dynamic libraries) and the CCCC build
runner compiles and links them with the system toolchain (`cc` / `ar` / `ld`).
A target can also be built by `cccc` itself — a `CcccExecutable` (see
[Target kinds](build-targets.md#target-kinds)) — so a project whose sources use
`[[cccc::comptime]]` needs no external build orchestrator.

There is no DSL and no separate config language: the build script is C, compiled
by the same preprocessor / parser / VM as every other CCCC source file. The mode
mirrors `--testing` (see [testing.md](testing.md)) and reuses the
`[[cccc::comptime]]` interception machinery (see [macros.md](macros.md)).

This guide is about writing `build.c` scripts for your own project. To build
`cccc` itself, see [install.md](install.md).

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
| `-b`/`--build` | (off) | Switch to build mode. The input is a build script; `main()` is not required, and if present is simply ignored — never called. |
| `--build-entry=NAME` | `build_main`, or every `[[cccc::build_target]]` factory if none is found | Symbol to invoke as the build entry. |
| `--build-out-dir=PATH` | `build/` | Output directory for artifacts. |
| `--build-dry-run` | off | Topo-sort and print the resolved command lines without executing them. |
| `--build-target=NAME` | (all) | Build only the named registered target and its transitive dependencies. Pruning happens at `Build*` call time — the full graph is declared first, then the filter is applied. |
| `--build-tool-allow=NAME[,NAME...]` | (allow all) | Comma-separated allowlist of tool names that may be probed via `HaveTool` / `PkgConfig`, executed via `RunCustom`, or called via `CaptureCommand`. Repeated flags accumulate. `cc`/`ar`/`ld` are always invoked directly by the runner and are not subject to this list. |
| `--build-jobs=N` | `1` | Controls parallelism at two levels (POSIX only; non-POSIX always serial). **Target-level** (N>1, multiple simultaneously-ready targets): forks up to N target children in parallel, each compiling its sources serially. **Source-level** (N>1, only one target ready at a time): compiles up to N `cc -c` invocations within that target in parallel. The `-j` budget covers both modes — at most N compiler processes run at once. A `CcccExecutable` target is a single whole-program invocation, so source-level parallelism does not apply to it; it still participates in target-level parallelism. |
| `--build-keep-going` | off | Continue building independent targets when one fails, rather than stopping at the first error. Failed target names are listed in the final summary. Targets whose dependency chain includes a failed target are skipped (not attempted) and listed as `skipped:` in the summary. |
| `--build-quiet` | off | Suppress per-step `[N/M] cc ...` lines. Errors and the final summary are still printed. Overridden by `--build-verbose`. |
| `--build-verbose` | off | Print a per-target header (`>> target 'name' [kind, N source(s)]`) before each target and show all command lines. Overrides `--build-quiet`. `-v` also enables this. |
| `--build-list-targets` | off | Print the names of all `[[cccc::build_target]]` factory functions (one per line) and exit without running the build entry. |
| `--build-profile=NAME` | (none) | Set a global build profile for all targets: `debug`, `release`, `relwithdebinfo`, or `minsizerel`. Individual targets can override with `SetProfile`. |
| `--build-cache[=PATH]` | (off) | Enable incremental builds. Two-level strategy: (1) mtime fast path — skips recompile when the existing output is newer than all sources *and* every header prerequisite recorded by `-MMD`, gated by a per-target host-architecture-and-compiler stamp (see below); (2) content-hash CAS — on a mtime miss, looks up `hash(host-arch + source_content + header_content + compile_flags)` in a content-addressable store and restores the cached output without recompiling (the resolved compiler's own path is part of `compile_flags` here, via `argv[0]`, so the CAS already discriminates by compiler). Native `Executable`/`StaticLib`/`DynamicLib` targets cache at per-source (`.o`) granularity, plus a separate link/archive-step check. A `CcccExecutable` target is whole-program — no per-source `.o` — so it caches **per target**: a single key over the host-arch tag, the running `cccc`'s own content hash (a rebuilt `cccc` invalidates every `CcccExecutable` output), the invocation, the content of every source, every path declared with `AddInput`, *and* every header prerequisite `cccc --compile=native` reported through a `--deps-file` depfile (the front end's own record of the files it opened — the equivalent of `-MMD` for this mode). It never touches the shared CAS. Outputs compiled fresh are stored in the CAS for future reuse. Default cache directory: `<out-dir>/.cccc-cache`. Pass `=PATH` to use a shared or cross-build cache directory — for genuine cross-compiles the target triple set via `--build-cc`/`--build-triple` is folded into the compile flags and thus the key; for two *native* builds sharing an out-dir but differing in host architecture (e.g. arm64 and Rosetta x86_64 macOS binaries) or resolved compiler (e.g. clang then `--build-cc=`/`CCCC_BUILD_CC=` a different gcc), a per-target toolchain stamp (`<out-dir>/obj/<target>/.cccc-toolchain`, arch tag + compiler path) additionally invalidates the mtime fast path on either changing, so a build dir reused across architectures or compilers always recompiles instead of linking mismatched objects. |
| `--build-option=KEY=VALUE` | (none) | Pass a typed build option to the build script. Queried via `GetBuildOption(ctx, key)` / `HaveBuildOption(ctx, key)`. Repeated flags accumulate. |
| `--build-install` | off | After a successful build, copy artifacts registered with `InstallArtifact` to the install prefix. Default prefix: `PREFIX` env var or `/usr/local`. |
| `-- [args...]` | (none) | Positional arguments forwarded to the build entry. Accessible via `BuildArgc(ctx)` / `BuildArgv(ctx, i)`. |

Existing flags forwarded to every target's compile as defaults: `-I`, `-i`, `-D`,
`-U`, `--std=`, `-L`, `-l`. VM-only / output options (`-c`, `-d`/`--disassemble`,
`-O<n>`/`--optimize`, `--vm-profile`, `-g`/`--debug`, `-o`, `-E`, `-m`, `--ast`)
are rejected on the `cccc --build` command line. (This is about the CLI flag
`-c`; a `CcccExecutable` target invokes `cccc --compile=native` internally, and
that is unrelated.)

A `CcccExecutable`/`CcccObject` child `cccc` also receives the build's own
`--comptime-allow`, `--allow-url`, `--disable-ffi`, `--ffi-allow` and
`--ffi-deny`, and never reads `.cccc.toml` files (`--no-config`).

### How `--std=` is handled

An `Executable`/`StaticLib`/`DynamicLib` target compiles the source *you
wrote*, not serializer output — there is no fixed dialect floor to protect, so
`--std=` is honoured far more literally here than for `-c=native` (a
`CcccExecutable` target is the exception: `--std=` is forwarded verbatim to
`cccc --compile=native`, which applies its own C11 floor to the C it emits):

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

## Guide pages

| Page | Covers |
|---|---|
| [build-recipes.md](build-recipes.md) | The build entry, recipes in a program file, `@build` directives |
| [build-targets.md](build-targets.md) | Factory functions, target kinds and the start of the builder API |
| [build-sources.md](build-sources.md) | Target outputs and source-set helpers |
| [build-incremental.md](build-incremental.md) | Parallel and incremental builds, header dependencies |
| [build-env.md](build-env.md) | Environment, files, toolchain probing, options and arguments |
| [build-profiles.md](build-profiles.md) | Install steps, profiles and custom steps |
| [build-cross.md](build-cross.md) | Cross-compilation |
| [build-policy.md](build-policy.md) | Watching, bootstrapping, tool allowlist, FFI policy and limits |
