# VM Heap Allocator

The built-in allocator behind the safety checks.

## VM Heap Allocator

`malloc`/`free`/`calloc`/`realloc`/`aligned_alloc`/`posix_memalign` route through the VM heap
(`MALC`/`MFRE`/`CALC`/`REALC`/`MALCA`/`PMEMA` opcodes) by default at every safety level, including
`-0`. This is what makes the heap safety features below usable without any special opt-in in
normal code.

The VM heap is a pure bump allocator: `MFRE` never moves the bump pointer
back (it only marks an allocation `freed`), so ordinary user allocations are
never reclaimed until the process exits. `alloca`/VLA backing storage
(`ALCA`/`ALCV` opcodes) is the one exception — see **Heap Reclamation** below.

### Heap Reclamation

`alloca`/VLA storage would otherwise grow the VM heap without bound in a
long-running loop or recursive function, since it is outside the
`malloc`/`free` leak-tracking model and nothing else frees it. It is
reclaimed, but **only** when doing so cannot desync any other safety
feature: reclaiming an address means a *later* allocation can be handed
that same address back, which every address-keyed feature below (bounds
checks, UAF/dangling-pointer/type checks, memory tagging, uninitialized-
read detection, leak detection, heap canaries) assumes never happens.

- **When it runs:** only at `-0` (or any explicit flag combination that
  happens not to set the features above), and only for `alloca`/VLA
  storage — never a genuine `malloc`, never a `__block` box (`Block_copy`
  is expected to let one legitimately outlive its declaring frame).
- **When it doesn't:** at `-1`/`-2`/`-3`, or under any of the flags listed
  above individually — the heap simply keeps growing as it always did,
  trading unbounded growth for keeping every
  other safety feature's tracking fully sound. This is a deliberate
  tradeoff, not an oversight: reclamation was designed from the start to
  never have to choose between the two.
- **Granularity:** a VLA's storage is reclaimed at both the exit of the
  block that declared it (C11 6.8.3's actual VLA lifetime) and, as a
  backstop, at the enclosing frame's exit. A bare `__builtin_alloca` call's
  storage is reclaimed only at frame exit, since (unlike a VLA) its
  lifetime extends to the whole function, not just the block it was called
  in.
- **Threads:** each thread reclaims only its own storage. The VM heap is
  one arena shared by every thread, so another thread's allocation sitting
  above a block's or frame's starting point stops the rewind there, and
  that storage is reclaimed only partially. A thread that ends through
  `pthread_exit`/`thrd_exit` has its outstanding storage reclaimed when it
  ends.

The opcode-level mechanics (`HMRK`/`HREL`, the `ALCA`/`ALCV`/`ALCB` split,
`heap_rewind_to`'s pinning behavior, and the `CALLT`/`longjmp` interaction)
live with the VM internals.

- `-V` / `--no-vm-heap` **turns the VM heap off**, reverting all of the above to the host allocator
  via FFI. It is only valid at safety level 0 (default or explicit `-0`) with none of
  `--bounds-checks`/`--uaf-detection`/`--type-checks`/`--heap-canaries`/
  `--memory-leak-detection`/`--memory-tagging`/`--pointer-sanitizer` set; combining it with any
  of those, `-1`/`-2`/`-3`, or `--safety=basic/standard/max` is a hard compile-time error since
  they all require the VM heap. It affects the compiled program only: `[[cccc::comptime]]` code
  always allocates from the VM heap.
- `free_sized`/`free_aligned_sized` (C23) are routed through the same `MFRE` opcode as `free`,
  so a VM-heap-allocated pointer freed through either call is handled correctly.
- `aligned_alloc`/`posix_memalign` are intercepted the same way as `malloc`/`calloc`, via
  `MALCA`/`PMEMA`: the bump allocator pads *before* the `AllocHeader` so the returned pointer
  meets the requested alignment while the header is still recoverable via
  `((AllocHeader*)ptr) - 1`, giving them the same canaries/bounds-checks/UAF-detection/
  type-checks/leak-detection/tagging coverage as `malloc`. Freeing their result with plain
  `free()`/`free_sized()`/`free_aligned_sized()` works via the normal `MFRE` path (no fallback to
  the host allocator needed, since the pointer now carries a VM-heap header).
- Zero overhead when no other safety features are enabled.
