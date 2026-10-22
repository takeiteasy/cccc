# Target Outputs and Source Sets

Refer to a target's own output and manage groups of sources.

## Referencing a target's own output

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

**`AddInput(t, path)`** records a file a target reads but does not list as a
source. On a `RunCustom` target, combined with `DeclareOutput`, this
gives `build_target()` a real "up to date" skip check: if the target has at least one declared input and one
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

On an **`Executable`**, **`StaticLib`** or **`DynamicLib`** target it names a
file the host compiler's `-MMD` depfile never reports, such as an assembler
`.incbin`. Every object of the target recompiles when one changes, so give such
files a target of their own:

```c
BuildTarget *table = StaticLib(ctx, "std_table");
AddSource(table, "src/std_table.c");
AddInput(table, "include/stdio.h");   // embedded via .incbin
```

`AddInput` on a **`CcccExecutable`** target means
something different: the declared paths' contents fold into the target's
cache key. Headers the target `#include`s are tracked **automatically** under
`--build-cache` — `cccc --compile=native` writes the set of files its front
end opened (sources plus resolved `#include`s) to a depfile, which the
builder folds into the cache key the way it folds a host `cc`'s `-MMD` output
for an ordinary `Executable`. `AddInput` remains for an input no depfile can
see: a data file the target reads at comptime through a `-D` define, which
has no `#include` at all. Declare those with `AddInput` (and `CcccPath(ctx)`
gives a `RunCustom` step the path of the same `cccc`, e.g. to run
`-c=generated` alongside).

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

For a `CcccExecutable` target the child process *is* `cccc`, so
`SetTargetEnv(t, "CCCC_NATIVE_CC", "cosmocc")` selects the host compiler
`cccc --compile=native` uses to compile and link the C it emits — this is the
supported way to produce a portable binary from a build target.

## Source-set ergonomics

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
