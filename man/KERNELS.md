# Kernels

Run a `[[cccc::kernel]]` function over a grid of work-items in the VM, the way
a GPU runs it. The kernel sees ids, work-group local memory and barriers; the
launch goes through the VM's bounds and safety checks.

```c
#include <cccc/kernel.h>

[[cccc::kernel]] static void add([[cccc::global]] int *out,
                                 [[cccc::global]] const int *a,
                                 [[cccc::global]] const int *b) {
    size_t i = cccc_global_id(0);
    out[i]   = a[i] + b[i];
}

int main(void) {
    int a[8] = {1, 2, 3, 4, 5, 6, 7, 8}, b[8] = {8, 7, 6, 5, 4, 3, 2, 1}, out[8];
    cccc_launch(add, CCCC_RANGE(8), CCCC_RANGE(4), out, a, b);
    return out[0];   // 9
}
```

The function body is checked as described in
[GPU Kernel Subset](ATTRIBUTES.md#gpu-kernel-subset); this guide covers
running it.

## Launching

`cccc_launch(kernel, global, local, args...)` runs `kernel` once per work-item
and returns when every work-item has finished.

| Argument | Meaning |
|---|---|
| `kernel` | A `[[cccc::kernel]]` function, `&function`, or a pointer to one |
| `global` | Work-items in total, one to three dimensions: `CCCC_RANGE(x)`, `CCCC_RANGE(x, y)`, `CCCC_RANGE(x, y, z)` |
| `local` | Work-items per work-group, same number of dimensions |
| `args...` | The kernel's arguments, checked and converted like an ordinary call |

Each global size must be a multiple of the matching local size. A launch with
a bad range stops the program with an `error: cccc_launch:` message.

Work-groups run one at a time, in order. A group's work-items are the only ones
that run concurrently, and only when the kernel uses a barrier (see
[Barriers](#barriers)).

A kernel called like an ordinary function, outside a launch, runs as a single
work-item: every id is 0, every size is 1, and a barrier does nothing.

**Argument types.** Integers, enums, `bool`, pointers, `float` and `double`
arguments work, up to 8 integer-class and 8 float-class arguments per launch.
Structs, unions and vectors are rejected.[^args]

**Function pointers.** A pointer's type does not carry `[[cccc::kernel]]`, so
launching through one checks the target when the launch runs:

```c
void (*k)([[cccc::global]] int *) = bump;
cccc_launch(k, CCCC_RANGE(4), CCCC_RANGE(2), out);
```

## Work-item builtins

Each takes a dimension (0, 1 or 2). Asking for a dimension beyond the launch's
returns 0 for ids and 1 for sizes.

| Builtin | Returns |
|---|---|
| `cccc_global_id(d)` | Index in the whole range |
| `cccc_local_id(d)` | Index within the work-group |
| `cccc_group_id(d)` | Index of the work-group |
| `cccc_global_size(d)` | Size of the whole range |
| `cccc_local_size(d)` | Size of a work-group |
| `cccc_num_groups(d)` | Number of work-groups |
| `cccc_work_dim()` | Dimensions of the launch (1 outside a launch) |

## Local memory

A `[[cccc::local]]` object in a kernel's body is shared by the work-items of
a group and starts zeroed in every group. It cannot have an initializer.

```c
[[cccc::kernel]] static void group_sum([[cccc::global]] const int *in,
                                       [[cccc::global]] int *sums) {
    [[cccc::local]] int tile[64];
    size_t l = cccc_local_id(0);
    tile[l]  = in[cccc_global_id(0)];
    cccc_barrier(CCCC_LOCAL_FENCE);
    for (size_t s = cccc_local_size(0) / 2; s > 0; s /= 2) {
        if (l < s)
            tile[l] += tile[l + s];
        cccc_barrier(CCCC_LOCAL_FENCE);
    }
    if (l == 0)
        sums[cccc_group_id(0)] = tile[0];
}
```

To size local memory at launch time, take a `[[cccc::local]]` pointer
parameter and pass `CCCC_LOCAL(bytes)` for it:

```c
[[cccc::kernel]] static void flip([[cccc::global]] int *data,
                                  [[cccc::local]] int *scratch) { /* ... */ }

cccc_launch(flip, CCCC_RANGE(8), CCCC_RANGE(4), data, CCCC_LOCAL(4 * sizeof(int)));
```

`CCCC_LOCAL` is only valid for a `[[cccc::local]]` pointer parameter, and such
a parameter needs it.

A kernel that declares local objects cannot be called from another kernel; call
it with `cccc_launch` from host code.

## Barriers

`cccc_barrier(fences)` waits until every work-item of the group has reached it.
`fences` is `CCCC_LOCAL_FENCE`, `CCCC_GLOBAL_FENCE` or both; the VM treats them
alike.

A kernel with no barrier runs its work-items one after another. A kernel that
calls `cccc_barrier`, directly or through a helper, runs a VM thread per
work-item of the group.[^threads] A work-item that returns early counts as
having reached every later barrier, so a kernel whose items disagree about a
barrier does not hang.

## Group size

A work-group may hold at most 256 work-items. Raise the limit with
`--kernel-max-group-size=N`.

## Limitations

- The VM only: `-c=native`, `-m` and `-c=generated` reject launches and the
  builtins ([#1394](https://todo.sr.ht/~takeiteasy/cccc/1394)).
- Structs, unions and vectors as kernel arguments, and more than 8 integer or
  8 float arguments ([#1395](https://todo.sr.ht/~takeiteasy/cccc/1395)).
- Id queries and local-memory accesses are host calls, and a barrier kernel
  starts a thread per work-item for each group, so large launches are slow
  ([#1396](https://todo.sr.ht/~takeiteasy/cccc/1396)).

---

[^args]: Arguments are checked against the kernel's parameters by the same
    rules as a call, then handed to the launcher in registers, which is why
    aggregates are not accepted.

[^threads]: The threads are ordinary VM threads under the global interpreter
    lock; the barrier releases the lock while a work-item waits.
