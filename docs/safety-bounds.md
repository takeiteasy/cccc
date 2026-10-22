# Bounds Checks

Runtime array bounds checking.

## `--bounds-checks` — Runtime array bounds checking

- Tracks the aligned/usable size (`AllocHeader.size`, not the raw
  requested size) for all heap allocations
- Split into two checks: `CHKB`/`CHKBN` (formation-time —
  "pointer *subscript* addition"/"pointer subtraction") validate a
  pointer *value* as it's formed by `p + n`/`p - n`; `CHKD`
  (dereference-time) validates the address actually read or written at
  every load/store, struct/union copy, and vector load/store
- Detects out-of-bounds array accesses with offset information
- Resolves **interior pointers** (`p = q + k`) back to their containing
  allocation via `vm->sorted_allocs`, not just exact base pointers; a
  negative effective offset is only rejected once it steps before the
  *resolved allocation's* start, so `p[-1]` on an interior pointer that
  stays within the allocation is valid
- A pointer **difference** (`&a - &b`, result type `ptrdiff_t`/`long`)
  is never bounds-checked — its operand isn't a scaled byte offset, it's
  the ptr-ptr divide's raw subtraction, so checking it against an
  allocation's size would be checking an unrelated value
- **Formation vs. dereference**: forming a one-past-the-end
  pointer on heap memory (`p + n` where `n == size`) is legal C — only
  dereferencing it is undefined — and `CHKB`/`CHKBN` now allow it (they
  used to reject it, since they were the *only* check on a subscript at
  all, `a[i]` desugaring to `*(a+i)`). `CHKD` is the separate
  dereference-time check that still traps on `a[size]` itself, so
  forming the pointer and dereferencing it are no longer conflated.
- **Fixed-size arrays**: a subscript directly over a local or global array
  (`buf[k]`, `m[i][j]`, `s.arr[k]`) is checked against the array's size at
  compile time, so a stack or global overrun is reported at the access, not
  later at function return, including a row that does not exist (`m[rows][0]`).
  `a[size]` is rejected, `&a[size]` and `a + size`
  are not. An array reached through a pointer (`char *q = buf; q[k]`) or
  passed as a parameter is not checked, and a struct's trailing array
  (`char data[1]`) is treated as flexible.
- **Atomic ops**: `CHKD` is also emitted ahead of the four atomic
  opcodes — `ALDR`/`ASTR`/`AXCHG`/`ACAS` — so a one-past-the-end atomic
  dereference traps the same as any other. `ACAS` gets two `CHKD`s (the
  object pointer and the `expected` pointer, since a failed
  `compare_exchange` reads *and writes* through `expected`). Emission is
  unconditional under `CCCC_BOUNDS_CHECKS` (no `addr_is_local_frame` gate
  like the scalar/vector sites use — at these sites the address is a
  pointer-*valued* expression, not an lvalue node, so that classifier
  would always report "not a local frame address" anyway). This was
  originally deferred because `AXCHG`/`ACAS`'s own operand words carry a
  register-aliasing hazard; a standalone `CHKD` instruction
  emitted ahead of them never touches that word, so it doesn't reopen it.
  Like `CHKB`/`CHKBN`, `CHKD` still has no bound to check against for a
  stack or global array (no `AllocHeader` to resolve).
- **Cost note**: under `-2`/`-3`, a scalar heap access now does two
  `sorted_allocs` lookups instead of one (`CHKB` at formation, `CHKD` at
  dereference) — `CHKP3` already does a lookup of its own at the same
  site, so this is a proportional, not order-of-magnitude, increase.

### Example

```c
// test_bounds.c - Bounds checking example
void *malloc(unsigned long size);

int main() {
    char *arr = (char *)malloc(10);
    char c = arr[10];  // Out of bounds!
    return c;
}
```

```bash
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


## `--pointer-sanitizer` — Comprehensive pointer checking

- Enables `--bounds-checks`, `--uaf-detection`, and `--type-checks` together
- Provides comprehensive pointer safety in a single flag
- Recommended for development and testing
