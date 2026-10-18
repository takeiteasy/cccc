# Security Model

cccc runs code while it compiles. This page says what runs, who can allow what, and how to compile code you do not trust.

## The gist

| Phase | Default | To widen it |
|---|---|---|
| Running your program | Unrestricted | `--disable-ffi`, `--ffi-allow`, `--ffi-deny` narrow it |
| `[[cccc::comptime]]` code | Safe libc only (strings, math, allocation) | `--comptime-allow=<classes>` |
| Network downloads (`#include <https://…>`) | Off | `--allow-url` |
| The whole process | Unconfined | `--sandbox` narrows it |

Grants come from the command line or from a config you approved. Source code, attributes and comments cannot grant anything, so a cloned project cannot widen its own permissions.

## What reaches outside the build

| Path | Needs | Notes |
|---|---|---|
| Comptime `fopen`, `system`, `socket`, `getenv`, `dlopen`, … | `--comptime-allow=fs,exec,net,env,dlopen` | See [COMPTIME_POLICY.md](COMPTIME_POLICY.md) |
| `asm` statements under `--asm-passthru` | `--comptime-allow=all` at comptime | Thunks run host assembly |
| `#include`/`#embed <https://…>` | `--allow-url` | A URL already in the cache resolves without it; `__has_include` never downloads without it |
| `dlopen` in the running program | Not being under `--disable-ffi` | `--disable-ffi` makes it return `NULL` |
| `#pragma cccc link(lib)`, `-l` | Nothing | Loaded when the program runs, never while compiling (`-c=native`, `-m`, `-E`) |
| `#embed "file"` | Nothing | A plain file read, like `#include` |
| `--watch` | Nothing extra | Restarts cccc with the same arguments, so the same policy applies |
| `cccc --build build.c` | Nothing | The script is a program: `RunCustom` runs commands by design |

## Not isolation

The allowlist prevents accidents. It is not a sandbox:

- Comptime loads, stores and the safe libc calls are confined to VM memory. Libc calls you grant with `--comptime-allow` (`fs`, `exec`, ...) and the handles passed to compiler builtins are trusted.
- `--build` scripts run with your full permissions.
- A cloned repository's code can run when you compile it, so use `--sandbox` for code you did not write.

## `--sandbox`

`--sandbox` confines the whole cccc process with the operating system's own sandbox:

| Allowed | Denied |
|---|---|
| Reading the inputs and their directories, the working directory, `-I`/`-isystem`/`-F`/`-L`/`--sysroot` paths, `--sandbox-read` paths, the cccc config, and the system and toolchain directories | Reading anything else, such as `~/.ssh` |
| | Network access |
| Writing the `-o` directory (or the working directory when `-o` is omitted in a compile mode), `--deps-file`, `--url-cache-dir`, `--build-out-dir`, `--build-cache`, `/tmp` and `$TMPDIR` | Writing anywhere else |
| Running cccc itself, the `--sandbox-exec` programs and, in `-c=native`, `--build` and `--asm-passthru` runs, the exact programs the host C compiler runs | Running anything else, including the C compiler in every other run |

```sh
cccc --sandbox prog.c                          # compile and run, confined
cccc --sandbox -c=native -o prog prog.c        # compile with the host cc, confined
cccc --sandbox --comptime-allow=all prog.c     # comptime still cannot escape
cccc --sandbox --sandbox-read=/data prog.c     # also let the run read /data
```

- Running from `$HOME` makes all of `$HOME` readable, because the working directory is always readable. Run from the project directory.
- `--sandbox-read=<path>` (repeatable) adds a readable file or directory. It is a trust option, so a project `.cccc.toml` needs approval to use it.
- A `--build` run probes `--build-cc=` (else `CCCC_BUILD_CC`, else `cc`). A compiler a build script sets per target with `SetToolchain` is only known after the sandbox closes, so name it with `--sandbox-exec=<cc>` and its install directory with `--sandbox-read=<dir>`.
- `--sandbox-exec=<path>` (repeatable) adds a program, or a directory of programs, the run may start. It is a trust option too. Use it for a toolchain the automatic grants miss.

- The program a sandboxed `cccc prog.c` runs is confined too. Use `-c=native` and run the result outside the sandbox when it needs the network or other files.
- Child cccc processes started by `--build` and `--watch` inherit the sandbox.
- A `--build` script under `--sandbox` can only write its output directory, so steps that write elsewhere or run other commands fail.
- `--sandbox` cannot be combined with `--allow-url`.
- If the platform cannot enforce the sandbox, cccc stops with `error: --sandbox unavailable: …` rather than running unconfined.[^1]

Put `sandbox = true` in `~/.config/cccc/config.toml` to confine every run. A project config can turn it on but cannot turn it off. See [CONFIG.md](CONFIG.md).

## Running an untrusted repository

1. `cccc --sandbox prog.c` rather than a plain `cccc prog.c`.
2. Leave `--comptime-allow`, `--allow-url` and the config trust prompt (`.cccc.toml`; `--trust-hash` makes any later edit ask again) unapproved until you have read what they ask for.
3. Do not run `cccc --build build.c` on it without `--sandbox`: the script runs as a program.

[^1]: macOS uses `sandbox_init`; Linux uses landlock (kernel 5.13 or newer) for files and exec, plus a seccomp filter that refuses inet sockets. Reads use `file-read-data` rules on macOS and landlock read rights on Linux; file names and metadata stay visible. The programs allowed to run come from `cc -###` and `-print-prog-name`, and the compiler's own directories from `-print-search-dirs`; a run that never reaches the compiler skips this probe. When the probe finds no programs, cccc warns and allows the toolchain directories instead. When an allowed program is a script (Xcode's `as` is a zsh script), its `#!` interpreter and `realpath`, `dirname` and `basename` are allowed too: macOS lets the interpreter start only as a script's interpreter, while Linux allows it as an ordinary program. On Linux the readable part of `/etc` is `ld.so.cache`, the `ld.so.conf` tree, `nsswitch.conf`, `localtime`, `passwd` and `group`.
