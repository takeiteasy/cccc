# Target Outputs and Source Sets

Refer to what a target produces, and manage groups of sources.

## Outputs and inputs

| Call | Effect |
|---|---|
| `TargetOutput(t)` | The path `t` will produce |
| `DeclareOutput(t, path)` | Record a `RunCustom` target's output (first one wins for `TargetOutput`) |
| `AddInput(t, path)` | A file the target reads without listing it as a source |
| `SetTargetEnv(t, name, value)` | An environment variable for `t`'s compiler or linker process only |

```c
BuildTarget *tool = Executable(ctx, "codegen");
AddSource(tool, "src/codegen.c");

char cmd[256];
snprintf(cmd, sizeof cmd, "%s --out gen/api.h", TargetOutput(tool));
BuildTarget *gen = RunCustom(ctx, "gen-api", cmd);
DependsOn(gen, tool);
```

`TargetOutput` on an `Executable`, `StaticLib` or `DynamicLib` is `<out_dir>/<path>`: the `SetOutput()` path, else the default (`bin/<name>`, `lib/lib<name>.a`). A `RunCustom` target has no convention, so it returns what `DeclareOutput` recorded (verbatim) or `""`.

### What `AddInput` does per kind

| Target | Effect |
|---|---|
| `RunCustom` | With `DeclareOutput`, enables the up-to-date skip ([build-steps.md](build-steps.md)) |
| `Executable`, `StaticLib`, `DynamicLib` | Names a file the compiler's `-MMD` depfile misses (an assembler `.incbin`); every object recompiles when it changes, so give such files a target of their own |
| `CcccExecutable` | Folds the file's contents into the cache key. Headers it `#include`s are tracked automatically; use `AddInput` for data a comptime read pulls in with no `#include` |

```c
BuildTarget *table = StaticLib(ctx, "std_table");
AddSource(table, "src/std_table.c");
AddInput(table, "include/stdio.h");   // embedded via .incbin
```

### Per-target environment

```c
SetToolchain(t, "/usr/bin/afl-clang-fast");
SetTargetEnv(t, "AFL_USE_ASAN", "1");
```

It does nothing on a `RunCustom` target (use `env VAR=value cmd` in the command). On a `CcccExecutable` the child process is `cccc`, so `SetTargetEnv(t, "CCCC_NATIVE_CC", "cosmocc")` picks the host compiler for the emitted C. `CcccPath(ctx)` gives a `RunCustom` step the path of the same `cccc`.

## Source sets

| Call | Effect |
|---|---|
| `AddSourcesGlob(t, pattern)` | Expands a `glob(3)` pattern now, sorted |
| `AddSourcesGlobDeferred(t, pattern)` | Expands at build time, after dependencies ran |
| `ExcludeSource(t, pattern)` | Drops sources matching an `fnmatch` pattern or exact path, in any call order |
| `AddSourceStr(t, name, content)` | Writes `<out_dir>/gen/<name>` and adds it |

```c
AddSourcesGlob(lib, "src/lib/**/*.c");
ExcludeSource(lib, "src/test_main.c");

BuildTarget *gen = RunCustom(ctx, "gen-sources", "python3 tools/codegen.py --out-dir gen/sources");
BuildTarget *app = Executable(ctx, "app");
AddSourcesGlobDeferred(app, "gen/sources/*.c");   // does not exist yet at this line
DependsOn(app, gen);

AddSourceStr(core, "version.c", "const char *version(void) { return \"1.0\"; }\n");
```
