# Native Output Differences

Where `-c=native`, `-m` and `-c=generated` output differs from the VM, and what they refuse.

## Refused

Constructs with VM-only semantics are a compile error rather than divergent C:

| Construct | Notes |
|---|---|
| Pointer in a union, or at a misaligned offset, of a struct with a wide `_BitInt` bit-field | See [wide bit-fields](#wide-_bitint-bit-fields) |
| `__builtin_decimal_to_chars`, `<decimal_math.h>`, any decimal construct without `CCCC_HAS_DECIMAL` | |
| `__builtin_pc_function_name`, `__builtin_pc_source_location` | VM source-map builtins |

`_Decimal32/64/128` pass through as GNU decimal syntax: gcc accepts it, and clang gets a guarded `#error` with a clear message.

## Output shape

| Topic | Behaviour |
|---|---|
| Local variables | Hoisted to one declaration list at the top of each function; initializers become assignments. Braces are kept for readability only. |
| Name clash | A local that collides with a global, function or typedef it also uses is renamed `name__cccc_N`; the global is never renamed. Grep for the suffix if a local seems missing. |
| Anonymous struct/union | Tag `__cccc_anon<N>` in `-m`/`-c=native`; every use names the same type |
| C11 anonymous members, header-owned types | Keep their inline spelling |
| `-c=generated` | Anonymous aggregates stay inline |

```c
static struct { int loc; void *fn; } buf[4];   // source
static struct __cccc_anon0 buf[4];             // output
```

## Behaviour differences

The emitted C behaves as the VM does, except:

| Construct | VM | Native |
|---|---|---|
| `asm(...)` | No-op; `--asm-passthru` calls it via FFI | Emitted verbatim, run by the host |
| File-scope `asm("...")` | No-op (`-Wignored-features`) | Emitted verbatim ([inline-asm.md](inline-asm.md#file-scope-asm)) |
| Extended `asm` / `asm goto` | No-op; outputs keep their value | Emitted verbatim with qualifiers, operands, clobbers, labels |
| `__builtin_return_address(n)` | VM bytecode offset | Real host address |
| `__builtin_dynamic_object_size` | Exact from the allocation header | Host builtin: exact only when the optimizer sees the allocation (`-O2`) |
| `__builtin_unreachable`, `__builtin_trap`, `__builtin_debugtrap` | Trap | `__builtin_trap()` |
| Call through an invalid function pointer | `invalid indirect call target`, exit 255 | `SIGSEGV`/`SIGBUS` |
| Integer divide by zero (no safety flags) | `DIVISION BY ZERO`, exit 255 | aarch64 yields 0; x86_64 raises `SIGFPE` |
| `__builtin_alloca` with a VLA in a loop | Fresh address per call | Host-defined; clang `-O0` may reuse a slot |
| Tail calls | Constant stack | Best effort; use `-O2` |
| `void` entry function falling off the end | Exit 0 | Whatever the ABI left in the return register |
| `%L` on integer conversions | `L` is `ll` | Host libc: glibc agrees, BSD/Apple use 32 bits |
| Zero-sized union through varargs | Consumes no slot | Host ABI passes a register; the read is garbage |
| `ioctl` requests | Allowlist | Real `ioctl`, no allowlist |
| `constructor(N)` priority | Honoured | Honoured by clang and Linux gcc; rejected by Homebrew gcc on macOS |
| `sizeof`/`_Alignof` of a host-header type in a bit-field width or initialized-global image | Folded against CCCC's layout | Stays folded |

## Runtime shims

| Feature | Emitted as |
|---|---|
| `_BitInt(65..128)` | Aligned 16 on every target (like `__int128`); differs from clang/gcc on x86_64, so do not share such objects |
| `_BitInt(N>128)` | A `struct { unsigned long long w[K]; }` plus the VM's own helper functions; size and alignment match the VM |
| C23 `<math.h>` functions host libms lack (`exp10`, `sinpi` family, `fmaximum` family, `totalorder`, `canonicalize`, payload and `fromfp` families) | `static __cccc_native_<name>`, the same code the VM runs; `l` variants compute in `double` |
| `memset_explicit`, `memalignment`, `free_sized`, `free_aligned_sized` | Static functions from the same shared runtime |

## Variable-length array parameters

A parameter whose pointee length reads an earlier parameter (`int (*a)[n]`, `int a[][n]`) is spelled by position:

| Where | Spelling |
|---|---|
| Prototype, function-pointer type | `int (*a)[*]` |
| Definition | `int (*a)[n]`, with the real parameter names |
| Call-site cast, compiler temporary | `void *` |

A length with side effects (`int (*a)[g()]`) is evaluated once per call, as in the VM. The parameter is taken as `void *__cccc_p_a` and declared as `int (*a)[tmp] = __cccc_p_a;` after the lengths are computed, so the prototype of such a function also says `void *`.

`[*]` is accepted in source in a prototype that is not a definition:

```c
int f(int n, int a[n][*]);   // ok
int f(int n, int a[n][*]) {  // error: star modifier used outside of function prototype
```

## Wide `_BitInt` bit-fields

A bit-field of a `_BitInt` wider than 128 bits has no C spelling, so the struct becomes a byte array in CCCC's layout and members are read and written at CCCC offsets:

```c
struct S { int tag; _BitInt(256) f : 193; };
struct S s;
s.f = 3;   // __cccc_bitfield_insert(...) on the bytes of s
s.tag = 7; // (*(int *)((unsigned char *)&s + 0)) = 7
```

`sizeof`, `_Alignof` and offsets match the VM, including under `packed`. Address-of members, copies, by-value arguments and returns, nested and anonymous members, flexible array members and global initializers all work.

**Limitations:** a global initializer holding a pointer inside a union, or at a misaligned (`packed`) offset, is refused ([#1472](https://todo.sr.ht/~takeiteasy/cccc/1472)).
