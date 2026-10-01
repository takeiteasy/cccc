# OpenCL C

`cccc kernel.cl` reads OpenCL C 1.2 kernels and runs them on the
[kernel model](KERNELS.md): in the VM with its bounds and safety checks, or on
CPU threads under `-c=native`. Host code in the same file starts a kernel with
`cccc_launch`.

```c
__kernel void vadd(__global int *out, __global const int *a,
                   __global const int *b) {
    size_t i = get_global_id(0);
    out[i]   = a[i] + b[i];
}

int main(void) {
    int a[8] = {1, 2, 3, 4, 5, 6, 7, 8}, b[8] = {8, 7, 6, 5, 4, 3, 2, 1}, out[8];
    cccc_launch(vadd, CCCC_RANGE(8), CCCC_RANGE(4), out, a, b);
    return out[0];   // 9
}
```

```sh
cccc vadd.cl                    # VM
cccc -c=native vadd.cl -o vadd  # CPU threads
```

## Selecting OpenCL C

| Input | Dialect |
|---|---|
| `file.cl` | OpenCL C |
| `-x cl file.c` | OpenCL C |
| `-x c file.cl` | C |
| `-cl-std=CL1.2` | Accepted; OpenCL C 1.2 is the only version |

`-x` applies to every input of the invocation. Without it each file is read by
its extension, so `cccc kernels.cl host.c` compiles one OpenCL input and one C
input together. The OpenCL keywords below do not leak into the C input.

An OpenCL input defines `__OPENCL_C_VERSION__` as `120` and `CL_VERSION_1_2`.
It includes `<cccc/kernel.h>`, so `cccc_launch`, `CCCC_RANGE` and `CCCC_LOCAL`
are available without an `#include`.[^prelude]

## Qualifiers

