# GPU Kernel Attribute Subset

Address spaces, vector size and visible lanes.

`[[cccc::kernel]]` checks that a function could run as a GPU kernel. The check
runs on the normal CPU build, so kernel-shaped code is caught without a GPU
toolchain. It does not generate GPU code. To run a kernel over a grid of
work-items in the VM, see [kernels.md](kernels.md); for OpenCL C source, see
[opencl.md](opencl.md).

```c
#include <stdatomic.h>

static inline unsigned bump(_Atomic unsigned *p) {
    return atomic_fetch_add_explicit(p, 1u, memory_order_relaxed);
}

[[cccc::kernel]] static void step(_Atomic unsigned *counters, unsigned i) {
    bump(&counters[i]);
}
```

Mark the entry function only. Every function it calls, directly or through
other functions, is checked as well, so shared helpers need no mark. A
violation is an error that names the call path:

```
error: calling 'puts', which has no definition in this file, is not allowed in kernel code (step -> log -> emit)
```

| Rejected | Example |
|---|---|
| Recursion, direct or mutual | `int f(int n) { return f(n - 1); }` |
| Function pointers: calls through one, or taking a function's address | `int (*f)(int) = g;` |
| A call to a function with no body in the file, which includes libc | `malloc(4)`, `puts("x")` |
| Variadic functions, `va_start`, `va_arg` | `int f(int n, ...)` |
| Variable-length arrays, `alloca` | `int a[n];` |
| Inline assembly | `__asm__("nop")` |
| `goto`, including computed goto | `goto out;` |
| `double` and `long double` | `double d = 1.0;` |
| Non-`const` global, static local, thread-local and string literal use | `static int n;` |
| Blocks and nested functions | `^{ ... }` |

Allowed: `float` and integer types, structs passed by value, local arrays,
loops, `const` tables, enum and macro constants, and the `<stdatomic.h>`
operations.

Callees may be defined after the marked function. The attribute has no effect
on `-c=native` output.

Guard the attribute in code that other compilers also build:

```c
#if defined(__has_c_attribute)
#  if __has_c_attribute(cccc::kernel)
#    define KERNEL_CHECK [[cccc::kernel]]
#  endif
#endif
#ifndef KERNEL_CHECK
#  define KERNEL_CHECK
#endif
```

The outer `defined` test keeps the header valid in C++ and Metal, where
`__has_c_attribute` does not exist and a bare `#if __has_c_attribute(...)` is
an error.

## Address Spaces

GPU dialects need to know which memory a pointer refers to. Mark the pointed-to
type the way you would write `const`; CPU builds, the VM and C output ignore
the mark, so the same source still builds with other compilers.

```c
[[cccc::kernel]] static void blur([[cccc::global]] float *out,
                                  [[cccc::constant]] const float *taps,
                                  [[cccc::local]] float *scratch) {
    [[cccc::local]] float tile[64];
    tile[0]    = taps[0];
    scratch[0] = tile[0];
    out[0]     = scratch[0];
}
```

| Attribute | Memory | Pointer example |
|---|---|---|
| `global` | Device buffers | `[[cccc::global]] float *p` |
| `local` | Workgroup-shared | `[[cccc::local]] float tile[64]` |
| `constant` | Read-only | `[[cccc::constant]] const Params *p` |
| `private` | Per work-item (the default) | `[[cccc::private]] float *p` |
| `generic` | Any of global, local, private | `[[cccc::generic]] float *p` |

Write the attribute in the declaration specifiers, before or after the type
name. It qualifies the pointed-to type: `[[cccc::global]] int *p` is a pointer
to global `int`. After the `*` it is an error.

`kernel`, `global`, `local`, `constant`, `private` and `generic` also accept the
spelling `__name__`, as in `[[cccc::__global__]]`. The two mean the same; the
underscored one cannot be rewritten by a macro called `global`, which is what
[OpenCL C](opencl.md) defines.

