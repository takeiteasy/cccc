# Targets and the Builder API

Factory functions, target kinds and the core builder calls.

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
match its C23/GNU spellings below. It describes the *factory*, not the
target the factory returns: a factory is free to return a `CcccExecutable`
(compiled by `cccc` itself — see [Target kinds](#target-kinds)) just as it
returns an `Executable`.

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
| cccc executable | `CcccExecutable(name)` | `bin/<name>` | `cccc --compile=native` |
| cccc object | `CcccObject(name)` | `obj/<name>.o` | `cccc --compile=object` |
| Custom step | `RunCustom(name, cmd)` | (none) | vendored shell |

A **`CcccExecutable`** target is compiled by the running `cccc` itself, not a
system `cc`: one whole-program `cccc --compile=native <sources...> -o
bin/<name>` invocation, so its sources may contain `[[cccc::comptime]]`. It has
no per-source object files — it caches per target, not per source (see
[Incremental builds](build-incremental.md#incremental-builds-and-header-dependencies) below) — and
accepts only the flags `cccc --compile=native` understands:
`-I`/`-isystem`/`-D`/`-U`/`-L`/`-l`/`--std=`/`--link-arg`. `AddLdFlag` and
`AddFramework` are sent as `--link-arg` to the host link line; add an object
passed that way with `AddInput` too, so the target rebuilds when it changes.
`AddCFlag`, `SetProfile`, and `SetTargetTriple` are rejected on this kind;
there is no compile-side host-`cc` flag pass-through. Use `SetTargetEnv(t, "CCCC_NATIVE_CC",
"...")` to choose the host compiler that links the emitted C — e.g. `cosmocc`
for a portable binary.

A **`CcccObject`** target compiles its sources with one `cccc --compile=object`
invocation and does not link, so the sources need no `main`. Other targets take
the object with `LinkWith`:

```c
BuildTarget *core = CcccObject(ctx, "core");
AddSource(core, "src/core.c");              // may use [[cccc::comptime]]

BuildTarget *app = Executable(ctx, "app");
AddSource(app, "src/main.c");
LinkWith(app, core);                        // links obj/core.o
```

An `Executable` or `DynamicLib` links the object, a `StaticLib` archives it,
and a `CcccExecutable` passes it as `--link-arg`. Each consumer rebuilds when
the object changes. `AddSource`, `AddInclude`, `AddDefine`, `AddUndef`,
`AddInput`, `DependsOn`, `SetOutput` and `SetTargetEnv` apply. Link options
(`AddLdFlag`, `AddFramework`, `AddLib`, `AddLibPath`, `LinkWith`,
`PkgConfig`) and `AddCFlag`, `SetProfile` and `SetTargetTriple` are errors;
put link options on the consuming target. `InstallArtifact` does not accept it.

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
BuildTarget *CcccExecutable(Builder *ctx, const char *name); // compiled by cccc itself
BuildTarget *CcccObject(Builder *ctx, const char *name);     // object compiled by cccc itself
const char  *CcccPath(Builder *ctx);     // the running cccc, for a RunCustom cmd

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
