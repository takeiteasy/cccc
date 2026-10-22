# Type and Initialization Checks

Pointer type confusion and uninitialized reads.

## `--type-checks` — Runtime type checking on pointer dereferences

- Tracks type information per heap byte in a byte-granular **type
  shadow** (mirroring C11 §6.5p6's effective-type model): every heap
  byte starts with no effective type; a **store** through any address
  establishes (or re-establishes) the effective type of the bytes it
  writes; a **load** checks its static type against the shadow for the
  bytes it reads
- CHKT3 opcode implements the check/stamp/clear operations against the
  shadow, keyed by `(char*)ptr - vm->heap_seg`
- Detects type confusion bugs (e.g., casting `int*` to `float*` and
  reading through it) at **any offset into a heap allocation**, not just
  the base pointer — a struct member (`s->b`) or array element (`a[2]`)
  gets its own independent effective type, since each is checked only
  against stores through that same sub-range
- `char`-typed accesses are always legal (C11 §6.5p7): a `char` load
  never checks, and a `char` store clears the shadow for its range
  rather than stamping it "char", so a hand-rolled byte-copy loop
  doesn't mis-stamp its destination
- Union member access is exempted from both directions: a load through
  a union member skips the check entirely, and a store clears (rather
  than stamps) the accessed range, so legal member punning never
  false-positives
- `memcpy`/`memmove` propagate the source range's effective type onto
  the destination, so an ordinary struct/array copy followed by typed
  member reads doesn't false-positive; every other host function that
  might write into a tracked heap allocation or global has no VM-level
  hook, so the VM can't observe what such a call actually wrote and must
  conservatively **clear** shadow state before the call runs — this
  trades a false negative (a type-confusion bug that happens to route
  through an unclassified host write) for zero false positives. This
  backstop covers indirect host calls too (through a function pointer or
  a `dlsym`'d symbol), not just calls to a name resolved at compile time,
  and resolves either tracked segment for a pointer-shaped argument
  (`ffi_shadow_clear_extent`, `src/ops.c`): a heap address clears by its
  allocation's extent, exactly as before; a data-segment (global) address
  carries no allocation header to bound it, so it clears from the pointer
  to the end of the emitted data segment. Common libc/POSIX functions are
  classified by name to recover coverage a blanket clear would otherwise
  destroy, in four tiers, each of which can only ever reduce clearing
  relative to an unclassified name (never widen it, so classifying a
  function can't introduce a false positive): a **read-only** allowlist
  (`strlen`, `strcmp`, `memcmp`, `fwrite`, ...) gets no clear at all; a
  **bounded-write** list (`fread`, `snprintf`, `read`, `recv`, `strncpy`,
  the strto*/wcsto* family's `*endptr`, ...) narrows the clear to the statically-known
  extent written through the one argument that receives it, clamped
  against whichever segment resolves that argument, while every *other*
  pointer-shaped argument to that same call still gets the default
  whole-object clear, *unless* the rule marks every non-designated
  pointer argument as statically read-only, in which case those are
  skipped entirely instead of cleared (the strto*/wcsto* family's `nptr`
  argument, for instance, is never written through by any of these
  calls, so its shadow state is left untouched rather than wiped); the
  **printf
  family** (`printf`, `fprintf`, `dprintf`, `sprintf`, `snprintf`) is
  read-only for every argument except its output buffer (if any) *unless*
  the format string may contain a `%n` conversion — which can write
  through any pointer-shaped argument, not just the output buffer — in
  which case the call falls back to the whole-object-clear default for
  every argument, exactly as an unclassified call would. The format string
  is read directly from the guest pointer at the call site (CCCC has no
  guest/host address-translation layer, so a guest pointer already *is* a
  host pointer) and scanned conservatively: an unreadable pointer, a
  non-literal or truncated format, or an actual `%n` all fall back to the
  default, so this classification can only preserve or improve the
  no-false-positive property, never regress it. `vprintf`/`vsprintf`/
  `vsnprintf`/`vfprintf` and `scanf`/`sscanf`/`fscanf` are deliberately
  left unclassified: the `va_list` variants' pointees aren't reachable
  from the argument list, and the scanf family writes through every
  pointer argument by design. An unclassified name keeps the default
  whole-object clear across every pointer-shaped argument.
- `qsort`/`bsearch` preserve type-shadow coverage across the call instead
  of clearing it, for the common case of a uniformly-typed array: `qsort`
  only ever reorders whole elements, it never rewrites their bytes, so if
  every element already carries the same shadow byte pattern as element 0
  going in (a plain scalar array, or a struct array where every element
  was stamped the same way — mixed member types and padding included),
  that pattern survives any permutation the sort applies and the shadow
  needs no clear at all. This is checked again immediately after the host
  `qsort()` call returns, unconditionally — not just when the pre-check
  found a uniform range: `qsort`'s comparator is guest code, reentered
  synchronously through the guest callback trampoline, and its
  loads/stores run through ordinary CHKT3 checks like any other guest
  code, so a comparator that writes through its arguments mid-sort stamps
  the shadow at that element's pre-move position — including on an
  already-cleared (and therefore trivially uniform) range, where skipping
  the post-check would let that stray stamp survive at a stale position
  once `qsort` relocates the bytes. Either check finding a non-uniform
  range clears exactly `[base, base+nmemb*size)` (the range a host
  `qsort()` call can touch) rather than the whole allocation. `bsearch`'s
  host half writes through no argument at all — only reads — so it is
  classified read-only outright; its comparator is shadow-tracked the
  same way `qsort`'s is.
- `realloc` (always a fresh allocation in CCCC's bump-allocating VM
  heap, never grown in place) carries the old block's shadow across to
  the new address
- Reusing a heap buffer as a different type — legal in C — does not
  false-positive: the next store simply re-establishes the effective
  type for the bytes it touches
- Skips checks for `void*` and generic pointers (universal pointers)
- Resolves the allocation via the same `vm->sorted_allocs` binary search
  as `--bounds-checks`/`--uaf-detection`, so it works
  through interior *pointer arithmetic* on the way to a dereference, not
  just an exact base-pointer variable
- Covers the heap and globals (`static`/file-scope variables); stack
  subobjects are not type-tracked. Globals need no lifetime bookkeeping
  (unlike the heap, `data_seg` storage is never reused, so a global's
  shadow entry is simply stamped on first store and never cleared, aside
  from the struct-return buffer pool, which is cleared each time a slot
  is handed out since it rotates between calls); the stack is excluded
  because slot reuse across frames would need its own liveness tracking
  to avoid the false-positive class the dangling-pointer detector's
  frame-epoch/stack-interval bookkeeping exists to prevent
- Uniform coverage: the VM has no optimiser, so every dereference takes
  the ordinary `emit_load`/`emit_store` path and its CHKT3. The codegen
  fusion paths that once bypassed those (indexed-load fusion, restrict
  memcpy-loop lowering, the restrict-value cache) were removed with the
  optimiser, so there is no longer an opt-level-dependent gap to
  guard against.
- The shadow is a sparse page table (64 KiB pages), not one flat
  heap_committed-sized array: a page is allocated lazily on first stamp
  and freed back the instant a *single* clear zeroes it in full, so host
  memory tracks the *live* stamped footprint rather than the heap's total
  reservation — a large allocate-then-free pattern reclaims its shadow
  pages once the allocation is freed, instead of paying for them for the
  rest of the process. A page whose bytes only reach all-zero across
  several separate *partial* clears (chiefly the edge pages of a
  multi-page freed allocation) is queued as a sweep candidate instead of
  staying allocated indefinitely; an amortized sweep, rate-limited against
  the VM's instruction-cycle counter and charged proportional to pages
  actually scanned, reclaims a candidate once it verifies every byte is
  zero. Freeing a verified all-zero page is unobservable from the guest —
  every shadow reader already treats a missing page as "no effective type
  established" — so this only tightens the host memory bound, it cannot
  change detection behavior. Sweep activity is visible via `--vm-profile`
  (`shadow_sweeps`, `shadow_pages_swept`, `shadow_pages_live`)

### Example

```c
// test_type_check.c - Type checking example
void *malloc(unsigned long size);

int main() {
    int *int_ptr = (int *)malloc(sizeof(int) * 10);
    int_ptr[0] = 42;

    // Type confusion: treating int* as float*
    float *float_ptr = (float *)int_ptr;
    float value = *float_ptr;  // Type mismatch!
    return (int)value;
}
```

```bash
$ ./cccc --type-checks test_type_check.c

========== TYPE MISMATCH DETECTED ==========
Pointer type mismatch on dereference
Address:       0x7f8640028
Expected type: float
Actual type:   int
Allocated at PC offset: 15
Current PC:    0x7f84002d8 (offset: 52)
============================================
```

## `--uninitialized-detection` — Uninitialized variable detection

- Tracks initialization state of stack variables using HashMap
- MARKI opcode marks variables as initialized after assignment
- CHKI opcode validates variable is initialized before read
- Detects use of uninitialized local variables with stack offset info
- HashMap key: BP address + offset for per-function-call tracking
- **Address-based locals are exempt from the CHKI check**: a
  struct/union/array/wide-`_BitInt`/`_Decimal` local was already exempt. The same exemption now
  also covers any scalar local whose address is ever taken (`&var`,
  including through interior addressing like `&arr[i]`/`&s.field`,
  tracked via `Obj.addr_taken`), one accessed from a nested function
  (`is_captured`), and a `__block`-qualified local written from inside a
  block literal (`is_block_var`) — all three write through a mechanism
  the syntactic-assignment MARKI tracking never sees, most commonly the
  ordinary C out-parameter idiom (`void fill(int *out){ *out = 42; }`).
  Cost: a genuinely-uninitialized local that has its address taken is no
  longer caught by this detector once that address is taken, even if the
  read in question doesn't go through that address.

### Example

```c
// test_uninit.c - Uninitialized variable example

int main() {
    int x;
    int y = 10;
    int z = x + y;  // Reading uninitialized variable x!
    return z;
}
```

```bash
$ ./cccc --uninitialized-detection test_uninit.c

========== UNINITIALIZED VARIABLE READ ==========
Attempted to read uninitialized variable
Stack offset: -1
Address:      0x7ffee4b3f8
BP:           0x7ffee4b400
PC:           0x7ffe400120 (offset: 32)
================================================
```
