# Config Files

A config file sets default command-line options. cccc reads a user-wide file and the nearest project `.cccc`; the command line overrides both. A project file that grants capabilities needs your approval first.

```ini
# .cccc
std = c17
D = DEBUG, VERSION=3
W = all, error=format
comptime-allow = fs
```

Every line is one option. `cccc prog.c` with the file above runs as `cccc --std=c17 -DDEBUG -DVERSION=3 -Wall -Werror=format --comptime-allow=fs prog.c`.

## Where cccc looks

| File | Path | Trusted |
|---|---|---|
| User | `$XDG_CONFIG_HOME/cccc/config`, else `~/.config/cccc/config`, else `~/.cccc` | always |
| Project | the nearest `.cccc` from the current directory upward | per grant, see [Trust](#trust) |

The upward search stops after a directory that contains `.git`, and before your home directory. A project `.cccc` that is a symlink or belongs to another user is ignored with a warning. Your user file may be a symlink (dotfile managers) or owned by someone else.

Options apply in the order user, project, command line. A single-value option takes the last one given; a list option (`D`, `I`, `W`, `ffi-deny`, ...) collects all of them. `cccc --build` reads no config file: a build script is its own configuration.

## Syntax

| Form | Meaning |
|---|---|
| `key = value` | `--key=value`, or `-Kvalue` for a one-letter key |
| `openmp = true` | a flag; `false` leaves it off |
| `# note`, `; note` | comment |
| `D = A=1, B(x,y)=2` | list keys split on commas outside parentheses and quotes |
| `link-arg = "-Wl,-rpath,x"` | a quoted value is passed as written |
| `I = include` | a relative path is taken from the config file's directory, so it works from any subdirectory |

A key is any long option name without the dashes, or a one-letter option such as `D`, `I`, `W`, `f`. Relative values of `include`, `isystem`, `library-path`, `framework-path`, `iframework`, `out`, `deps-file`, `url-cache-dir`, `sysroot`, `build-out-dir` and `watch-path` are resolved against the file's directory; command-line paths stay relative to where you run cccc. Control characters in a file are an error. Sections (`[name]`) are not supported. Errors name the file and line:

```
error: /work/proj/.cccc:2: 'repl' cannot be set in a config file
```

## Which options a config can set

| Class | Options | Rule |
|---|---|---|
| Free | `std`, `D`, `U`, `I`, `isystem`, `W`, `f`, `O`, the safety and check flags, `ffi-deny`, `disable-ffi`, `test-timeout`, `max-errors`, ... | always applied |
| Trust | `comptime-allow`, `comptime-include-all`, `ffi-allow`, `asm-passthru`, `library`, `library-path`, `framework-path`, `iframework`, `link-arg`, `out`, `deps-file`, `url-cache-dir`, `sysroot`, `use-system-headers`, `compile`, `build-cc`, `build-tool-allow`, `build-option`, `build-out-dir`, `build-cache` | applied from the user file; from a project file only once approved |
| Command line only | `help`, `version`, `repl`, `debug`, `build`, `test`, `watch`, `entry`, the dump modes (`ast`, `preprocess`, `json`, ...), `trust`, `untrust`, `show-config`, `no-config` | an error in a config |

Trust options widen what runs on your machine or where cccc writes, so a cloned repository must not be able to switch them on by itself.

## Trust

The first time a project `.cccc` asks for a trust option, cccc shows the request on the terminal:

```
cccc: /work/proj/.cccc asks for:
    comptime-allow=fs
Trust these settings? [y/N]
```

`y` records the grant for that file in `$XDG_CONFIG_HOME/cccc/trusted` (`~/.config/cccc/trusted`). Later runs apply it silently. Adding a new grant to the file asks again for the new one only; free options and comments never ask.

Without a terminal (CI, a detached process), cccc skips the unapproved options and says so:

```
warning: /work/proj/.cccc: ignoring comptime-allow=fs (not trusted); run 'cccc --trust=/work/proj/.cccc'
```

| Command | Effect |
|---|---|
| `cccc --trust` | approve the nearest project `.cccc`, printing what it grants |
| `cccc --trust=<file or dir>` | approve a specific file, or the `.cccc` in a directory |
| `cccc --untrust[=<file or dir>]` | remove the approval |

The prompt reads the terminal, never standard input, so `cccc prog.c < data` keeps its input.

## Inspecting and bypassing

| Command | Effect |
|---|---|
| `cccc --show-config` | prints the files read, each grant's trust state and the resulting options, then exits |
| `cccc --no-config` | ignores both files |
| `CCCC_NO_CONFIG=1` | same as `--no-config` for every invocation |
| `CCCC_CONFIG_NO_PROMPT=1` | never ask; unapproved grants are skipped |

`--no-config` is also the way to drop an option a config sets, since the command line can add to a list or override a single value but cannot turn off a flag such as `openmp = true`.

## Example: a project that reads files at comptime

```ini
# .cccc
std = c23
comptime-allow = fs
```

```sh
cccc --trust        # once per clone, after reading the file
cccc prog.c         # comptime fopen/glob now work
```

See [COMPTIME_POLICY.md](COMPTIME_POLICY.md) for the capability classes.
