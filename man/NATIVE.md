# Native Compilation (`-c=native`)

By default CCCC compiles your program to bytecode and runs it in the built-in
VM. `-c=native` instead serializes the fully-resolved program back to portable
C and hands it to a real host compiler, producing a normal native executable —
full toolchain performance, real system libraries, no VM overhead.

The front end (preprocessor, parser, compile-time macros) is identical to a
normal VM build. Only what happens *after* the AST is resolved changes.

Supported host platforms: macOS (aarch64) and Linux (aarch64, x86_64).

## The four output modes

| Flag | What it does | Typical use |
|---|---|---|
| *(none)* | Compile to VM bytecode, run it in the interpreter | Default: portability, sandboxing, the debugger, the safety suite, iteration with no system compiler |
| `-m` / `--dump-expanded` | Serialize the AST to portable C, print to stdout; don't compile | Seeing what CCCC would hand a host compiler |
| `-c=generated` | Serialize to C, write to a file (`-o`, default `./a.c`); don't compile | Build systems that drive their own `cc` |
| `-c=native` | Serialize to C, compile + link a real executable (`-o`, default `./a.out`) | The production path |

All three serializing modes run the same tree walk and are subject to
everything in this document identically — "under `-c=native`" below means all
three unless stated otherwise.

`--emit-cccc` is a dialect switch layered on top: by default the serializer
strips CCCC-only syntax (`@`-attribute shorthand, checked-pointer qualifiers,
…) down to portable C; `--emit-cccc` preserves it, for round-tripping CCCC's
own extended syntax back through its own front end. `-c=native --emit-cccc`
therefore needs `CCCC_NATIVE_CC` pointed at a `cccc`-aware compiler.

## Invoking it

```sh
cccc -c=native -o program program.c
CCCC_NATIVE_CC=clang cccc -c=native -o program program.c
```

The host compiler is `CCCC_NATIVE_CC` if set, else the first of `cc`, `clang`,
`gcc` found on `PATH`. To run the result, invoke it directly: `./program`.

### Flags forwarded to the host compiler

`-I`, `-i`/`--isystem`, `-D`, `-U`, `-L`, `-l`, `--std=`, and `-O<n>` are
passed through the front end to the host `cc`. `-c=native` additionally always
adds `-lm`, `-pthread`, and `-fsigned-char` (CCCC models `char` as signed on
every target, so the host build must match).

`--std=` is only used as a flag-spelling probe; the emitted dialect is a fixed
**GNU C11** floor regardless of what `--std=` you pass.

### Rejected in this mode

VM-only options have no meaning once a host compiler takes over and are
rejected: bytecode output, the disassembler (`-d`), the debugger (`-g`), the
profiler (`--vm-profile`), the REPL (`-r`), and the `-0`…`-3` safety levels.
Run those against the default VM build.

Plain runtime `#include <stdio.h>` and other real system headers work here —
see [HEADERS.md](HEADERS.md) for how header resolution and host-header
hand-off change under native mode.

## What `-c=native` refuses to lower

Where a construct has genuine VM-specific semantics with no faithful host
translation, CCCC emits a diagnosed compile error rather than silently
divergent C. The main cases:

- `_BitInt(N)` with `N > 128` (the host backends have no multi-word lowering;
  `N ≤ 128` maps to `__int128`).
- `__builtin_decimal_to_chars` and `#include <decimal_math.h>`; any decimal
  construct at all in a `CCCC_HAS_DECIMAL=0` build.
- A VLA declared in a `for`-loop initializer (`for (int i = 0, v[n]; …)`).
- The VM-only source-map builtins `__builtin_pc_function_name` /
  `__builtin_pc_source_location`.

`_Decimal32/64/128` themselves pass through as GNU decimal syntax, which gcc
accepts and clang does not; CCCC emits a guarded `#error` preamble so clang
gives a clear diagnostic instead of a confusing one.

## Serialized-output divergences

Everywhere else the rule is that the emitted C behaves as the VM behaves. A
handful of constructs cannot fully honour that and are listed here rather than
left to be discovered:

| Construct | Under the VM | Under `-c=native` |
|---|---|---|
| `asm(...)` | no-op by default; `--asm-passthru` compiles + calls via FFI | emitted verbatim and executed by the host binary |
| `__builtin_return_address(n)` | a VM bytecode offset cast to `void*` | a real host return address — same meaning, unrelated numeric value |
| `__builtin_dynamic_object_size` | reads the VM allocation header, always exact | the host builtin — exact only when the host optimizer can see the allocation (`-O2`), else "unknown" |
| `__builtin_unreachable` / `__builtin_trap` / `__builtin_debugtrap` | all trap | all emit `__builtin_trap()` |
| `__builtin_alloca` sharing a block with a real VLA, in a loop | each call gets a fresh address, live until the frame returns | host-compiler-defined; clang -O0 may reuse one slot per iteration |
| tail calls (`CALLT`) | guaranteed constant stack space | best-effort — the host cc's own heuristic; pass `-O2` to rely on it |
| a `void` entry function falling off its end | always exits 0 | exit status is whatever the ABI left in the return register |
| `%L` length modifier on an integer conversion | `L` == `ll`, a 64-bit slot (glibc rule) | the host libc decides — glibc agrees; BSD/Apple libc treats it as a 32-bit slot |
| a zero-sized union passed through varargs | consumes no argument slot; reads as `0` | the host ABI passes it in a register; the conversion reads garbage |
| `ioctl` request codes | rejected unless on an explicit allowlist | the real host `ioctl`, no allowlist |
| `__attribute__((constructor(N)))` priority | honoured in init/fini ordering | honoured by clang and by gcc on Linux; rejected by Homebrew gcc on macOS |
| `sizeof`/`_Alignof` of a host-header type reached through a bitfield width or an initialized global's byte image | folds against CCCC's type projection | stays folded (re-materializing it could desync the host's layout of the same struct) |

`_BitInt(65..128)` is aligned 16 on every target (matching the `__int128` it
lowers to), which differs from clang's/gcc's own native `_BitInt` alignment on
x86_64 — so don't assume a cccc-compiled and a clang-compiled `_BitInt(65..128)`
object share layout on that target. `__int128` itself has no such divergence.

## See also

- [HEADERS.md](HEADERS.md) — header resolution and host-header hand-off.
- [COVERAGE.md](COVERAGE.md) — the C language surface CCCC accepts.
- [BUILD_MODE.md](BUILD_MODE.md) — `--build` scripts can produce native targets.
