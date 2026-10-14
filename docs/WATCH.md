# Watch mode (`--watch`)

`--watch` reruns your work when a source file is saved. A program is hot-reloaded
in place and keeps its state; a `--build` script is run again.

```sh
cccc --watch app.c            # edit app.c: changed functions are redefined
cccc --watch build.c          # build.c has [[cccc::build]]: rebuild on every save
```

| You run | A save does |
|---|---|
| `cccc --watch app.c` | Reloads the changed functions into the running program |
| `cccc --watch --watch-restart app.c` | Restarts the program |
| `cccc --watch --build build.c` | Builds again |
| `cccc --watch build.c` with `[[cccc::build]]` in the file | Builds again (`--build` is implied) |

## Running a program

The program starts as usual. When a watched file is saved, the VM reloads it at
its next instruction, with the same rules as the debugger's
[`reload`](DEBUGGER.md#reloading-a-file): changed functions are redefined,
globals and statics keep their values, and a frame already inside an old body
finishes in it.

```
$ cccc --watch app.c
count=1
count=2
reloaded app.c: 1 changed, 2 unchanged, 0 new
count=20
```

- **Refused reloads** (a parse error, a changed signature or struct layout) print
  the error. The old code keeps running and the next save tries again.
- **Headers:** saving a header reloads the files that include it, directly or
  through another header; unchanged functions are skipped, so only the ones the
  edit touches are redefined. A header a reload starts to include is watched
  from then on.
- **Program exit:** the process waits for the next save and then runs the
  program from the start.
- **Compile errors** at startup print the error and wait for the fix.
- **Blocking calls:** a program inside `read`, `sleep` or another blocking call
  picks up a reload when the call returns.[^gil]
- **Under `-g`:** a save made while the debugger is at its prompt is applied
  when the program resumes.

Use `--watch-restart` when a change is outside what a reload accepts, or when
the program should start fresh each time.

## In the REPL

`cccc --repl --watch` reloads each file `:load` read when it is saved, as
`:reload` would. The message appears at the prompt without pressing Enter, and a
half-typed line is redrawn below it.

```
$ cccc --repl --watch
cccc> :load f.c
cccc> f()
(int) 41
reloaded f.c: 1 changed, 0 unchanged, 0 new
cccc> f()
(int) 42
```

A file with statements at file scope cannot be reloaded, as with `:reload`.
`--watch-restart` does not apply to the REPL, and `--watch-clear` does nothing.

## Building

With `--build`, the build runs again from scratch on every save. Each run is a
new `cccc` process, so edits to the build script itself apply too.

```
$ cccc --watch build.c
build succeeded (1 target, 0 errors)
watch: 4 files; waiting for changes
```

The watch set is found automatically:

| Watched | From |
|---|---|
| Sources and `AddInput` files | each target |
| Headers | the compiler's dependency files |
| Deferred globs | `AddSourcesGlobDeferred` patterns |
| The script and the files it includes | the script's own sources |

The output directory and every `DeclareOutput` path are never watched, so a
build cannot trigger itself.

### In the script

`Watch`, `WatchGlob` and `IgnoreWatch` add to or trim the set. `BuildWatching`
tells the script whether it runs under `--watch`.

```c
[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *app = Executable(ctx, "app");
    AddSource(app, "src/main.c");

    Watch(ctx, "config.toml");        // a file the build reads in its own way
    WatchGlob(ctx, "assets/*.png");   // also sees files created later
    IgnoreWatch(ctx, "*.swp");

    if (!BuildWatching(ctx))
        InstallArtifact(ctx, app);    // skip the install step while watching
    return BuildDefault(ctx);
}
```

These calls do nothing without `--watch`.

## Options

| Option | Effect |
|---|---|
| `--watch` | Turns watch mode on. Every option below also turns it on. |
| `--watch-path=PATH` | Also watch a file, or a directory and everything under it. Repeatable. |
| `--watch-ignore=GLOB` | Ignore matching paths. The glob is matched against the full path, the path from the working directory and the file name. Repeatable. |
| `--watch-debounce=MS` | Quiet time that ends a burst of saves. Default 100. |
| `--watch-clear` | Clear the terminal before each restart or rebuild. |
| `--watch-restart` | Restart the program on a save instead of reloading it. Run mode only. |

## Limits

`--watch` cannot be combined with `--testing`, `--test-run`, `-c`, `-d`, `-E`,
`-m`, `-p`, `--ast`, `--vm-profile` or `-o`, and cannot read standard input.

A reload follows the rules in [DEBUGGER.md](DEBUGGER.md#reloading-a-file): no
layout changes, and functions that call `[[cccc::comptime]]` code are not
reloaded once edited.

[^gil]: Only one guest thread runs at a time, and a reload runs between two
    instructions of the thread that holds the interpreter lock. A thread blocked
    in a host call cannot be interrupted, so the reload waits for it.
    macOS watches directories through FSEvents and Linux through inotify. On
    macOS, arming a directory briefly creates and removes a hidden
    `.cccc-watch-probe-N` file in it, to know the watch is live.
