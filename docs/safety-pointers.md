# Pointer Provenance, Tagging and CFI

Alignment, provenance, arithmetic, format strings, memory tagging and control-flow integrity.

| Flag | Detects | Needs |
|---|---|---|
| `--alignment-checks` | Misaligned pointer dereference | |
| `--provenance-tracking` | Records whether a pointer is HEAP, STACK or GLOBAL | |
| `--invalid-arithmetic` | Pointer arithmetic that leaves its object | `--provenance-tracking` |
| `--stack-instrumentation` | Stack variable lifetimes, access counts, stack overflow | |
| `--format-string-checks` | `printf`/`scanf` argument mismatches | |
| `--memory-tagging` (`-T`) | Use-after-free, with generation detail | VM heap |
| `--control-flow-integrity` (`-C`) | Return-address corruption and ROP | |

## `--alignment-checks`

Traps a dereference through a pointer that is not a multiple of its type's size (types over one byte).

```c
char *buffer = (char *)malloc(16);
int *misaligned = (int *)(buffer + 1);
int value = *misaligned;   // alignment error
```

```
$ ./cccc --alignment-checks test_alignment.c

========== ALIGNMENT ERROR ==========
Pointer is misaligned for type
Address:       0x100c8eac1
Type size:     4 bytes
Required alignment: 4 bytes
Current PC:    0xb98800148 (offset: 41)
=====================================
```

## `--provenance-tracking` and `--invalid-arithmetic`

Provenance records where each pointer came from and the object's `{base, size}`. With `--invalid-arithmetic`, a pointer must stay within `[base, base+size]` after arithmetic, so an out-of-bounds computation is caught before any dereference.

## `--stack-instrumentation`

| Part | Role |
|---|---|
| Lifetimes | Block-level scoping; each `{ }` marks entry and exit |
| Liveness guard | Keyed by the variable's runtime address, so recursion and same-offset locals in different functions do not collide |
| Access counts | Read and write counts per declaration, across all activations |
| Stack overflow | Tracks the high-water mark and warns at 90% |

The liveness guard cannot fire in correct code: a local's name is not visible outside its scope. Catching use-after-return through a pointer is [`--dangling-pointers`](safety-dangling.md), which is independent of this flag. `--stack-errors` turns the logging into runtime errors, and `cc_print_stack_report()` prints access statistics.

## `--format-string-checks`

Checks calls to any function annotated with `__attribute__((format(printf|scanf, ...)))`, including the `printf` and `scanf` families in the bundled headers. It is part of `-1` and every higher level, and costs nothing when off.

| Check | Detail |
|---|---|
| Argument count | Missing arguments (undefined behaviour) and extra arguments |
| Specifiers | `d i u o x X f F e E g G a A c s p n`; `%%` is literal |
| `*` width and `.*` precision | Count as arguments |
| Length modifiers | `hh h l ll L z j t` do not change the count |
| Basic types | `%d` wants `int`, `%s` wants `char *`, `%f` wants `double`, ... |

Warnings show the expected and actual counts.

## `--memory-tagging`

Each heap allocation has a generation, bumped on free. A pointer remembers the generation it was born with, and a dereference whose generation is stale traps. It needs the VM heap, so it cannot be combined with `-V`.

```c
int *ptr1 = (int *)malloc(sizeof(int) * 10);
*ptr1 = 42;
int *stale_ptr = ptr1;
free(ptr1);                                  // generation becomes 1
int *ptr2 = (int *)malloc(sizeof(int) * 10);
int value = *stale_ptr;                      // temporal safety violation
```

```
$ ./cccc --memory-tagging test_temporal_tagging.c

========== TEMPORAL SAFETY VIOLATION ==========
Stale pointer access detected
Address:            0x137210040
Pointer generation: 0
Current generation: 1
Allocated at PC offset: 17
Current PC:         0x57 (offset: 87)
================================================
```

`./cccc -TV my_program.c` is the short form of the flag.[^tag]

## `--control-flow-integrity`

A shadow stack catches return-address corruption and ROP: every call pushes the return address onto both stacks, and every return checks they match. A mismatch aborts.

```sh
./cccc --control-flow-integrity test_cfi_normal.c    # or: ./cccc -C test_cfi_normal.c
```

| Property | Value |
|---|---|
| Runtime cost | About 1 to 3% per call |
| Memory | 2x stack size |
| Coverage | All calls, including recursion and indirect calls |
| `main` exit | Skipped: it has no matching call |

## `-V` and the VM heap

`-V` / `--no-vm-heap` disables the VM heap. It is a compile error together with `-1`/`-2`/`-3`, `--safety=basic|standard|max`, or any flag that reads the heap's allocation metadata: `--bounds-checks`, `--uaf-detection`, `--type-checks`, `--heap-canaries`, `--memory-leak-detection`, `--memory-tagging`, `--pointer-sanitizer`. See [safety-vm-heap.md](safety-vm-heap.md).

[^tag]: The side table is keyed by address. With memory tagging active the bump allocator never reuses an address, so a freed address cannot be silently re-tagged. Tagging is one of the features that keeps heap reclamation disabled for this reason.
