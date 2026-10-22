# Custom Build Steps

Run arbitrary commands as nodes in the build graph.

```c
BuildTarget *gen = RunCustom(ctx, "gen-headers", "python3 tools/gen.py > include/gen.h");
AddInput(gen, "tools/gen.py");
DeclareOutput(gen, "include/gen.h");

BuildTarget *core = StaticLib(ctx, "core");
AddSourcesGlob(core, "src/*.c");
DependsOn(core, gen);       // core waits for gen; no -lgen-headers
LinkWith(app, core);        // app gets -lcore
```

| Call | Effect |
|---|---|
| `RunCustom(ctx, name, cmd)` | A command step; a non-zero exit stops the build |
| `DependsOn(t, dep)` | Ordering only: `dep` builds first, no `-l` flag |
| `AddInput(t, path)` | A file the step reads |
| `DeclareOutput(t, path)` | A file it writes |

With at least one input and one output, the step is skipped (`(up to date)`) when every output is at least as new as every input. It does not depend on `--build-cache`. With no `AddInput` it runs every build; `DeclareOutput` alone only lets `TargetOutput()` resolve a path ([build-sources.md](build-sources.md)).

## The command shell

Commands run through a built-in POSIX-like shell (`src/build_shell.c`).

| Supported | Notes |
|---|---|
| Pipes `\|` | All stages start together; `producer \| head` stops the producer cleanly |
| Redirects `<`, `>`, `>>` | Any number per command; for two of the same direction the last wins |
| `;`, `&`, `&&`, `\|\|` | Real short-circuiting |
| Builtins `cd`, `pwd`, `exit` | Honour redirects and pipes; as a pipeline stage they run in a subshell, so `cd sub \| cmd` does not change the directory |
| Quoting | `'...'` literal; `"..."` allows `\"`, `\\`, `\$`, `\<newline>` and expansion |
| `$VAR`, `${VAR}` | Environment value as a single word: never re-split, never globbed |
| `$(cmd)` | Same rules, nestable, trailing newlines stripped[^lex] |

A malformed command exits non-zero with a diagnostic naming the problem.

| Not supported | Use instead |
|---|---|
| `VAR=value cmd` | `env VAR=value cmd args...` |
| Globbing, `~`, backticks, `$1`/`$@`/`$?`, `for`/`if`/`while` | `RunCustom(ctx, "regen", "sh tools/script.sh arg")` |
| Word splitting of `$CFLAGS` | `AddCFlag` per flag |

`SetToolchain(t, cc)` takes one executable path, not a command line: `"clang -arch x86_64"` fails at exec. Use `AddCFlag` or `SetTargetTriple` for extra arguments.

[^lex]: Substitution runs at lex time, before `&&`/`||`/`;` are parsed, so `false && $(cmd)` still runs `cmd`.
