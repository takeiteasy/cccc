# Pointer Provenance, Tagging and CFI

Alignment, provenance, arithmetic, format strings, memory tagging and control-flow integrity.

## `--alignment-checks` — Pointer alignment validation

- CHKA opcode validates pointer alignment before dereference
- Checks that `pointer % type_size == 0`
- Detects misaligned memory access (e.g., `int*` at odd address)
- Only checks types larger than 1 byte

### Example

```c
// test_alignment.c - Pointer alignment example

void *malloc(unsigned long size);

int main() {
    char *buffer = (char *)malloc(16);

    // Create a misaligned int pointer (offset by 1 byte)
    int *misaligned = (int *)(buffer + 1);

    int value = *misaligned;  // Alignment error!
    return value;
}
```

```bash
$ ./cccc --alignment-checks test_alignment.c

========== ALIGNMENT ERROR ==========
Pointer is misaligned for type
Address:       0x100c8eac1
Type size:     4 bytes
Required alignment: 4 bytes
Current PC:    0xb98800148 (offset: 41)
=====================================
```

## `--provenance-tracking` — Pointer origin tracking

- Tracks pointer provenance: HEAP, STACK, or GLOBAL
- MARKP opcode records origin when pointers are created
- Automatically tracks heap allocations in MALC opcode
- HashMap stores: pointer → {origin_type, base, size}
- Enables validation of pointer operations within original object bounds

## `--invalid-arithmetic` — Pointer arithmetic bounds checking

- Requires `--provenance-tracking` to be enabled
- Checks that `ptr` stays within `[base, base+size]` after arithmetic
- Detects out-of-bounds pointer computations before dereference
- Prevents pointer escape from original object

## `--stack-instrumentation` — Stack variable lifetime and access tracking

- Tracks all stack variable lifetimes with full block-level scoping
- SCOPEIN/SCOPEOUT opcodes mark scope entry/exit for each `{ }` block
- CHKL opcode validates variable is alive before access, keyed by the
  variable's actual runtime address (bp+offset) — not by stack offset or
  scope_id alone, so two functions whose locals land at the same offset, or
  recursive calls of the same function, don't collide with each other's
  liveness state
- MARKR/MARKW opcodes track read/write counts for each variable, aggregated
  across all activations (including recursive calls) of that declaration
- **Note on detection coverage:** `CHKL` is a liveness *guard*, not a
  dangling-pointer *detector* — it only checks that a local being accessed
  by name is currently in scope, which ordinary C code can't violate (a
  local's name isn't visible outside its own lexical scope). In practice it
  no longer fires in correct programs after; catching a genuine
  use-after-return through a pointer dereference is `--dangling-pointers`'
  job (see above)
- Stack overflow detection: tracks high water mark, warns at 90% threshold
- `--dangling-pointers` is independent of this flag — its check lives in
  `CHKP3`, not in the SCOPEIN/SCOPEOUT/CHKL machinery described here
- Use `--stack-errors` flag to enable runtime errors (vs logging only)
- Use `cc_print_stack_report()` API to print access statistics

## `--format-string-checks` — Format string validation

- Validates format strings at compile time via `__attribute__((format(printf/scanf, …)))`
- Gated by `--format-string-checks` or included in safety presets `-S1` / `-S2`
- Annotates printf/scanf-family functions in the bundled standard library headers
- Works with any function annotated with the GNU `format` attribute
- Counts format specifiers (%d, %s, %f, %x, %p, %c, etc.) and compares with argument count
- Detects mismatches before runtime to prevent undefined behavior
- Supports all standard format specifiers: d, i, u, o, x, X, f, F, e, E, g, G, a, A, c, s, p, n
- Handles %% (literal percent sign, not a specifier)
- Handles width (*) and precision (.*) specifiers that consume arguments
- Handles length modifiers: hh, h, l, ll, L, z, j, t (don't affect specifier count)
- Works with: printf, fprintf, sprintf, snprintf, scanf, sscanf, fscanf
- Detects both missing arguments (undefined behavior) and extra arguments (logic error)
- Performs basic type checking: %d expects int, %s expects char*, %f expects double, etc.
- Prints detailed warning message showing expected vs. actual argument counts
- Zero overhead when disabled (simple flag check at compile time)

## `--memory-tagging` — Temporal memory tagging

- Tracks generation counter for each heap allocation in AllocHeader
- Records pointer→creation_generation in a host-memory side table (HashMap) at malloc time
- Increments generation counter when memory is freed (MFRE opcode)
- CHKP3 opcode validates pointer's stored generation matches current header generation
- Detects use-after-free with generation detail: shows which generation the pointer was born at
- Requires VM heap mode (on by default; not compatible with disabling it via `-V`)
- Uses `hashmap_put_int` / `hashmap_get_int` for O(1) pointer tag lookup
- **Limitation:** side table is keyed by address; if freed memory is reallocated at the same
  address (free-list path), the old entry is overwritten and stale pointers to that address
  are not detected. The bump allocator never reuses addresses *while memory tagging is
  active* — `--memory-tagging` is one of the exact features that keeps heap reclamation below) permanently disabled, precisely
  to avoid this side table going stale — so this is not a practical concern with memory
  tagging on. It would become one the moment a real free list is added on top of an
  allocator mode where tagging is off, which doesn't exist today.

