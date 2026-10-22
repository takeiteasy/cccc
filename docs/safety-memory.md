# Canaries, Poisoning and Leaks

Stack and heap protection, poisoning, leak, use-after-free and double-free detection.

| Flag | Detects | Needs VM heap |
|---|---|---|
| `--stack-canaries` | Stack overruns that reach the canary or return address | |
| (always on) | Stack exhaustion | |
| `--heap-canaries` | Heap buffer overflows and header corruption | yes |
| `--random-canaries` | Predictable canary bypass | |
| `--memory-poisoning` | Uninitialized and freed reads (`0xCD` / `0xDD`) | yes |
| `--memory-leak-detection` | Unfreed allocations | yes |
| `--uaf-detection` | Use after free | yes |
| (always on) | Double free | yes |

"Needs VM heap" means it cannot be combined with `-V` ([safety-vm-heap.md](safety-vm-heap.md)). Levels bundle these ([safety.md](safety.md)).

## `--stack-canaries`

The slot at `bp[-1]` holds a canary (`0xDEADBEEFCAFEBABE`), checked when the function returns; parameters and locals sit below it. An overrun that reaches the canary or saved return address is reported at return with the PC offset. An overrun that stays inside the frame is not detected. It works with parameters and with threads. `--random-canaries` replaces the fixed value with a random one (from `/dev/urandom`, else a time-seeded `rand()`) generated once at startup.[^canary]

## Stack exhaustion

Always on, and cannot be disabled. The stack pointer is checked at function entry and before calls, with a 128-byte guard zone; the default stack is 2 MB. The report shows requested against available space.

```c
int recurse(int n) {
    if (n <= 0) return 0;
    return recurse(n - 1) + 1;
}
int main() { return recurse(100000); }   // deep recursion
```

```
========== STACK OVERFLOW ==========
Stack space exhausted
Requested:  1 slots (8 bytes)
Available:  16 slots (128 bytes)
PC:         0x1480081e8 (offset: 61)
====================================
```

A large local array overflows the same way (`long long arr[500000];` requests 500009 slots against 262131 available).

## `--heap-canaries`

A front canary in the allocation header and a rear canary after the aligned allocation both hold the current canary value (random under `--random-canaries`). Both are checked on `free`; a mismatch reports `HEAP CANARY CORRUPTED` with address and size.

## `--memory-poisoning`

New memory is filled with `0xCD` (so uninitialized reads return `0xCDCDCDCD...`) and freed memory with `0xDD`. The cost is a `memset` on each allocation and free.

## `--memory-leak-detection`

User-facing VM heap allocations are tracked and any left at exit are listed to stdout with address, size and allocation site; the exit code is unchanged. VLA storage, `alloca` and `__block` boxes are excluded, since they are never meant to be freed by the user.[^kinds]

## `--uaf-detection`

Freed blocks are marked rather than reused, and a generation counter is bumped on each free. Interior pointers (`p = q + k`) resolve to their allocation, so a use after free through one is caught too.

```c
int *ptr = (int *)malloc(sizeof(int) * 10);
ptr[0] = 42;
free(ptr);
int value = ptr[0];   // use after free
```

```
========== USE-AFTER-FREE DETECTED ==========
Attempted to access freed memory
Address:     0x7f3640028
Size:        40 bytes
Allocated at PC offset: 15
Generation:  1 (freed)
Current PC:  0x7f34002b0 (offset: 86)
============================================
```

## Double free

Always on with the VM heap, whatever safety flags are active, so plain `malloc`/`free` code is covered. Freeing the same pointer twice aborts with the address, size and generation.

```c
void *ptr = malloc(100);
free(ptr);
free(ptr);   // detected
```

```
========== DOUBLE-FREE DETECTED ==========
Attempted to free already-freed memory
Address:  0x897640038
Size:     104 bytes
Allocated at PC offset: 11
Generation: 1
=========================================
```

[^canary]: The one-slot shift for the canary is baked into stack offsets at compile time, so it stays consistent across threads.
[^kinds]: They use separate opcodes (`ALCV`, `ALCA`, `ALCB`) instead of `MALC` and still get a full allocation header, so bounds, pointer and `__builtin_dynamic_object_size` checks work on them. A VLA's storage is reclaimed at block and frame exit, an `alloca`'s at frame exit only, and a `__block` box's never.
