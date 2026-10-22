# Bounds Checks

`--bounds-checks` traps out-of-bounds array accesses, and `--pointer-sanitizer` turns on the main pointer checks together.

## `--bounds-checks`

```c
int main() {
    char *arr = (char *)malloc(10);
    char c = arr[10];   // out of bounds
    return c;
}
```

```
$ ./cccc --bounds-checks test_bounds.c

========== ARRAY BOUNDS ERROR ==========
Pointer is outside allocated region
Address:       0x8c564003a
Base:          0x8c5640030
Offset:        10 bytes
Requested size: 10 bytes
Allocated size: 16 bytes (rounded)
Allocated at PC offset: 11
Current PC:    0x8c5400140 (offset: 40)
=========================================
```

| Behaviour | Detail |
|---|---|
| Bound | Heap allocations, using the usable (aligned) size |
| Forming a pointer | `p + n` may form a one-past-the-end pointer, which is legal C |
| Dereferencing | The address actually read or written is checked at every load, store, struct or union copy, vector access and atomic operation, so `a[size]` traps |
| Interior pointers | `p = q + k` resolves to its containing allocation; `p[-1]` is fine while it stays inside |
| Pointer difference (`&a - &b`) | Never checked |
| Fixed-size arrays | `buf[k]`, `m[i][j]`, `s.arr[k]` over a local or global array are checked at compile time, including a nonexistent row (`m[rows][0]`). `&a[size]` and `a + size` are allowed |
| Not checked | An array reached through a pointer or passed as a parameter, and a struct's trailing array (`char data[1]`, treated as flexible) |
| Stack and global arrays through a pointer | No bound to check against; use [checked pointers](safety-checked-pointers.md) |

Under `-2` and `-3` a scalar heap access does two allocation lookups (formation and dereference) instead of one.[^ops]

## `--pointer-sanitizer`

Enables `--bounds-checks`, `--uaf-detection` and `--type-checks` together. Recommended for development and testing.

[^ops]: Formation uses `CHKB`/`CHKBN`; dereference uses `CHKD`, also emitted ahead of the atomic opcodes (`ALDR`, `ASTR`, `AXCHG`, `ACAS`, the last with two for the object and `expected` pointers). Internals: [safety-impl.md](safety-impl.md#bounds-checks).
