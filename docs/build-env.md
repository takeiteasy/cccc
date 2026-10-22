# Environment, Files and Options

Environment access, filesystem helpers, toolchain probing, options and arguments.

## Environment and filesystem

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

## Working directory and file operations

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

## Toolchain probing

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

## Build options

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

## Passing arguments to the build entry

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