### Example

```c
// test_temporal_tagging.c - Cross-generation use-after-free
void *malloc(unsigned long size);
void free(void *ptr);

int main() {
    // Allocate memory (generation 0)
    int *ptr1 = (int *)malloc(sizeof(int) * 10);
    *ptr1 = 42;

    // Save pointer for later use
    int *stale_ptr = ptr1;

    // Free the memory (generation becomes 1)
    free(ptr1);

    // Allocate new memory at different address
    // Without --uaf-detection, this might reuse same address
    // With --uaf-detection, memory is quarantined, gets new address
    int *ptr2 = (int *)malloc(sizeof(int) * 10);
    *ptr2 = 100;

    // Try to use the stale pointer
    // stale_ptr has generation 0 tag, but memory was freed
    // This is caught even though ptr1's memory isn't reused
    int value = *stale_ptr;  // Temporal safety violation!

    return value;
}
```

```bash
$ ./cccc --memory-tagging -V test_temporal_tagging.c

========== TEMPORAL SAFETY VIOLATION ==========
Stale pointer access detected
Address:            0x137210040
Pointer generation: 0
Current generation: 1
Allocated at PC offset: 17
Current PC:         0x57 (offset: 87)
================================================
```

**Note:** Memory tagging uses a host-side HashMap keyed by pointer address. Because the current
VM heap is a bump allocator that never reuses freed addresses, the stored generation at the old
address will always diverge after a free, giving correct temporal violation reports. This works
for programs that call `malloc`/`free` directly since the VM heap is on by default; do not pass
`-V` (it disables the VM heap and is incompatible with `--memory-tagging`, which requires it).

**Example:**
```bash
$ ./cccc --memory-tagging my_program.c

# Or use short flags
$ ./cccc -TV my_program.c
```

## `--control-flow-integrity` — Control flow integrity

- Implements shadow stack to detect ROP attacks and stack corruption
- On CALL/CALLI: pushes return address to both main stack and shadow stack
- On LEV (function return): validates return address matches shadow stack
- Detects any modification to return addresses on the stack
- Protects against Return-Oriented Programming (ROP) exploits
- Minimal performance overhead (~1-3% per function call)
- Memory overhead: 2x stack size (main + shadow stack)
- Zero overhead when disabled
- Works with all function calls including recursion and indirect calls
- Automatically skips validation for main() exit (no corresponding CALL)
- `-V` / `--no-vm-heap` **VM heap allocator** — see [VM Heap Allocator](safety-vm-heap.md#vm-heap-allocator) below;
as of this flag *disables* the VM heap (it's on by default) and cannot be combined with
`-1`/`-2`/`-3` or `--safety=basic/standard/max`, nor (as of) with any individual flag
whose checks key off the VM heap's `AllocHeader` metadata: `--bounds-checks`,
`--uaf-detection`, `--type-checks`, `--heap-canaries`, `--memory-leak-detection`,
`--memory-tagging`, `--pointer-sanitizer`. All of these are hard compile-time errors, not a
silent no-op — before, `-V --bounds-checks` compiled and ran with the requested check
never firing (it segfaults on the exact out-of-bounds write it was supposed to trap, instead
of trapping it).


### Example

```c
// test_cfi_normal.c - Normal function calls with CFI
int helper() {
    return 42;
}

int main() {
    int x = helper();
    return x;
}
```

```bash
$ ./cccc --control-flow-integrity test_cfi_normal.c
$ echo $?
42
```

**CFI protects against stack corruption and ROP attacks by maintaining a shadow stack:**
- Every function call (CALL/CALLI) pushes the return address to both the main stack and shadow stack
- On function return (LEV), the VM validates that the return address on the main stack matches the shadow stack
- If they differ, a CFI violation is detected and execution is aborted

**Example with recursion:**
```c
// test_cfi_recursion.c - CFI with recursive calls
int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int main() {
    int result = factorial(5);
    return result == 120 ? 42 : 1;
}
```

```bash
$ ./cccc -C test_cfi_recursion.c
$ echo $?
42
```

**Performance characteristics:**
- Memory overhead: 2x stack size (main stack + shadow stack)
- Runtime overhead: 1-3% per function call (one extra push/pop operation)
- Zero overhead when disabled

**Note:** CFI automatically handles the main() function exit, which has no corresponding CALL instruction. The shadow stack validation is skipped when returning from main() to prevent false positives.

**Using the short flag:**
```bash
$ ./cccc -C my_program.c
```
