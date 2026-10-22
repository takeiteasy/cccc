# Install, Profiles and Custom Steps

Installing artifacts, build profiles and custom steps.

## Install

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

| Target kind      | Destination                  |
|------------------|------------------------------|
| executable       | `{prefix}/bin/{name}`        |
| cccc executable  | `{prefix}/bin/{name}`        |
| static lib       | `{prefix}/lib/lib{name}.a`   |
| dynamic lib      | `{prefix}/lib/lib{name}.{so\|dylib}` |

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

## Build profiles

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

## Custom steps

**`RunCustom(ctx, name, cmd)`** registers an arbitrary shell command as a build
step in the DAG. The command runs through a vendored bourne-compatible shell
(`src/build_shell.c`) supporting pipes, `<`/`>` redirection (any number, in
any order, on one command — `cmd < in > out` and `cmd > out < in` both
work), `;`/`&` sequencing, and `&&`/`||` with real short-circuit semantics.
The step's exit code is propagated — a non-zero exit stops the build, and a
malformed command is itself a non-zero exit with a diagnostic on stderr
naming what was wrong (never a silent no-op, never a bare exit code with no
explanation).

When one command carries two redirects of the same direction (`cmd > o1 >
o2`), the last one wins — `cmd`'s output goes to `o2`, and `o1` is created
but left empty — matching real `sh`/`bash`.

Every stage of a pipeline is forked before any of them is waited on, so a
stage producing more output than fits in one pipe buffer doesn't deadlock
the build waiting for a reader that hasn't started yet. When a pipeline
stage also carries its own redirect (`a | b > f | c`), the redirect wins —
`b`'s output goes to `f`, and `c` sees an empty input — matching real
`sh`/`bash`, not the other way around. A pipeline whose upstream stage
never terminates on its own (`producer | head`) stops cleanly once the
downstream reader has taken what it needs and exited: the producer sees
end-of-pipe and is signalled, rather than blocking forever.

The builtins `cd`, `pwd` and `exit` honour redirects and pipes and report a
real exit status, so `pwd > file`, `pwd | wc -c` and `cd nonexistent ||
fallback` all behave. A builtin used as a pipeline stage runs in a subshell,
so `cd sub | cmd` does not change the working directory of later commands in
the same step; a bare `cd` (no pipe) does.

Word splitting follows POSIX quote-removal rules: `'...'` is fully literal
(no escapes, no expansion); `"..."` allows `\"`, `\\`, `\$` and `\<newline>`
escapes plus `$VAR`/`${VAR}`/`$(...)` expansion; unquoted text allows the
same escapes and expansion. `$VAR`/`${VAR}` expands to the value from the
process environment (empty if unset) as a single literal chunk — the
expansion is never re-split into multiple words and never globbed, so
`$CFLAGS`-style "one variable, several arguments" splitting is unavailable
by design.

`$(cmd)` runs `cmd` under the same allowlist/blacklist as the command it
appears in, captures its stdout with trailing newlines stripped, and
substitutes the result as a single literal chunk — same non-splitting,
non-globbing rule as `$VAR`. Nesting is supported (`$(echo $(echo x))`) up
to a fixed depth. Two things distinguish it from a real shell: substitution
happens at *lex* time, eagerly, before `&&`/`||`/`;` structure is even
parsed — `false && $(cmd)` still runs `cmd`, unlike a real shell's deferred
evaluation. And only `$(...)` is recognized; backtick `` `cmd` ``
substitution stays literal and unsupported.

Everything else stays literal and unsupported: no `VAR=value cmd`
env-prefix syntax, no globbing (`*`, `?`), no `~` expansion, no
positional/special parameters (`$1`, `$@`, `$?`, ...), no `for`/`if`/`while`.
For anything needing those, either use `env VAR=value cmd args...` (a plain
command, not special syntax) or delegate to a real shell/script:
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
