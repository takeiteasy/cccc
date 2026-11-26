# Checking `-E` output against a compiler

`--output-compiler=gcc` or `=clang` reports what `-E` prints that the named compiler cannot read, so a dump compiles with the compiler it is for. `any` (the default) checks nothing.

```sh
cccc -E --output-compiler=gcc overloads.c
# error: 'overloadable' is not supported by gcc and is dropped [-Wtarget-compiler]
```

| What | Where it is found | In the output |
|---|---|---|
| [Attributes](#attributes) | While printing | Dropped |
| [Builtins](#builtins) | While printing | Printed |
| [Nested functions and blocks](#nested-functions-and-blocks) | A parse of the input after printing | Printed |

All three are `-Wtarget-compiler` ([warnings.md](warnings.md)):

| Option | A construct the compiler lacks |
|---|---|
| (default) | Error |
| `-Wno-error=target-compiler` | Warning |
| `-Wno-target-compiler` | Silent |

Without `-E` the flag warns and does nothing. It cannot be combined with `--attr-target=msvc`.

## Attributes

An attribute the compiler lacks is dropped from the output. CCCC attributes, which `-E` drops anyway, and names cccc does not know are not checked.

| Attributes | gcc | clang |
|---|---|---|
| `overloadable`, `enable_if`, `diagnose_if`, `pass_object_size`, `pass_dynamic_object_size` | No | Yes |
| `optimize`, `designated_init` | Yes | No |
| `no_unique_address` | No | No |
| Every other attribute cccc knows, including `unavailable` | Yes | Yes |

`__has_attribute` and `__has_c_attribute` answer as the named compiler does, so `#if __has_attribute(overloadable)` takes its `#else` branch under `gcc`. Under `--attr-target=c23`, `unavailable` prints as `[[clang::unavailable]]`, or `[[gnu::unavailable]]` with `--output-compiler=gcc`, since gcc has no `clang::` namespace. <!-- attr-ns: ok -->

## Builtins

A builtin the compiler lacks is reported where it is written, and still printed.

| Builtins | gcc | clang |
|---|---|---|
| `__builtin_shuffle`, `__builtin_infd32/64/128`, `__builtin_nand32/64/128` | Yes | No |
| `__builtin_assume`, `__builtin_debugtrap`, `__builtin_os_log_format`, `__builtin_os_log_format_buffer_size` | No | Yes |

`__has_builtin` answers as the named compiler, so guarded code takes the compiler's own branch. `__builtin_convertvector` is in both.

## Nested functions and blocks

gcc has nested functions and no blocks; clang has blocks and no nested functions.

| Written | gcc | clang |
|---|---|---|
| `int g(int y) { ... }` inside a function | Yes | No |
| `^(int y) { ... }`, `int (^b)(int)` | No | Yes |

Finding them needs the parser, so `--output-compiler=gcc` or `=clang` also parses the input after printing it. Only target findings are reported from that parse: a parse error is not, and it stops the search, so text that is not valid C is printed unchanged. Code the compiler generates, such as an OpenMP region or a template instance, is not a nested function.
