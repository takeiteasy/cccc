# Config Files

A config file sets default command-line options in [TOML](https://toml.io). cccc reads a user-wide file and the nearest project `.cccc.toml`; the command line overrides both. A project file that grants capabilities needs your approval first.

```toml
# .cccc.toml
std = "c17"
D = ["DEBUG", "VERSION=3"]
W = ["all", "error=format"]
comptime-allow = ["fs"]
```

Every top-level key is one option. `cccc prog.c` with the file above runs as `cccc --std=c17 -DDEBUG -DVERSION=3 -Wall -Werror=format --comptime-allow=fs prog.c`.

## Where cccc looks

| File | Path | Trusted |
|---|---|---|
| User | `$XDG_CONFIG_HOME/cccc/config.toml`, else `~/.config/cccc/config.toml`, else `~/.cccc.toml` | always |
| Project | the nearest `.cccc.toml` from the current directory upward | per grant, see [Trust](#trust) |

The upward search stops after a directory that contains `.git`, and before your home directory. A project `.cccc.toml` that is a symlink or belongs to another user is ignored with a warning. Your user file may be a symlink (dotfile managers) or owned by someone else.

Options apply in the order user, project, command line. A single-value option takes the last one given; a list option (`D`, `I`, `W`, `ffi-deny`, ...) collects all of them. `cccc --build` reads no config file: a build script is its own configuration.

## Syntax

| TOML | Meaning |
|---|---|
| `key = "value"` | `--key=value`, or `-Kvalue` for a one-letter key |
| `max-errors = 5` | an integer is passed as its decimal text |
| `openmp = true` | a flag; `false` leaves it off |
| `D = ["A=1", "B(x,y)=2"]` | a list key takes an array, one option per item |
| `D = "A=1"` | a list key also takes a single string |
| `link-arg = ["-Wl,-rpath,x"]` | values are passed as written; commas and parentheses need no escaping |
| `I = "include"` | a relative path is taken from the config file's directory, so it works from any subdirectory |
| `# note` | comment |

A key is any long option name, or a one-letter option such as `D`, `I`, `W`, `f`. List keys are `D`, `U`, `I`, `isystem`, `library`, `library-path`, `framework-path`, `iframework`, `W`, `f`, `comptime-allow`, `ffi-allow`, `ffi-deny`, `watch-path`, `watch-ignore`, `build-tool-allow`, `build-option` and `link-arg`. Any other key takes one value.

Relative values of `include`, `isystem`, `library-path`, `framework-path`, `iframework`, `out`, `deps-file`, `url-cache-dir`, `sysroot`, `build-out-dir` and `watch-path` are resolved against the file's directory; command-line paths stay relative to where you run cccc.

Only strings, integers, booleans and arrays of strings are accepted. Tables (`[name]`, `a.b = 1`), floats, dates, a repeated key and control characters in any key or string are errors that name the file and line:[^1]

```
error: /work/proj/.cccc.toml:2: 'repl' cannot be set in a config file
```

## Which options a config can set

| Class | Options | Rule |
|---|---|---|
| Free | `std`, `D`, `U`, `I`, `isystem`, `W`, `f`, `O`, the safety and check flags, `ffi-deny`, `disable-ffi`, `sandbox`, `test-timeout`, `max-errors`, ... | always applied |
| Trust | `comptime-allow`, `comptime-include-all`, `ffi-allow`, `asm-passthru`, `allow-url`, `library`, `library-path`, `framework-path`, `iframework`, `link-arg`, `out`, `deps-file`, `url-cache-dir`, `sysroot`, `use-system-headers`, `compile`, `build-cc`, `build-tool-allow`, `build-option`, `build-out-dir`, `build-cache` | applied from the user file; from a project file only once approved |
| User config only | `trust-hash` | applied from the user file and the command line; an error in a project file |
| Command line only | `help`, `version`, `repl`, `debug`, `build`, `test`, `watch`, `entry`, the dump modes (`ast`, `preprocess`, `json`, ...), `trust`, `untrust`, `show-config`, `no-config` | an error in a config |

Trust options widen what runs on your machine or where cccc writes, so a cloned repository must not be able to switch them on by itself.

## Trust

The first time a project `.cccc.toml` asks for a trust option, cccc shows the request on the terminal:

```
cccc: /work/proj/.cccc.toml asks for:
    comptime-allow=fs
Trust these settings? [y/N]
```

`y` records the grant for that file in `$XDG_CONFIG_HOME/cccc/trusted` (`~/.config/cccc/trusted`). Later runs apply it silently. Adding a new grant to the file asks again for the new one only; free options and comments never ask.

Without a terminal (CI, a detached process), cccc skips the unapproved options and says so:

```
warning: /work/proj/.cccc.toml: ignoring comptime-allow=fs (not trusted); run 'cccc --trust=/work/proj/.cccc.toml'
```

| Command | Effect |
|---|---|
| `cccc --trust` | approve the nearest project `.cccc.toml`, printing what it grants |
| `cccc --trust=<file or dir>` | approve a specific file, or the `.cccc.toml` in a directory |
| `cccc --untrust[=<file or dir>]` | remove the approval |

The prompt reads the terminal, never standard input, so `cccc prog.c < data` keeps its input.

Parallel runs (`make -j`, a build driver) ask once. The first run prompts; runs started meanwhile wait, then use its answer, so a decline skips the grants everywhere without asking again.

## Pinning a file by hash

By default an approval survives any edit that adds no new grant. A pinned file is approved only while its content is unchanged: any edit, even a comment, withdraws every grant in it until you approve again.

```
cccc --trust --trust-hash       # approve and pin the nearest .cccc.toml
```

After an edit the prompt says so, and without a terminal the grants are skipped:

```
cccc: /work/proj/.cccc.toml has changed since you trusted it; it asks for:
    comptime-allow=fs
Trust these settings? [y/N]
```

```
warning: /work/proj/.cccc.toml: ignoring comptime-allow=fs (changed since trusted); run 'cccc --trust=/work/proj/.cccc.toml'
```

| Setting | Effect |
|---|---|
| `cccc --trust --trust-hash` | approve and pin the file |
| `cccc --trust` on a pinned file | approve again and pin the new content |
| `cccc --trust --trust-hash=off` | approve and drop the pin |
| `trust-hash = true` in the user config | pin every file approved from now on, by the prompt or by `--trust`; files already approved stay unpinned until `cccc --trust` runs on them again[^2] |
| `cccc --untrust` | remove the approval and the pin |

`--show-config` reports a pinned file as `pinned` or `changed since trusted`. The value is `on` or `off` and is written with `=`: `--trust-hash=off`.

## Inspecting and bypassing

| Command | Effect |
|---|---|
| `cccc --show-config` | prints the files read, each grant's trust state and the resulting options, then exits |
| `cccc --no-config` | ignores both files |
| `CCCC_NO_CONFIG=1` | same as `--no-config` for every invocation |
| `CCCC_CONFIG_NO_PROMPT=1` | never ask; unapproved grants are skipped |

`--no-config` is also the way to drop an option a config sets, since the command line can add to a list or override a single value but cannot turn off a flag such as `openmp = true`.

## Example: a project that reads files at comptime

```toml
# .cccc.toml
std = "c23"
comptime-allow = ["fs"]
```

```sh
cccc --trust        # once per clone, after reading the file
cccc prog.c         # comptime fopen/glob now work
```

See [COMPTIME_POLICY.md](COMPTIME_POLICY.md) for the capability classes.

[^1]: TOML escapes such as `"\u001b"` are decoded before the control-character check, which also rejects DEL, NUL and the C1 range (`U+0080`–`U+009F`). A key or value that could rewrite the trust prompt or an error message never reaches the terminal. The parser is the vendored [tomlc17](https://github.com/cktan/tomlc17) in `src/tomlc17.c`.

[^2]: A pin is one more line in the trusted list, `<path>` then a tab then `@sha256=<digest>`. The digest is taken from the same bytes cccc parses, so the file cannot change between the check and its use.
