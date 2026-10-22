# Getting started

Install `cccc`, run a C file on the VM, and pick a mode.

## Install

Download a release build from [github.com/takeiteasy/cccc/releases](https://github.com/takeiteasy/cccc/releases), or build from source (needs a C compiler, `make` and libffi):

```sh
git clone --recurse-submodules https://github.com/takeiteasy/cccc
cd cccc && make        # produces ./cccc
```

Supported: macOS (aarch64) and Linux (aarch64, x86_64). See [install.md](install.md) for optional dependencies.

## Run a file

`cccc file.c` compiles to bytecode and runs `main()` on the built-in VM. The process exit code is `main`'s return value.

```c
#include <stdio.h>
int main(void) { puts("hello"); return 0; }
```

```sh
./cccc hello.c        # hello
```

## Four ways to run one file

| Command | What happens |
|---|---|
| `cccc file.c` | Run `main()` on the VM |
| `cccc -c=native -o out file.c` | Lower to C and build a native binary with `cc` ([native.md](native.md)) |
| `cccc --testing file.c` | Run the `@test` functions ([testing.md](testing.md)) |
| `cccc --build file.c` | Run the `@build_target` recipes ([build.md](build.md)) |

All four work on the same file: tests, build recipes and `main()` live side by side. `--run` runs `main()` after a passing `--testing` or `--build`.

## Compile-time code

A function marked `@comptime` is ordinary C that runs *while compiling*. Call sites can be rewritten, and whole declarations generated. See [macros.md](macros.md).

## Safety checks

Runtime checks are on a scale from `-0` (none) to `-3` (maximum):

```sh
./cccc -2 file.c       # use-after-free, overflows, uninitialized reads, ...
```

See [safety.md](safety.md).

## Next

| To... | Read |
|---|---|
| Generate code from a type definition | [derive.md](derive.md) |
| Write tests | [testing.md](testing.md) |
| Replace `make` for your project | [build.md](build.md) |
| Explore interactively | [repl.md](repl.md), [debugger.md](debugger.md) |
| Set defaults per project | [config.md](config.md) |
