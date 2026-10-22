# Install and Profiles

Install built artifacts, and pick flag presets. Custom steps are in [build-steps.md](build-steps.md).

## Install

`InstallArtifact` copies artifacts to a prefix after a successful build, but only with `--build-install`; without it the call does nothing.

```sh
cccc --build build.c --build-install               # /usr/local
PREFIX=/opt/myapp cccc --build build.c --build-install
```

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

| Call | Effect |
|---|---|
| `SetInstallPrefix(ctx, path)` | Install root; default is `$PREFIX`, else `/usr/local` |
| `InstallArtifact(ctx, t)` | Register `t` for install |
| `BuildWantsInstall(ctx)` | 1 if `--build-install` was passed |

| Target kind | Destination |
|---|---|
| Executable, cccc executable | `{prefix}/bin/{name}` |
| Static lib | `{prefix}/lib/lib{name}.a` |
| Dynamic lib | `{prefix}/lib/lib{name}.{so\|dylib}` |

`--build-dry-run` prints the destinations without copying.

## Profiles

| Profile | Compiler flags | Defines |
|---|---|---|
| `debug` | `-g -O0` | |
| `release` | `-O2` | `-DNDEBUG` |
| `relwithdebinfo` | `-O2 -g` | `-DNDEBUG` |
| `minsizerel` | `-Os` | `-DNDEBUG` |

```sh
cccc --build build.c --build-profile=release
```

```c
BuildTarget *lib = StaticLib(ctx, "core");
SetProfile(lib, "release");   // this target ignores the global profile

BuildTarget *app = Executable(ctx, "app");
AddCFlag(app, "-O3");         // target flags come after profile flags, so this wins
```

Profile flags come first, so a target's own `AddCFlag` overrides them (the last `-O` wins), and `AddUndef(t, "NDEBUG")` unsets `-DNDEBUG`. `BuildProfile(ctx)` returns the global profile name, or `NULL`.
