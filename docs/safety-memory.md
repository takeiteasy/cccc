# Memory Safety: Canaries, Poisoning and Leaks

Heap and stack protection, poisoning, leak, use-after-free and double-free detection.

All features listed below can be enabled individually or through the safety level presets above. This section provides detailed information about what each feature does and how it works.


## `--stack-canaries` — Stack overflow protection

- Reserves the slot just below the saved base pointer (`bp[-1]`) for a canary value (0xDEADBEEFCAFEBABE); parameters and locals are placed from `bp[-2]` downward
- The one-slot shift is baked into stack offsets at compile time (`assign_stack_offsets`), so it stays consistent across threads
- Validates canary on function return (LEV instruction)
- Detects a stack overrun that reaches the canary or saved return address, reported when the function returns with the PC offset; an overrun that stays inside the frame is not detected
- Works with functions that take parameters and with threading (`pthread`)

### Deep Recursion
```c
// test_stack_overflow_recursion.c - Stack overflow from deep recursion
int recurse(int n) {
    if (n <= 0) return 0;
    return recurse(n - 1) + 1;
}

int main() {
    return recurse(100000);  // Stack overflow!
}
```

```bash
$ ./cccc test_stack_overflow_recursion.c

========== STACK OVERFLOW ==========
Stack space exhausted
Requested:  1 slots (8 bytes)
Available:  16 slots (128 bytes)
PC:         0x1480081e8 (offset: 61)
====================================
```

### Large Stack Frame
```c
// test_stack_overflow_large_frame.c - Stack overflow from large local array
void large_frame() {
    long long arr[500000];  // 4MB on stack!
    arr[0] = 42;
}

int main() {
    large_frame();  // Stack overflow!
    return 42;
}
```

```bash
$ ./cccc test_stack_overflow_large_frame.c

========== STACK OVERFLOW ==========
Stack space exhausted
Requested:  500009 slots (4000072 bytes)
Available:  262131 slots (2097048 bytes)
PC:         0x1280080a8 (offset: 21)
====================================
```

**Note:** Stack bounds checking is **always enabled** to prevent memory corruption. The default stack size is 2MB (256KB poolsize × 8 bytes per slot). Stack overflow is always a bug and cannot be disabled.

## Stack bounds checking (always enabled)

- Validates stack pointer stays within allocated stack segment before function calls and frame allocation
- Checks at function entry (ENT3) and call instructions (CALL/CALLI) before pushing to stack
- Detects stack exhaustion from deep recursion or oversized stack frames
- Includes 128-byte guard zone to prevent edge-case overflows
- Reports detailed error with requested vs available stack space and PC offset
- Zero overhead when stack is within bounds (single pointer comparison)
- Prevents memory corruption from stack overflow

## `--heap-canaries` — Heap overflow protection

- Front canary written into AllocHeader.canary (before user data)
- Rear canary (`sizeof(long long)`) written after the end of the aligned allocation
- Both canaries use the current stack canary value (0xDEADBEEFCAFEBABE, or random if `--random-canaries`)
- Both validated on free(); mismatch triggers HEAP CANARY CORRUPTED error with address and size
- Detects heap buffer overflows (rear canary) and header corruption (front canary)
- Requires VM heap mode (on by default; not compatible with disabling it via `-V`)

## `--random-canaries` — Random stack canaries

- Generates cryptographically random canary value on VM initialization
- Uses /dev/urandom on Unix-like systems for strong randomness
- Falls back to time-seeded rand() if /dev/urandom unavailable
- Prevents predictable canary bypass attacks
- Works with `--stack-canaries` flag (canaries are fixed by default)
- Minimal performance overhead (one-time generation at startup)
- Canary value stored in `vm->stack_canary` field

## `--memory-poisoning` — Allocated/freed memory poisoning

