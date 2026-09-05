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
cccc -c=native -o program a.c b.c c.c   # multiple inputs, one program
```

`-c=native` is whole-program: pass any number of source files in one
invocation and the front end links their ASTs into a single program before
serializing one temporary `.c` and spawning the host `cc` once. This is the
shape a [`CcccExecutable` build target](BUILD_MODE.md#target-kinds) drives.

The host compiler is `CCCC_NATIVE_CC` if set, else the first of `cc`, `clang`,
`gcc` found on `PATH`. To run the result, invoke it directly: `./program`.

### Flags forwarded to the host compiler

`-I`, `-i`/`--isystem`, `-D`, `-U`, `-L`, `-l`, `--std=`, and `-O<n>` are
passed through the front end to the host `cc`. `-c=native` additionally always
adds `-lm`, `-pthread`, and `-fsigned-char` (CCCC models `char` as signed on
every target, so the host build must match).

### How `--std=` is handled

The emitted C is a fixed **GNU C11** floor regardless of what `--std=` you
pass — the front end accepts the whole C23 superset and the serializer lowers
everything past C11 (`auto` → the resolved concrete type, single-argument
`static_assert` → two-argument `_Static_assert`, `nullptr` → `void *`,
`_BitInt(N)` → a fixed integer container, `[[gnu::…]]` → `__attribute__`, digit
separators and binary literals folded away). So `--std=` never changes what is
emitted; it only decides which `-std=` *spelling* to forward to the host
compiler, so that host doesn't choke on that fixed-floor output.

That spelling is chosen by probing the host `cc`:

- The `gnu<NN>` spelling of a standard is always tried before the strict ISO
  `c<NN>` one. The serializer emits GNU constructs (statement expressions,
  `__int128`, `case A ... B` range labels, `__asm__`) unconditionally, and a
  strict `-std=c<NN>` rejects them — CCCC's own front end only pedantic-warns.
- The probe never resolves to anything **older than C11** — the emitted C11
  floor (`_Atomic`, `_Thread_local`, `_Alignas`/`_Alignof`, `_Static_assert`,
  `_Complex`, all unconditional) needs at least that. A `--std=` naming an
  older standard (`c99`, `c89`, and their `gnu` spellings) still has its
  probe run, but against C11's own spellings instead — `--std=c99` forwards
  `-std=gnu11` (or the newest C11 spelling the host accepts), not a spelling
  of C99. This describes the emitted file honestly: it is C11 regardless of
  what `--std=` you passed, so the older standard's spelling would be a lie
  about the file the host is about to see.
- With an explicit `--std=` naming C11 or later, the probe only tries
  spellings of *that* standard (`c23`/`gnu23`/`c2x`/`gnu2x` for C23, and so
  on) — it never silently descends to an older standard than the one you
  named. With no `--std=` at all, it descends the ladder toward older
  standards (bottoming out at C11) and forwards the newest rung the host
  accepts, or nothing if the host has no usable `-std=` at all.
- If you pass an explicit `--std=` and the host compiler accepts **none** of
  the resulting standard's spellings (C11's, for a `--std=` naming C99 or
  C89), `-c=native` fails with an error naming the compiler and the
  spellings tried, rather than compiling for a different dialect.
- With no explicit `--std=`, a host that accepts no `-std=` spelling at all
  still gets nothing forwarded — but its own *default* dialect is then
  checked against the same C11 floor (one extra probe, only reached when the
  ladder already found nothing to forward), and `-c=native` fails rather
  than handing the emitted file to a host whose default dialect cannot
  parse it.

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

- A bitfield whose *declared type* is itself a `_BitInt` wider than 128 bits
  (e.g. `_BitInt(256) f : 193;`) -- a bit-field's type has no legal C
  spelling once it needs the multi-word container below (that container is a
  `struct`, and `struct T f : 193;` is as illegal as it looks). A plain
  (non-bitfield) object or value of the same width lowers fine; see below.
- `__builtin_decimal_to_chars` and `#include <decimal_math.h>`; any decimal
  construct at all in a `CCCC_HAS_DECIMAL=0` build.
- A VLA declared in a `for`-loop initializer (`for (int i = 0, v[n]; …)`).
- The VM-only source-map builtins `__builtin_pc_function_name` /
  `__builtin_pc_source_location`.

`_Decimal32/64/128` themselves pass through as GNU decimal syntax, which gcc
accepts and clang does not; CCCC emits a guarded `#error` preamble so clang
gives a clear diagnostic instead of a confusing one.

## Local variable hoisting

The emitted C flattens every local variable of a function to one C89-style
declaration list at the top of that function, with any initializer split out
into a separate assignment where it originally appeared. Block structure
(braces) is otherwise preserved, purely for readability — it carries no
scoping weight in the output.

If a local's source name happens to collide with a global (a variable,
function, or an FFI/host-library function) or a file-scope `typedef` that the
same function refers to elsewhere, CCCC renames the *local* — never the
global or the typedef, which may already have other references emitted
elsewhere — by appending a `__cccc_N` suffix. This means `-m`/`-c=generated`
output is not guaranteed to preserve every local's exact source spelling; if
you are diffing or grepping generated output for a local variable by name and
it doesn't appear verbatim, check for this suffix.

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

`_BitInt(N)` with `N > 128` lowers to an emitted `struct { unsigned long long
w[K]; }` container (`K` = `sizeof`/8) plus the same runtime helper functions
the VM itself uses for every width past 64 bits (`src/stdlib/wide_bitint.c`,
shared verbatim with the emitted output — see that file's own comment for
how) — every arithmetic/bitwise/comparison/cast operation lowers to a GNU
statement expression calling into it. `sizeof`/`_Alignof` match the VM
exactly (the container is size- and alignment-identical to CCCC's own
`_BitInt(N>128)` representation, unlike clang's or gcc's own native
`_BitInt`, which neither accepts this width range on every target this
project supports nor agrees with CCCC's layout where it does). The one
residual is the bitfield case above — a bitfield's own declared type still
has no lowering, since the container is a `struct` and a bit-field's type
must be a plain integer type.

## See also

- [HEADERS.md](HEADERS.md) — header resolution and host-header hand-off.
- [COVERAGE.md](COVERAGE.md) — the C language surface CCCC accepts.
- [BUILD_MODE.md](BUILD_MODE.md) — `--build` scripts can produce native
  targets, and a `CcccExecutable` target drives `-c=native` directly.
