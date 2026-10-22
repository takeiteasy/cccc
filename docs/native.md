# Native Compilation (`-c=native`)

Serialize the resolved program back to portable C and build it with a real host compiler: full toolchain speed, real system libraries, no VM.

The front end (preprocessor, parser, comptime) is the same as a VM build; only what happens after the AST is resolved changes. Hosts: macOS (aarch64), Linux (aarch64, x86_64).

## Output modes

| Flag | Does | Use |
|---|---|---|
| *(none)* | Compile to bytecode, run in the VM | Default: debugger, safety suite, no system compiler needed |
| `-m` / `--dump-expanded` | Print the serialized C to stdout | See what the host compiler would get |
| `-c=generated` | Write C to `-o` (default `./a.gen.c`) | Build systems that run their own `cc` |
| `-c=native` | Compile and link (`-o`, default `./a.out`) | The production path |
| `-c=object` | Compile to an object (`-o`, default `<input>.o`) | Libraries, custom linking |

The serializing modes all follow the rules on this page and in [native-differences.md](native-differences.md). `--emit-cccc` keeps CCCC-only syntax (`@` shorthand, checked-pointer qualifiers) instead of lowering it, which needs `CCCC_NATIVE_CC` set to a `cccc`-aware compiler.

## Invoking

```sh
cccc -c=native -o program program.c
CCCC_NATIVE_CC=clang cccc -c=native -o program program.c
cccc -c=native -o program a.c b.c c.c      # several inputs, one program
```

The program is whole: all inputs are linked into one AST, serialized to one temporary `.c`, and the host compiler runs once. The host is `CCCC_NATIVE_CC`, else the first of `cc`, `clang`, `gcc` on `PATH`.

## Flags

| Flag | Effect |
|---|---|
| `-I`, `-i`/`--isystem`, `-F`, `--iframework`, `-D`, `-U`, `-L`, `-l`, `-O<n>` | Forwarded to the host compiler |
| `--std=` | Only picks the `-std=` spelling forwarded ([native-std.md](native-std.md)); the output is always GNU C11 |
| `--link-arg=ARG` | Appended verbatim to the link line (repeatable; `-c=native` only) |
| `--deps-file=PATH` | Writes a make-style dependency rule for every file the front end opened[^deps] |
| `--no-emit-tests` | Drops `[[cccc::test]]` bodies from the output |

Always added: `-lm`, `-pthread`, `-fsigned-char` (CCCC's `char` is signed everywhere) and `-w`. Warnings come from CCCC's `-W` options, not the host. A program defining its own library-named function (`double fabs(double)`) also gets `-fno-builtin-fabs`, as do the same flags when compiling `-m` output by hand.

```sh
cccc -c=native main.c --link-arg=helper.o -o app
cccc -c=native main.c --link-arg=-framework --link-arg=CoreServices -o app
```

### Object files

```sh
cccc -c=object lib.c                 # ./lib.o
cccc -c=object a.c b.c -o ab.o       # several inputs need -o
cc main.c lib.o -o app
```

`-l`, `-L` and `--link-arg` are errors with `-c=object`, `#pragma cccc link` libraries are dropped, and `--test-run`/`--testing=native` cannot be combined with it. A `--build` script gets objects from a [`CcccObject` target](build-targets.md#target-kinds).

## Tests and build recipes in the same file

| Under plain `-c=native` | Behaviour |
|---|---|
| `[[cccc::test]]` bodies | Emitted, but each `Assert*` prints "called outside a test run" and returns |
| `[[cccc::build]]` / `[[cccc::build_target]]` | Always dropped |

See [build-recipes.md](build-recipes.md#build-recipes-in-a-program-file) and [testing.md](testing.md).

## Options that do not apply

| Option | In native modes |
|---|---|
| `-d`, `-g`, `-r`, `-0`…`-3`, bytecode output | Rejected: run these on the VM build |
| `--bounds-checks`, `--uaf-detection`, other VM runtime checks | Dropped with a warning |
| `--checked-pointers` | Enforced in the emitted C ([safety-checked-interfaces.md](safety-checked-interfaces.md#native-output)) |
| `--vm-profile` | Reports only comptime code that ran during the build ([macros-debugging.md](macros-debugging.md#profiling-comptime-code)) |

System headers and URL includes work as described in [headers.md](headers.md) and [headers-native.md](headers-native.md).

## See also

[native-differences.md](native-differences.md) · [native-std.md](native-std.md) · [headers.md](headers.md) · [coverage.md](coverage.md) · [build.md](build.md)

[^deps]: Lists command-line sources, `-include` files and every `#include` resolved from disk; bundled headers served from memory do not appear. It never runs the program, and `--build` uses it for `CcccExecutable` header tracking ([build-incremental.md](build-incremental.md#incremental-builds-and-header-dependencies)).