- Fills newly allocated memory with 0xCD pattern ("clean memory")
- Fills freed memory with 0xDD pattern ("dead memory")
- Makes uninitialized reads return consistent bad values (0xCDCDCDCD…)
- Makes use-after-free reads return dead memory pattern (0xDDDDDDDD…)
- Helps detect uninitialized variable bugs and UAF issues
- Requires VM heap mode (on by default; not compatible with disabling it via `-V`)
- Performance overhead proportional to allocation size (memset on alloc/free)

## `--memory-leak-detection` — Memory leak detection

- Tracks user-facing VM heap allocations in a host-memory linked list (AllocRecord)
- Compiler-internal automatic storage -- a VLA's backing storage (`ALCV`
  opcode, `AllocHeader.kind = ALLOC_KIND_FRAME`), a bare `__builtin_alloca`
  call's storage (`ALCA` opcode, `AllocHeader.kind = ALLOC_KIND_ALLOCA`),
  and `__block` boxes (`ALCB` opcode, `ALLOC_KIND_BLOCK_BOX`) -- is
  allocated via these separate opcodes instead of `MALC` and is
  deliberately excluded from this list: each still gets a full
  `AllocHeader`/`sorted_allocs` entry (so
  `CHKB`/`CHKBN`/`CHKP3`/`__builtin_dynamic_object_size` are unaffected),
  but none of the three is ever meant to be user-freed, so reporting them
  would be permanent false-positive noise on every program that declares
  a VLA or calls `alloca`. The three-way split (rather than one shared
  kind) exists because heap reclamation below) treats them differently:
  a VLA's storage is reclaimed at both block and frame exit, a bare
  alloca's only at frame exit, and a `__block` box's never at all
- Removes the record on free() and realloc()
- Reports all unfreed allocations at program exit (in cc_destroy)
- Shows address, size, and PC offset of allocation site for each leak
- The report is printed to stdout after the program exits; exit code is unchanged

## `--uaf-detection` — Use-after-free detection

- Marks freed blocks instead of reusing them
- Increments generation counter on each free
- CHKP opcode checks if accessed pointer has been freed
- Reports UAF with allocation details and generation number
- Resolves **interior pointers** (`p = q + k`) back to their containing
  allocation via `vm->sorted_allocs` (the same range-query table used by
  `__builtin_dynamic_object_size`), not just exact base pointers, so
  a UAF reached through an interior pointer is caught too

### Example

```c
// test_uaf.c - Use-after-free example
void *malloc(unsigned long size);
void free(void *ptr);

int main() {
    int *ptr = (int *)malloc(sizeof(int) * 10);
    ptr[0] = 42;
    free(ptr);
    int value = ptr[0];  // Use after free!
    return value;
}
```

```bash
$ ./cccc --uaf-detection test_uaf.c

========== USE-AFTER-FREE DETECTED ==========
Attempted to access freed memory
Address:     0x7f3640028
Size:        40 bytes
Allocated at PC offset: 15
Generation:  1 (freed)
Current PC:  0x7f34002b0 (offset: 86)
============================================
```

## Double-free detection (always enabled)

- Automatically detects attempts to free the same pointer twice
- Works independently of UAF detection setting
- Tracks freed state in allocation header
- Prevents free list corruption and security vulnerabilities
- Aborts execution with detailed error message including address, size, and generation

### Example

```c
// test_double_free.c - Double-free example
void *malloc(unsigned long size);
void free(void *ptr);

int main() {
    void *ptr = malloc(100);
    free(ptr);
    free(ptr);  // Double-free detected!
    return 0;
}
```

```bash
$ ./cccc -V test_double_free.c

========== DOUBLE-FREE DETECTED ==========
Attempted to free already-freed memory
Address:  0x897640038
Size:     104 bytes
Allocated at PC offset: 11
Generation: 1
=========================================
```

**Note:** Double-free detection is always enabled when using VM heap (MALC/MFRE), regardless of which safety flags are active. Since the VM heap is on by default, this applies to plain `malloc`/`free` code without any extra flags; it also works with any memory safety feature (`-M`/`--memory-leak-detection`, `-B`/`--bounds-checks`, etc.) that routes allocations through the VM heap.