Both spellings of each qualifier work. They map onto the attributes in
[Address Spaces](ATTRIBUTES.md#address-spaces), whose rules apply unchanged.

| OpenCL C | cccc |
|---|---|
| `__kernel`, `kernel` | `[[cccc::kernel]]` |
| `__global`, `global` | `[[cccc::global]]` |
| `__local`, `local` | `[[cccc::local]]` |
| `__constant`, `constant` | `[[cccc::constant]]` |
| `__private`, `private` | `[[cccc::private]]` |

These names are reserved in an OpenCL input, as in OpenCL C. A variable cannot
be called `local`, and `[[cccc::local]]` is written `[[cccc::__local__]]` or
with the OpenCL spelling.[^underscore]

A kernel pointer parameter with no qualifier is global. A `__constant` object
at file scope is a lookup table:

```c
__constant int taps[4] = {1, 3, 3, 1};

__kernel void blur(__global int *out, __global const int *in) {
    size_t i = get_global_id(0);
    out[i]   = in[i] * taps[i % 4];
}
```

## Work-item functions

| OpenCL C | Same as |
|---|---|
| `get_global_id(d)` | `cccc_global_id(d)` |
| `get_local_id(d)` | `cccc_local_id(d)` |
| `get_group_id(d)` | `cccc_group_id(d)` |
| `get_global_size(d)` | `cccc_global_size(d)` |
| `get_local_size(d)` | `cccc_local_size(d)` |
| `get_num_groups(d)` | `cccc_num_groups(d)` |
| `get_work_dim()` | `cccc_work_dim()` |
| `get_global_offset(d)` | `0` |
| `barrier(flags)` | `cccc_barrier(flags)` |
| `mem_fence(flags)`, `read_mem_fence`, `write_mem_fence` | a sequentially consistent fence |

`CLK_LOCAL_MEM_FENCE` and `CLK_GLOBAL_MEM_FENCE` are the `CCCC_*_FENCE` values.

## Local memory

A `__local` array in a kernel body is shared by a work-group. A `__local`
pointer parameter takes its size at the launch with `CCCC_LOCAL`:

```c
__kernel void group_sum(__global const int *in, __global int *sums,
                        __local int *tile) {
    size_t l = get_local_id(0);
    tile[l]  = in[get_global_id(0)];
    barrier(CLK_LOCAL_MEM_FENCE);
    for (size_t s = get_local_size(0) / 2; s > 0; s /= 2) {
        if (l < s)
            tile[l] += tile[l + s];
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    if (l == 0)
        sums[get_group_id(0)] = tile[0];
}

// cccc_launch(group_sum, CCCC_RANGE(16), CCCC_RANGE(8), in, sums,
//             CCCC_LOCAL(8 * sizeof(int)));
```

## Atomics

Each atomic returns the value from before the operation and is sequentially
consistent. The `atom_*` spellings of OpenCL 1.0 work too.

| Function | Operand types |
|---|---|
| `atomic_add`, `atomic_sub`, `atomic_and`, `atomic_or`, `atomic_xor` | `int`, `uint`, `long`, `ulong` |
| `atomic_inc`, `atomic_dec` | `int`, `uint`, `long`, `ulong` |
| `atomic_min`, `atomic_max` | `int`, `uint`, `long`, `ulong` |
| `atomic_cmpxchg(p, cmp, val)` | `int`, `uint`, `long`, `ulong` |
| `atomic_xchg(p, val)` | `int`, `uint`, `long`, `ulong`, `float` |

```c
__kernel void count(__global int *total, __global int *first) {
    atomic_add(total, 1);
    atomic_cmpxchg(first, 0, (int)get_global_id(0) + 1);   // one item wins
}
```

## Vector types

| Types | Total size |
|---|---|
| `char16`, `uchar16`, `short8`, `ushort8`, `int3`, `int4`, `uint3`, `uint4`, `long2`, `ulong2`, `float3`, `float4` | 16 bytes |
| `short16`, `ushort16`, `int8`, `uint8`, `long3`, `long4`, `ulong3`, `ulong4`, `float8` | 32 bytes |
| `int16`, `uint16`, `long8`, `ulong8`, `float16` | 64 bytes |

A three-lane type is stored as four lanes, so `sizeof(float3)` is 16, and it
exposes three. Arithmetic, comparisons and a scalar broadcast follow the GNU
vector rules in [COVERAGE.md](COVERAGE.md).

### Components

| Selector | Lanes |
|---|---|
| `.x` `.y` `.z` `.w` | Lane 0 to 3; letters combine: `.wzyx`, `.xxyy` |
| `.s0` to `.sF` | Lane by hex digit; digits combine: `.s0123`, `.sA` (also `.S`) |
| `.lo`, `.hi` | First or second half |
| `.even`, `.odd` | Lanes 0, 2, 4, ... or 1, 3, 5, ... |

```c
float4 v = (float4)(1, 2, 3, 4);
float4 r = v.wzyx;            // (4, 3, 2, 1)
float3 t = v.xyz;             // (1, 2, 3)
float8 w = (float8)(v, r);
float4 h = w.hi;              // (4, 3, 2, 1)

v.x  = 10;                    // one lane
w.lo = (float4)(0);           // several lanes, with =
```

A swizzle that names several lanes is a vector, so it must be one of the types
above; `.xy` of a `float4` is rejected. Several lanes assign with `=` only, and
each lane can appear once.

### Vector literals

`(T)(a, b, ...)` builds a vector. One scalar fills every lane; otherwise the
components, scalars and whole vectors, must add up to the lanes the type has.

```c
float4 a = (float4)(7);               // (7, 7, 7, 7)
float3 b = (float3)(1, 2, 3);
float8 c = (float8)(a, 1, 2, 3, 4);   // a vector and four scalars
```

## Pragmas and attributes

`#pragma OPENCL EXTENSION ... : enable` and `#pragma OPENCL FP_CONTRACT` are
ignored. `__attribute__((reqd_work_group_size(...)))`,
`work_group_size_hint(...)` and `vec_type_hint(...)` are accepted and not
enforced; the launch's local size is the one that applies.

## Limitations

- Vectors smaller than 16 bytes (`float2`, `int2`, `char4`, ...) do not exist
  ([#1407](https://todo.sr.ht/~takeiteasy/cccc/1407)).
- 128-byte vectors (`long16`, `ulong16`) do not exist
  ([#1408](https://todo.sr.ht/~takeiteasy/cccc/1408)).
- The OpenCL built-in library (`sqrt`, `min`, `max`, `clamp`, `mad`, `dot`,
  `native_*`, ...) is not provided, and a kernel cannot call libm
  ([#1409](https://todo.sr.ht/~takeiteasy/cccc/1409)).
- `half` is not provided
  ([#1410](https://todo.sr.ht/~takeiteasy/cccc/1410)).
- A compound assignment to a swizzle of several lanes (`v.lo += x`) is rejected
  ([#1412](https://todo.sr.ht/~takeiteasy/cccc/1412)).
- `get_global_offset` is always 0
  ([#1413](https://todo.sr.ht/~takeiteasy/cccc/1413)).
- The OpenCL host API (`clCreateBuffer`, `clEnqueueNDRangeKernel`, ...) is not
  emulated ([#1387](https://todo.sr.ht/~takeiteasy/cccc/1387),
  [#1388](https://todo.sr.ht/~takeiteasy/cccc/1388)).
- Images and samplers are not supported.
- `double` is rejected in kernel code, as in
  [GPU Kernel Subset](ATTRIBUTES.md#gpu-kernel-subset).

---

[^prelude]: An OpenCL input is preceded by `<cccc/opencl.h>`, which defines the
    macros in this guide on top of `<cccc/kernel.h>`. `-c=native` and `-m`
    output does not carry those macros: the generated C is already expanded.

[^underscore]: The qualifier macros expand to `[[cccc::__global__]]` and its
    siblings. A `[[cccc::...]]` attribute accepts the `__name__` spelling of
    `kernel`, `global`, `local`, `constant`, `private` and `generic` so that a
    macro named `local` cannot rewrite its own expansion.