**Unmarked pointers.** An unmarked pointer parameter of a kernel entry points
to global memory. An unmarked pointer parameter of a helper takes the space its
callers pass, so `bump(&counters[i])` from an entry passes global memory and
`bump` needs no mark. If callers pass different spaces, mark the parameter
`[[cccc::generic]]` or split the helper. Every other unmarked pointer is
private. Pointers inside a struct passed by value are not inferred; mark them.

**Conversions.** Inside kernel code, assignment, initialisation, arguments,
returns and casts must keep the space:

| From | To | Allowed |
|---|---|---|
| Same space | Same space | Yes |
| `global`, `local`, `private` | `generic` | Yes |
| `constant` | `generic` | No |
| `generic` | A named space | Only with an explicit cast |
| Any other pair | | No |

```
error: converting a pointer from the global to the private address space is not allowed in kernel code (step)
```

**Other rules.**

- Writing through a `constant` pointer, including an atomic update, is an error.
- `local` objects (not pointers) are only allowed in a kernel entry's body.
  `global`, `constant` and `generic` objects cannot live on the stack.
- A file-scope object can be `constant`; any other file-scope object is
  rejected as a global variable.
- A kernel entry's pointer parameter cannot be `private` or `generic`.

**Limitations:**

- Callees must be defined in the same file; a call into another file is rejected.
- One rule set covers both Metal and CUDA, so code valid on only one of them
  (for example `double` on CUDA) is rejected.

## Vector Size

`__attribute__((vector_size(N)))` or `[[gnu::vector_size(N)]]` turns an integer
or floating-point scalar into an `N`-byte vector. It works before the type, after
the type specifier, after the declarator, and in cast, compound-literal and
`sizeof` type-names. Before or after the specifier, it applies to the base type
of every declarator. After a declarator, it applies to the innermost scalar,
through pointers, arrays and function return types:

```c
#define VS __attribute__((vector_size(16)))

VS int a, *p;               // a: vector; p: pointer to a vector
int [[gnu::vector_size(16)]] b = {1, 2, 3, 4};
int c VS = {1, 2, 3, 4};    // after the declarator
int *q VS;                  // pointer to a vector
int arr[2] VS;              // array of two vectors
int mk(void) VS;            // function returning a vector
int n = sizeof((VS int){0});  // 16
```

Vector alignment follows the host ABI, so locals, struct members and
`-c=native` output agree with the host compiler:

| Host | alignment of a 32- or 64-byte vector (layout, `__alignof__`) |
|---|---|
| aarch64 (macOS, Linux), macOS x86_64 | 16 |
| Linux x86_64 | the vector's size |

C11 `_Alignof(type)` and `_Alignas(type)` differ from that on Linux x86_64
under `--compiler-family=gcc`: gcc caps them at 16 for a wide vector, or for a
struct, union or array holding one, unless the type carries `aligned(N)`.
`-mavx` and `-mavx2` raise the cap to 32, `-mavx512f` to 64; CCCC accepts the
three flags (x86_64 only; elsewhere they warn and are ignored), defines
`__AVX__`, `__AVX2__` and `__AVX512F__` to match, and forwards them to the
`-c=native` compiler. `__alignof__`, `_Alignof expr` and
`--compiler-family=clang` always give the vector's size.

```c
typedef float v8f32 __attribute__((vector_size(32)));
_Alignof(v8f32);      // 16 (gcc, Linux x86_64); 32 with -mavx
__alignof__(v8f32);   // 32
```

Use `__alignof__(type)` when checking where a vector-bearing member lands.

## Visible Lanes

`__attribute__((cccc_visible_lanes(N)))` on a `vector_size` typedef makes the
vector store all its lanes but expose only the first `N` to swizzles and
vector literals. Both exist only in an [OpenCL C](opencl.md) input, so the
attribute has no visible effect in plain C. `N` must be less than the lane
count, or it is ignored. OpenCL C uses it for the three-lane types:

```c
typedef float float3 __attribute__((vector_size(16), cccc_visible_lanes(3)));

// sizeof(float3) == 16; a literal takes three components; .w is an error
```

Arithmetic still runs on every stored lane.

---
