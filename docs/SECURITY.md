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

- Comptime loads and stores are confined to VM memory, but the libc calls it is allowed to make, and the handles it passes to compiler builtins, are trusted.
- `--build` scripts run with your full permissions.
- A cloned repository's code can run when you compile it, so use `--sandbox` for code you did not write.

## `--sandbox`

`--sandbox` confines the whole cccc process with the operating system's own sandbox:

| Allowed | Denied |
|---|---|
| Reading files | Network access |
| Writing the `-o` directory (or the working directory when `-o` is omitted in a compile mode), `--deps-file`, `--url-cache-dir`, `--build-out-dir`, `--build-cache`, `/tmp` and `$TMPDIR` | Writing anywhere else |
| Running cccc itself and the host C compiler's toolchain | Running anything else |

```sh
cccc --sandbox prog.c                          # compile and run, confined
cccc --sandbox -c=native -o prog prog.c        # compile with the host cc, confined
cccc --sandbox --comptime-allow=all prog.c     # comptime still cannot escape
```

- The program a sandboxed `cccc prog.c` runs is confined too. Use `-c=native` and run the result outside the sandbox when it needs the network or other files.
- Child cccc processes started by `--build` and `--watch` inherit the sandbox.
- A `--build` script under `--sandbox` can only write its output directory, so steps that write elsewhere or run other commands fail.
- `--sandbox` cannot be combined with `--allow-url`.
- If the platform cannot enforce the sandbox, cccc stops with `error: --sandbox unavailable: …` rather than running unconfined.[^1]

Put `sandbox = true` in `~/.config/cccc/config.toml` to confine every run. A project config can turn it on but cannot turn it off. See [CONFIG.md](CONFIG.md).

## Running an untrusted repository

1. `cccc --sandbox prog.c` rather than a plain `cccc prog.c`.
2. Leave `--comptime-allow`, `--allow-url` and the config trust prompt (`.cccc.toml`) unapproved until you have read what they ask for.
3. Do not run `cccc --build build.c` on it without `--sandbox`: the script runs as a program.

[^1]: macOS uses `sandbox_init`; Linux uses landlock (kernel 5.13 or newer) for files and exec, plus a seccomp filter that refuses inet sockets. Reads are unrestricted and exec is allowed by toolchain directory, not by exact binary.
