# CCCC Memory Safety Features

CCCC includes a suite of powerful memory safety features designed to detect common C programming errors at runtime. These features can be enabled individually or together to provide comprehensive protection against bugs like buffer overflows, use-after-free, and type confusion.

## Safety Levels (Quick Start)

CCCC provides preset safety levels that make it easy to choose the right combination of features without needing to understand every individual flag. Each level builds on the previous one, adding more comprehensive checks with increasing performance overhead.

### Level 0: None (`-0` or `--safety=none`)
**Purpose:** Maximum performance, no safety *checks*
**Overhead:** ~0% for checks; `malloc`/`free`/`calloc`/`realloc` still route through the VM heap by default (see below)
**Use when:** You've thoroughly tested your code and need maximum speed, or when running in a trusted/controlled environment

```bash
./cccc -0 program.c
./cccc --safety=none program.c
```

**Enabled features:** None. The VM heap allocator is still active by default at every level (add `-V`/`--no-vm-heap` to opt back into the host allocator); see [VM Heap Allocator](#vm-heap-allocator) below.

---

### Level 1: Basic (`-1` or `--safety=basic`)
**Purpose:** Essential "smoke alarm" checks with minimal overhead
**Overhead:** ~5-10%
**Use when:** Running production code where you want essential safety without significant performance impact

```bash
./cccc -1 program.c
./cccc --safety=basic program.c
```

**Enabled features:**
- Stack canaries (stack overflow protection)
- Heap canaries (heap overflow protection)
- Memory leak detection
- Integer overflow checks
- Format string validation
- VM heap mode (required for heap safety features)

**Detects:** Stack buffer overflows, heap buffer overflows, memory leaks, integer overflow/underflow, format string bugs

---

### Level 2: Standard (`-2` or `--safety=standard`) — **Recommended Default**
**Purpose:** Comprehensive development and testing safety
**Overhead:** ~20-40%
**Use when:** Developing, testing, or running code where safety is more important than raw performance

```bash
./cccc -2 program.c
./cccc --safety=standard program.c
```

**Enabled features:**
- **All Level 1 features** plus:
- Pointer sanitizer (bounds checks, use-after-free detection, type checks)
- Uninitialized variable detection
- Memory poisoning (0xCD for allocated, 0xDD for freed memory)

**Detects:** Everything in Level 1 plus use-after-free, out-of-bounds array access, type confusion, uninitialized variable reads, double-free

---

### Level 3: Maximum (`-3` or `--safety=max`)
**Purpose:** Paranoid mode for debugging hard-to-find bugs
**Overhead:** ~60-100%+
**Use when:** Debugging mysterious crashes, memory corruption, or security-critical code

```bash
./cccc -3 program.c
./cccc --safety=max program.c
```

**Enabled features:**
- **All safety features** including:
  - Control flow integrity (CFI) with shadow stack
  - Temporal memory tagging (generation-based UAF detection)
  - Dangling stack pointer detection (dereference-time range check plus
    per-frame epoch liveness tracking plus interior interval-stabbing; see
    [Advanced Pointer Tracking Features](#advanced-pointer-tracking-features)
    for what it catches, #670/#673/#675)
  - Alignment checks
  - Provenance tracking (pointer origin validation)
  - Invalid pointer arithmetic detection
  - Stack variable instrumentation (per-activation liveness tracking,
    read/write counts — see below for what it does and doesn't catch)
  - Random canaries (unpredictable stack protection)

**Detects:** Most memory safety bugs CCCC can catch, including dereferencing a
stack pointer whose owning frame has already returned -- whether the
dereference happens in the frame that got control back or one or more calls
*deeper* than that (#673 closed the deeper-call gap left by #670), including
through an interior pointer with a runtime-computed offset (e.g. `&arr[i]`
for non-constant `i`) whose owning array/struct escapes its frame (#675
closed the remaining gap left by #673; see `--dangling-pointers` below)

---

### Combining Safety Levels with Individual Flags

Safety levels are **additive** - you can combine a preset level with individual flags to customize your safety profile:

```bash
# Level 2 + temporal tagging (not in Level 2 by default)
./cccc -2 --memory-tagging program.c

# Level 1 + control flow integrity
./cccc -1 --control-flow-integrity program.c

# Level 3 is already maximum, so additional flags are redundant
./cccc -3 program.c  # Already includes everything
```

### Quick Reference

| Level | Flags | Overhead | Best For |
|-------|-------|----------|----------|
| 0 (none) | - | 0% | Production (validated code) |
| 1 (basic) | `-1` or `--safety=basic` | ~5-10% | Production (with safety net) |
| 2 (standard) | `-2` or `--safety=standard` | ~20-40% | Development & Testing |
| 3 (max) | `-3` or `--safety=max` | ~60-100%+ | Debugging & Security Analysis |

### Smoke-testing before a real compile: `--test-run[=LEVEL]`

`--test-run[=LEVEL]` (see [TESTING.md](TESTING.md#test-runlevel-smoke-test-the-program-itself-before-compiling)) reuses these same preset names/levels (`LEVEL` accepts `none`/`basic`/`standard`/`max` or `0`/`1`/`2`/`3`, defaulting to `max`) to run the program once under the VM's safety instrumentation before handing it to `-c=native`/`-c=bytecode`. It's a way to get this level's crash/safety-violation coverage as a pre-compile gate — e.g. `-1`/`--safety=basic`'s low-overhead checks as a cheap CI smoke test before every native build — without needing a `[[cccc::test]]` suite.

---

## Configuring Safety from Source: `#pragma cccc config(...)`

`#pragma cccc config(key = value, ...)` is a file-scope pragma that sets
safety levels, the optimisation level, and individual safety-check flags
directly from source, without needing matching command-line flags. It is
useful for self-contained test files and examples that must run with a
specific safety/optimisation profile regardless of how they're invoked.

```c
#pragma cccc config(safety = 2, optimisation = 1)
#pragma cccc config(bounds_checks = true, memory_tagging = false)
#pragma cccc config(overflow_checks) // bare key is shorthand for "= true"
```

### Keys

| Key | Values | Effect |
|-----|--------|--------|
| `safety` | `0`-`3` | Same as `-0`/`-1`/`-2`/`-3` (none/basic/standard/max) |
| `optimisation` | `0`-`3` | Same as `--optimize=N` |
| `bounds_checks` | `true`/`false` (or `1`/`0`) | `--bounds-checks` |
| `uaf_detection` | `true`/`false` | `--uaf-detection` |
| `type_checks` | `true`/`false` | `--type-checks` |
| `overflow_checks` | `true`/`false` | `--overflow-checks` |
| `stack_canaries` | `true`/`false` | `--stack-canaries` |
| `heap_canaries` | `true`/`false` | `--heap-canaries` |
| `memory_leak_detection` | `true`/`false` | `--memory-leak-detection` |
| `pointer_sanitizer` | `true`/`false` | `--pointer-sanitizer` |
| `memory_tagging` | `true`/`false` | `--memory-tagging` |
| `checked_pointers` | `true`/`false` | `--checked-pointers` |

A bare key with no `= value` (e.g. `config(bounds_checks)`) is shorthand for
`= true`. Multiple keys can be set in one `config(...)`, and the pragma can
appear multiple times in a file — later pragmas win for the same key
("last write wins"), the same as combining individual CLI flags.

### Precedence

- **Command-line flags always win.** If a safety/optimisation setting was
  explicitly passed on the command line (`-0`/`-1`/`-2`/`-3`,
  `--safety=...`, `--optimize=...`, `--bounds-checks`, etc.), `config(...)`
  silently has no effect on that setting — the CLI value is kept.
- **Ignored in native mode.** When compiling with `-c=native`, `config(...)`
  has no effect on `vm->flags`/optimisation level (native output is handed
  to the system `cc`, which has its own optimisation/sanitizer flags). This
  prints a `-Wignored-features` warning (on by default under `-Wall`) naming
  the pragma; it is otherwise silent by default, the same as every other
  `-Wignored-features` diagnostic (`_Atomic`, etc.). Unknown keys and invalid
  values are still hard errors in native mode.
- **Unknown keys and out-of-range values are hard compile errors**, e.g.
  `config(frobnicate = 1)` or `config(safety = 9)`.

---

## Individual Memory Safety Features

All features listed below can be enabled individually or through the safety level presets above. This section provides detailed information about what each feature does and how it works.

- `--stack-canaries` **Stack overflow protection**
  - Reserves the slot just below the saved base pointer (`bp[-1]`) for a canary value (0xDEADBEEFCAFEBABE); parameters and locals are placed from `bp[-2]` downward
  - The one-slot shift is baked into stack offsets at compile time (`assign_stack_offsets`), so it stays consistent across threads
  - Validates canary on function return (LEV instruction)
  - Detects stack buffer overflows with detailed error reporting including PC offset
  - Works with functions that take parameters and with threading (`pthread`)
- **Stack bounds checking** (always enabled)
  - Validates stack pointer stays within allocated stack segment before function calls and frame allocation
  - Checks at function entry (ENT3) and call instructions (CALL/CALLI) before pushing to stack
  - Detects stack exhaustion from deep recursion or oversized stack frames
  - Includes 128-byte guard zone to prevent edge-case overflows
  - Reports detailed error with requested vs available stack space and PC offset
  - Zero overhead when stack is within bounds (single pointer comparison)
  - Prevents memory corruption from stack overflow
- `--heap-canaries` **Heap overflow protection**
  - Front canary written into AllocHeader.canary (before user data)
  - Rear canary (`sizeof(long long)`) written after the end of the aligned allocation
  - Both canaries use the current stack canary value (0xDEADBEEFCAFEBABE, or random if `--random-canaries`)
  - Both validated on free(); mismatch triggers HEAP CANARY CORRUPTED error with address and size
  - Detects heap buffer overflows (rear canary) and header corruption (front canary)
  - Requires VM heap mode (on by default; not compatible with disabling it via `-V`)
- `--random-canaries` **Random stack canaries**
  - Generates cryptographically random canary value on VM initialization
  - Uses /dev/urandom on Unix-like systems for strong randomness
  - Falls back to time-seeded rand() if /dev/urandom unavailable
  - Prevents predictable canary bypass attacks
  - Works with `--stack-canaries` flag (canaries are fixed by default)
  - Minimal performance overhead (one-time generation at startup)
  - Canary value stored in `vm->stack_canary` field
- `--memory-poisoning` **Allocated/freed memory poisoning**
  - Fills newly allocated memory with 0xCD pattern ("clean memory")
  - Fills freed memory with 0xDD pattern ("dead memory")
  - Makes uninitialized reads return consistent bad values (0xCDCDCDCD…)
  - Makes use-after-free reads return dead memory pattern (0xDDDDDDDD…)
  - Helps detect uninitialized variable bugs and UAF issues
  - Requires VM heap mode (on by default; not compatible with disabling it via `-V`)
  - Performance overhead proportional to allocation size (memset on alloc/free)
- `--memory-leak-detection` **Memory leak detection**
  - Tracks user-facing VM heap allocations in a host-memory linked list (AllocRecord)
  - Compiler-internal automatic storage -- `alloca`/VLA backing blocks
    (`ALCA` opcode, `AllocHeader.kind = ALLOC_KIND_FRAME`) and `__block`
    boxes (`ALCB` opcode, `ALLOC_KIND_BLOCK_BOX`, split from `ALCA` in
    #981's prerequisite so a future reclamation pass can target the former
    without ever sweeping the latter) -- is allocated via these separate
    opcodes instead of `MALC` and is deliberately excluded from this list
    (#979): it still gets a full `AllocHeader`/`sorted_allocs` entry (so
    `CHKB`/`CHKBN`/`CHKP3`/`__builtin_dynamic_object_size` are unaffected),
    but it is never meant to be user-freed, so reporting it would be
    permanent false-positive noise on every program that declares a VLA
  - Removes the record on free() and realloc()
  - Reports all unfreed allocations at program exit (in cc_destroy)
  - Shows address, size, and PC offset of allocation site for each leak
  - The report is printed to stdout after the program exits; exit code is unchanged
- `--uaf-detection` **Use-after-free detection**
  - Marks freed blocks instead of reusing them
  - Increments generation counter on each free
  - CHKP opcode checks if accessed pointer has been freed
  - Reports UAF with allocation details and generation number
  - Resolves **interior pointers** (`p = q + k`) back to their containing
    allocation via `vm->sorted_allocs` (the same range-query table used by
    `__builtin_dynamic_object_size`, #647), not just exact base pointers, so
    a UAF reached through an interior pointer is caught too (#650)
- **Double-free detection** (always enabled)
  - Automatically detects attempts to free the same pointer twice
  - Works independently of UAF detection setting
  - Tracks freed state in allocation header
  - Prevents free list corruption and security vulnerabilities
  - Aborts execution with detailed error message including address, size, and generation
- `--bounds-checks` **Runtime array bounds checking**
  - Tracks the aligned/usable size (`AllocHeader.size`, not the raw
    requested size) for all heap allocations
  - Split into two checks (#983): `CHKB`/`CHKBN` (formation-time —
    "pointer *subscript* addition"/"pointer subtraction", #982) validate a
    pointer *value* as it's formed by `p + n`/`p - n`; `CHKD`
    (dereference-time) validates the address actually read or written at
    every load/store, struct/union copy, and vector load/store
  - Detects out-of-bounds array accesses with offset information
  - Resolves **interior pointers** (`p = q + k`) back to their containing
    allocation via `vm->sorted_allocs`, not just exact base pointers; a
    negative effective offset is only rejected once it steps before the
    *resolved allocation's* start, so `p[-1]` on an interior pointer that
    stays within the allocation is valid (#650)
  - A pointer **difference** (`&a - &b`, result type `ptrdiff_t`/`long`)
    is never bounds-checked — its operand isn't a scaled byte offset, it's
    the ptr-ptr divide's raw subtraction, so checking it against an
    allocation's size would be checking an unrelated value (#982)
  - **Formation vs. dereference (#983)**: forming a one-past-the-end
    pointer on heap memory (`p + n` where `n == size`) is legal C — only
    dereferencing it is undefined — and `CHKB`/`CHKBN` now allow it (they
    used to reject it, since they were the *only* check on a subscript at
    all, `a[i]` desugaring to `*(a+i)`). `CHKD` is the separate
    dereference-time check that still traps on `a[size]` itself, so
    forming the pointer and dereferencing it are no longer conflated.
  - **Known limitation**: `CHKD` is emitted for scalar loads/stores,
    struct/union/wide-`_BitInt`/`_Decimal` copies, and vector loads/stores,
    but not for the atomic ops (`ALDR`/`ASTR`/`AXCHG`/`ACAS`) — their
    operand words already carry a register-aliasing hazard (#497) that a
    naive addition would risk reopening; tracked as a follow-up ticket.
    Also, like `CHKB`/`CHKBN`, `CHKD` has no bound to check against for a
    stack or global array (no `AllocHeader` to resolve).
  - **Cost note**: under `-2`/`-3`, a scalar heap access now does two
    `sorted_allocs` lookups instead of one (`CHKB` at formation, `CHKD` at
    dereference) — `CHKP3` already does a lookup of its own at the same
    site, so this is a proportional, not order-of-magnitude, increase.
- `--checked-pointers` **Checked-pointer bounds checking (Checked C-style)**
  - Enforces the `[[cccc::single/array/ntarray]]` + `count()`/`byte_count()`/
    `bounds()` attributes declared on a pointer type (always parsed and
    type-checked regardless of this flag; see
    [Checked Pointers](#checked-pointers) below for the full reference)
  - Unlike `--bounds-checks` above, the bound comes from the pointer's own
    **declaration**, not from heap allocation metadata — so it works
    uniformly on heap, stack, and global-array pointers, not just the heap
  - CHKR opcode validates `addr != 0 && lo <= addr && addr + size <= hi` on
    every checked dereference, where `[lo, hi)` is recomputed from the
    declaration at each access
  - Not part of any `-0`/`-1`/`-2`/`-3` safety preset; opt in explicitly
- `--type-checks` **Runtime type checking on pointer dereferences**
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
    as `--bounds-checks`/`--uaf-detection` (#650's pattern), so it works
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
  - Full coverage at every optimization level, including standalone
    `--type-checks -O2`/`-O3`: the codegen fusion paths that bypass
    `emit_load`/`emit_store` (indexed-load fusion, restrict memcpy-loop
    lowering) are disabled whenever `--type-checks` is enabled, not just
    alongside `--bounds-checks`/`--uaf-detection`. The restrict-value cache
    stays enabled instead of disabled — its cache-hit path re-derives the
    address and runs CHKP3/CHKT3 itself, so a cache hit gets the same
    coverage as a real load (see OPTIMIZATION.md)
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
- `--uninitialized-detection` **Uninitialized variable detection**
  - Tracks initialization state of stack variables using HashMap
  - MARKI opcode marks variables as initialized after assignment
  - CHKI opcode validates variable is initialized before read
  - Detects use of uninitialized local variables with stack offset info
  - HashMap key: BP address + offset for per-function-call tracking
- `--overflow-checks` **Signed integer overflow detection**
  - Detects arithmetic overflow for addition, subtraction, multiplication, and division
  - Emits checked opcodes (ADDC, SUBC, MULC, DIVC) when enabled
  - ADDC/SUBC: Validates result stays within LLONG_MIN to LLONG_MAX range
  - MULC: Uses `__builtin_mul_overflow` to detect multiplication overflow
  - DIVC: Detects division by zero and signed overflow (LLONG_MIN / -1)
  - Reports overflow with operands, operation type, and PC offset
  - Zero overhead when disabled (uses regular ADD/SUB/MUL/DIV opcodes)
  - Does not affect floating-point operations
- `--pointer-sanitizer` **Comprehensive pointer checking (convenience flag)**
  - Enables `--bounds-checks`, `--uaf-detection`, and `--type-checks` together
  - Provides comprehensive pointer safety in a single flag
  - Recommended for development and testing

### Threading Safety

The POSIX `<pthread.h>` VM runtime serializes bytecode execution with the VM GIL. The following threading-aware features are available:

**GIL release around blocking POSIX calls**

Blocking calls — `read`, `write`, `pread`, `pwrite`, `readv`, `writev`, `preadv`, `pwritev`, `poll`, `accept`, `connect`, `wait`, `waitpid`, `sleep`, `usleep` — release the GIL while blocked so other VM threads can make progress. Non-blocking calls (`close`, `lseek`, `stat`, `open`, and most control-plane socket calls) intentionally hold the GIL.

**Thread-aware stack canaries**

Stack canary checks work correctly under threading. Each VM thread runs with its own `bp`/`sp`/`stack_seg` via `ExecState` (saved and restored on every GIL hand-off), so canary slots are isolated per thread. Stack canary protection enabled with `-1` or `--stack-canaries` is not disabled when `pthread_create` is used.

**Dangling-pointer detection under threading**

`--dangling-pointers`'s per-frame liveness bookkeeping (`frame_epochs`/`live_epochs`/`stack_ptr_epochs`/`stack_intervals`, see below) is isolated per thread the same way stack canaries are: each field lives in `ExecState` and is swapped in/out on every GIL hand-off (`#866`), so a worker thread's own `ENT3`/`LEV3` bookkeeping is never compared against another thread's unrelated stack. Before this, running any worker thread at `-3`/`--dangling-pointers` while another thread had an outstanding escaping local (e.g. simply because `main` took `&thread_id` for `pthread_create`'s out-parameter) would assert almost immediately. Detection of a pointer escaping **across** threads (a worker's local address stored somewhere and dereferenced by a different thread after the worker has exited) is not implemented — each thread's liveness bookkeeping is discarded when that thread exits, so such a dereference is a real, undetected use-after-free rather than a diagnosed one.

**`--thread-safety` diagnostics** _(off by default; enables the checks below)_

Enable with `--thread-safety`. Intended for development and testing — not enabled by preset safety levels.

- **Double-lock detection**: Diagnoses when a thread attempts to lock a non-recursive mutex it already holds.
  ```
  ====== DEADLOCK: double-lock detected ======
  ```

- **Lock-order inversion detection**: Builds a lock-acquisition graph at runtime. When a thread acquires locks in the opposite order seen previously, a potential deadlock is reported.
  ```
  ====== LOCK ORDER INVERSION detected ======
  ```

- **Data race detection**: Tracks the last thread to write each address without holding a mutex (shadow-map approach). When a second thread accesses the same address also without a mutex, a potential race is reported. Detection covers pointer-dereference and indexed array access; global-variable access tracking is planned for a future release.
  ```
  ====== DATA RACE DETECTED ======
  ```

- **`_Atomic` cast warning**: Warns at compile time when a cast strips the `_Atomic` qualifier from a pointer type, producing a plain pointer to an atomic object.
  ```
  warning: cast discards '_Atomic' qualifier from pointer type; non-atomic access to atomic object may cause data races
  ```

- **Mixed atomic/non-atomic access detection**: `atomic_load`/`atomic_store` (and `atomic_load_explicit`/`atomic_store_explicit`) emit dedicated `ALDR`/`ASTR` opcodes that tag a per-address `atomic_shadow` map with the accessing thread id. If a second thread performs a plain (non-atomic) memory access to the same address without holding a mutex, a diagnostic is printed at runtime. Forward-only detection: the atomic access must occur before the non-atomic one to be detected.
  ```
  ====== MIXED ATOMIC/NON-ATOMIC ACCESS DETECTED ======
  Address 0x... was accessed atomically by thread 0x... and is now read non-atomically by thread 0x... without a mutex
  ```
  **Known limitation:** `atomic_fetch_add`, `atomic_fetch_sub`, and similar read-modify-write operations still expand to plain load/store opcodes — they do not set the atomic tag and may be incorrectly flagged as non-atomic on an address that was previously `atomic_store`d. Avoid mixing `atomic_fetch_*` with `atomic_store`/`atomic_load` on the same address across threads when using `--thread-safety`.

- **`atomic_exchange` and `atomic_compare_exchange`**: These operations compile to dedicated `AXCHG`/`ACAS` opcodes and are correctly handled as atomic accesses in the shadow map. The VM GIL ensures atomicity with respect to other VM threads.

## Advanced Pointer Tracking Features

- `--dangling-pointers` **Dangling stack pointer detection (dereference-time range check + per-frame epoch liveness + interior interval-stabbing, #670/#673/#675)**
  - Enforced in `CHKP3`, the same gate that runs before every pointer
    dereference under `--pointer-checks` / `-3`. Three layers, all
    dereference-time (no creation-time escape analysis decides *correctness*
    — #676's escape analysis only prunes a redundant recording, see below):
    1. **Range check (#670).** The VM stack grows downward, so every *live*
       local sits at an address `>= vm->sp`, and any address inside the stack
       reservation but below the current `vm->sp` belongs to a frame that has
       already returned. A single unsigned range check
       (`stack_seg <= ptr < sp`) is precise by construction — cheap, no
       hashmap needed for this layer.
    2. **Frame-epoch liveness (#673).** Every activation gets a monotonic
       epoch at `ENT3`; every `&local` (`LEA3`) tags the resulting address
       with the current frame's epoch in a side table (`stack_ptr_epochs`),
       the direct stack analogue of how heap UAF detection tags an
       allocation's creation generation in `ptr_tags`. A pointer to a local
       is valid iff the frame that created it is still live, so `CHKP3` flags
       a dereference whose tagged epoch is no longer in the live set
       (`live_epochs`) — regardless of whether the address happens to sit
       above or below the *current* `sp`. This closes the range check's gap:
       a pointer passed *deeper* into another call reuses the dead frame's
       memory (`ptr >= sp` again there), which layer 1 alone would miss, but
       its tagged epoch is still absent from `live_epochs` and layer 2 still
       catches it. Since #740, a dead exact tag no longer concludes dangling
       on its own: stack addresses are reused across frames, so a returned
       sibling's own exact-recorded escaping local can leave a stale tag at
       an address a *live* frame has since legitimately reclaimed (e.g. a
       stack-spilled variadic vector argument's `va_arg` slot landing on a
       dead sibling's old `va_list` base). `CHKP3` now falls through to
       layer 3's `stack_interval_stab` on a dead exact hit and only reports
       dangling if no *live* interval covers the address either — the same
       prefer-live arbitration layer 3 already had for #727, now shared by
       layer 2
    3. **Interior interval-stabbing (#675).** An interior stack pointer with a
       runtime-computed offset (e.g. `&arr[i]` for non-constant `i`) compiles
       to a base `LEA3` for `arr` plus a separate runtime `ADD`, so the
       *final* dereferenced address is never itself the one recorded in
       `stack_ptr_epochs` by layer 2 — only `arr`'s base is. `STKTAG`, emitted
       immediately after the base `LEA3` of any *escaping* array/struct local
       (and, unconditionally, of any `vector_size` vector local regardless of
       whether escape analysis proved it escaping — #727: element access
       (`v[i]`) always re-derives the vector's address like a struct member,
       even for a vector that never leaves its frame), retains
       `[base, base+size)` tagged with the creating frame's epoch in
       `vm->stack_intervals`. Unlike `sorted_allocs` (the heap analogue,
       #650) — a bump allocator whose base addresses are globally ordered and
       never reused — stack addresses *are* reused across frames, so
       intervals are retained rather than pruned on frame death; a dead
       frame's extent and a later live frame's extent can share the same
       addresses. Consulted only when layer 2's exact lookup misses, `CHKP3`
       resolves an interior address by **preferring the max-epoch *live*
       containing interval** (#727) — only when every containing interval is
       dead does it fall back to the plain max-epoch (dead) interval and
       flag it. Recency-by-epoch-order alone (epoch order *is* recency order,
       since activations get strictly increasing epochs) is not sound here:
       a live frame's own STKTAG range can coexist with an *unrelated* dead
       sibling frame's STKTAG range at the very same addresses (stack reuse
       across siblings, not just across a dead frame and its live successor),
       and the dead sibling can have a numerically higher epoch just by
       having run more recently before returning. Preferring any live
       containing interval resolves this regardless of epoch ordering,
       while a genuinely dangling interior pointer (no live interval covers
       it) is still flagged correctly.
  - **What it catches:** dereferencing a pointer to a local whose function has
    returned, whether the dereference happens in the frame that got control
    back (or any ancestor of it — layer 1) or one or more calls *deeper* than
    that (layer 2), through a base pointer or an interior one with a
    runtime-computed offset whose owning array/struct escapes (layer 3) —
    e.g. `int *p = get_local(); return *p;`, `int *p = get_local(); use(p);`
    where `use` derefs `p`, and `int *p = &arr[i]` (runtime `i`) returned from
    `get_local` and derefed in a deeper call. This also covers deref-through-
    pointer cases that `CHKL` (see `--stack-instrumentation` below) does not
  - **What it does not catch:** this is frame-granular detection, not
    block-granular — a `&local` whose *block* (not function) has exited
    while the frame is still alive is `CHKL`'s job, not this check's (see
    `--stack-instrumentation` below)
  - **Local aggregate member/element access is not gated on `CHKP3` at all
    (#740).** A local struct/union's own member access (`t.a`) lowers
    through `gen_addr()` + `emit_load`/`emit_store` exactly like a genuine
    pointer dereference, so `CHKP3` used to run unconditionally there too —
    and could find a *stale* exact tag left in `stack_ptr_epochs` by an
    unrelated, already-returned sibling frame's own escaping local that
    happened to reuse the same physical stack address, misreporting a
    plainly-live access to the current frame's own memory as dangling.
    `addr_is_local_frame` (`src/codegen.c`) recognizes when `gen_addr`'s
    result is guaranteed to be a bp-relative address of the *current*
    function's own live frame — built entirely through the plain
    local-offset path, with no intervening pointer *value* load (captured
    variable, static-link chain, by-pointer aggregate param, `__block` heap
    wrapper, or an actual pointer dereference) — and skips `CHKP3` for that
    access entirely; a genuine pointer dereference (`p->a`, `p[i]` through a
    variable holding a passed/returned address) always bottoms out at a
    dereference in this classifier and stays fully checked. Deliberately not
    gated on whether the local's address escapes: accessing your own local
    from within your own still-running frame is safe regardless.
    **Related layer-2 exact-tag collision, resolved separately (#740).**
    Array/vector *indexing* (`arr[i]`) and a stack-spilled variadic
    argument slot read by `va_arg` don't qualify for the `addr_is_local_frame`
    skip above (the address isn't always provably the current frame's own
    memory — e.g. the `va_arg` slot lives in the argument-passing area, not
    a declared local), so they can still hit a dead sibling frame's stale
    exact tag in `stack_ptr_epochs`. Rather than skip `CHKP3` at these sites,
    #740 fixed the collision at its source: layer 2 now falls through to
    layer 3's prefer-live `stack_interval_stab` on a dead exact hit instead
    of concluding dangling outright (see layer 2's own description above).
    A live frame's own STKTAG'd extent — e.g. a non-escaping vector or array
    local, or a stack-spilled `va_arg` read — now wins over an overlapping
    dead sibling's stale exact tag; a genuinely dangling exact-tag deref
    (nothing live covers the address) is still flagged. Verified with
    `tests/test_dangling_variadic_stack_spilled.c` (the stack-spilled
    `after8`/`wide` shapes from the ticket) plus manual probes of local
    array-indexing collisions
  - **Second consumer (#648):** the epoch/interval bookkeeping above
    (`frame_epochs`, `live_epochs`, `stack_intervals`) is not exclusive to
    `--dangling-pointers` — `__builtin_dynamic_object_size` also stabs
    `stack_intervals` (via `DYNOBJSZ`) to size an escaping fixed-size stack
    array/struct/union reached through an opaque pointer, using the exact
    same max-epoch resolution and live-epoch trust check as layer 3 above.
    Using that builtin anywhere in the program activates this bookkeeping on
    its own, independently of `-1`/`-2`/`-3` — see [VM.md](VM.md)'s
    `DYNOBJSZ`/`STKTAG` opcode rows and [COVERAGE.md](COVERAGE.md)'s
    `__builtin_dynamic_object_size` entry.
  - **Lazy per-function push (#703):** activation is scoped to the functions
    that actually need it, not every call in the program. `ENT3` pushes a
    frame epoch only when that function's own body emits `STKTAG` (an
    escaping aggregate local/param) or a recorded `LEA3` (an escaping
    scalar, under `--dangling-pointers`) — the only two consumers of the
    *current top* epoch. A function with no escaping local/param of its own
    (the common case — plain wrapper/leaf functions) pushes nothing and is
    simply absent from `frame_epochs` for its entire activation; `LEV3`/
    `CALLT` retire an epoch only when the top entry's saved `bp` matches the
    frame currently unwinding, so this is self-synchronizing and needs no
    per-frame flag on the teardown side. This means a program that uses
    `__builtin_dynamic_object_size` sparingly (e.g. one `FORTIFY`-style
    wrapper) in an otherwise call-heavy program pays the epoch push/pop cost
    only in the functions on the path to that wrapper's escaping buffer, not
    on every call in the program — the same shape of win #676 made for
    `LEA3` recording, but for the `ENT3`/`LEV3` push/pop itself. See
    [VM.md](VM.md)'s "Lazy per-function activation" note.
  - Recording is pruned to addresses that provably *escape* their creating
    frame (#676) — a post-parse pass (`mark_addr_escapes` in `src/parse.c`)
    marks a local's `Obj.addr_escapes` when its address (or an array/struct
    it owns, interior-aware through `&arr[i]`/`&s.field`, or pointer
    arithmetic on that base such as `arr + i`, #718) is observed as a call
    argument, a `return` operand, or stored into a pointer/aggregate
    lvalue; `LEA3` carries a `LEA3_NO_RECORD` flag (set at codegen time) that
    tells `op_LEA3_fn` to skip the `stack_ptr_epochs` write whenever the
    creating var's address was never marked. This is the *opposite* polarity
    from the pre-#676 escape analysis mistake below: an **unmarked** local
    still records unconditionally (safe default), and only a *proven*
    non-escaping address is pruned — so a pattern the pass fails to
    recognize just costs a wasted hashmap entry, never a missed catch.
    Measured ~16% overhead on a call-heavy `-3` microbenchmark before this
    change; pruning is skipped (recording stays on) for any address that
    reaches a call/return/pointer-store, so that worst case is unaffected —
    the win is on the common non-escaping case (loop counters, in-place
    accumulators, and all compiler-internal `LEA3`s: static links, block
    descriptors, closure captures, and other slot addresses that only ever
    feed one immediate load/store)
  - This replaces the pre-#670 scope-exit check (removed in #669), which
    conflated "address taken" with "escaped" — it tracked every `&local` via
    the (now removed) `MARKA` opcode and aborted whenever any of them was
    still present at *function* exit, which is every non-consumed `&local`,
    not just ones that actually escaped. It had zero true-positive coverage
    and aborted on fully benign code (e.g. `int *p = &n; *p = 5;`)
- `--alignment-checks` **Pointer alignment validation**
  - CHKA opcode validates pointer alignment before dereference
  - Checks that `pointer % type_size == 0`
  - Detects misaligned memory access (e.g., `int*` at odd address)
  - Only checks types larger than 1 byte
- `--provenance-tracking` **Pointer origin tracking**
  - Tracks pointer provenance: HEAP, STACK, or GLOBAL
  - MARKP opcode records origin when pointers are created
  - Automatically tracks heap allocations in MALC opcode
  - HashMap stores: pointer → {origin_type, base, size}
  - Enables validation of pointer operations within original object bounds
- `--invalid-arithmetic` **Pointer arithmetic bounds checking**
  - Requires `--provenance-tracking` to be enabled
  - Checks that `ptr` stays within `[base, base+size]` after arithmetic
  - Detects out-of-bounds pointer computations before dereference
  - Prevents pointer escape from original object
- `--stack-instrumentation` **Stack variable lifetime and access tracking**
  - Tracks all stack variable lifetimes with full block-level scoping
  - SCOPEIN/SCOPEOUT opcodes mark scope entry/exit for each `{ }` block
  - CHKL opcode validates variable is alive before access, keyed by the
    variable's actual runtime address (bp+offset) — not by stack offset or
    scope_id alone, so two functions whose locals land at the same offset, or
    recursive calls of the same function, don't collide with each other's
    liveness state (#671)
  - MARKR/MARKW opcodes track read/write counts for each variable, aggregated
    across all activations (including recursive calls) of that declaration
  - **Note on detection coverage:** `CHKL` is a liveness *guard*, not a
    dangling-pointer *detector* — it only checks that a local being accessed
    by name is currently in scope, which ordinary C code can't violate (a
    local's name isn't visible outside its own lexical scope). In practice it
    no longer fires in correct programs after #671; catching a genuine
    use-after-return through a pointer dereference is `--dangling-pointers`'
    job (see above, #670)
  - Stack overflow detection: tracks high water mark, warns at 90% threshold
  - `--dangling-pointers` is independent of this flag — its check lives in
    `CHKP3`, not in the SCOPEIN/SCOPEOUT/CHKL machinery described here
  - Use `--stack-errors` flag to enable runtime errors (vs logging only)
  - Use `cc_print_stack_report()` API to print access statistics
- `--format-string-checks` **Format string validation**
  - Validates format strings at compile time via `__attribute__((format(printf/scanf, …)))`
  - Gated by `-F` / `--format-string-checks` or included in safety presets `-S1` / `-S2`
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
- `--memory-tagging` **Temporal memory tagging**
  - Tracks generation counter for each heap allocation in AllocHeader
  - Records pointer→creation_generation in a host-memory side table (HashMap) at malloc time
  - Increments generation counter when memory is freed (MFRE opcode)
  - CHKP3 opcode validates pointer's stored generation matches current header generation
  - Detects use-after-free with generation detail: shows which generation the pointer was born at
  - Requires VM heap mode (on by default; not compatible with disabling it via `-V`)
  - Uses `hashmap_put_int` / `hashmap_get_int` for O(1) pointer tag lookup
  - **Limitation:** side table is keyed by address; if freed memory is reallocated at the same
    address (free-list path), the old entry is overwritten and stale pointers to that address
    are not detected. The current bump allocator never reuses addresses, so this is not a
    practical concern unless the free list is activated.
- `--control-flow-integrity` **Control flow integrity (CFI)**
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
- `-V` / `--no-vm-heap` **VM heap allocator** — see [VM Heap Allocator](#vm-heap-allocator) below;
  as of #665 this flag *disables* the VM heap (it's on by default) and cannot be combined with
  `-1`/`-2`/`-3` or `--safety=basic/standard/max`, nor (as of #845) with any individual flag
  whose checks key off the VM heap's `AllocHeader` metadata: `--bounds-checks`,
  `--uaf-detection`, `--type-checks`, `--heap-canaries`, `--memory-leak-detection`,
  `--memory-tagging`, `--pointer-sanitizer`. All of these are hard compile-time errors, not a
  silent no-op — before #845, `-V --bounds-checks` compiled and ran with the requested check
  never firing (it segfaults on the exact out-of-bounds write it was supposed to trap, instead
  of trapping it).

### Checked Pointers

A Checked C-inspired spatial-safety layer (#770/#482/#483/#484): a pointer's
declaration carries a *bounds* obligation, and every dereference through it
is checked at runtime against that declaration.

**Attribute syntax.** The attributes attach in post-`*` qualifier position,
the same grammar slot as `const`/`volatile`/`restrict` — not in declspec
position, which would qualify the pointee rather than the pointer:

```c
int  * [[cccc::single]]                    p;  // ~ Checked C _Ptr<int>
int  * [[cccc::array, cccc::count(n)]]     a;  // ~ _Array_ptr<int> : count(n)
char * [[cccc::ntarray, cccc::count(n)]]   s;  // ~ _Nt_array_ptr<char>
```

`__attribute__((...))` and the `@`-prefix spellings work too
(`@array`/`@count(n)`/etc.).

| Attribute | Meaning |
|---|---|
| `single` | Points to exactly one object; all pointer arithmetic on it is a compile error; deref is implicitly range-checked against `[p, p + sizeof(T))`, which is also a NULL check |
| `array` | Points into an array-like region; a bounds form (below) declares its extent |
| `ntarray` | Like `array`, but widens the checked range by one element (`sizeof(T)` bytes at the declared end of the range) for a null-terminator slot, under all three bounds forms; writing that slot is permitted **only with a null value** (#923/#938) |
| `count(n)` | The pointer is valid for `n` elements from its own current value: `[p, p + n*sizeof(T))` |
| `byte_count(n)` | Like `count(n)` but `n` is a byte count, not an element count: `[p, p + n)` |
| `bounds(lo, hi)` | Explicit absolute range `[lo, hi)`, independent of the pointer's own value |
| `bounds(unknown)` | Trust escape hatch: the type is checked, but no runtime range check is ever emitted for it |

A bounds form (`count`/`byte_count`/`bounds`) requires `array` or `ntarray`;
it is a compile error on `single` (which has its own implicit range) or with
no checked kind at all. `array`/`ntarray` with no bounds form at all is
legal but unchecked — nothing to enforce, same effect as `bounds(unknown)`.

**Bounds declarations** (#483) can reference any other in-scope
parameter, local, or global — including a *later* parameter of the same
function (`count(n)` before `int n` in the parameter list). A bounds
expression must be side-effect-free (`count(i++)` is a compile error): it is
**re-evaluated at every checked access**, not just once at the declaration,
so a side effect would run once per access instead of once. The check
applies inside a ternary's branches too — `count(c ? i++ : 3)` is rejected
just like `count(i++)` — but a *pure* ternary is accepted, including GNU
elvis (`count(n ?: 8)`, which desugars without a compiler temp specifically
so a pure elvis bounds expression stays side-effect-free). A
prototype-only declaration (`void f(int * [[cccc::array, cccc::count(n)]]
p, int n);`, no body) leaves its bounds unresolved — there is nothing to
check at the declaration site, and caller-side checking is future work
(#488) — this is correct, not an error.

**Struct/union member bounds** (#921). A bounds expression on a member may
reference a *sibling* member — in either textual order, the same "may name a
later declaration" rule bounds on a parameter already have — or a global
declared before the struct:

```c
struct S {
    int n;
    int * [[cccc::array, cccc::count(n)]] p;  // count(n) -- a sibling field
};
```

`n` in `count(n)` resolves relative to whichever struct/union *instance* is
actually being accessed (`s.p[i]` checks against `s.n`; `t.p[i]` against
`t.n`) — not to a single fixed scope the way a local's or parameter's bounds
are. Every access spelling reaches the same member-relative base:
`s.p[i]`, `sp->p[i]`, `(&s)->p[i]`, `(*sp).p[i]`. An object expression with
side effects (`f()->p[i]`) is declined — no check is emitted — permanently,
not merely because of an evaluation-count concern that a smarter
implementation could remove: the hoist (below) rewrites the *bounds
expressions* a check reads, not the access itself, so `f()->p[i]`'s own
access still calls `f()` once regardless of whether a check is emitted.
Instrumenting it would therefore call `f()` an extra time just to build the
hoisted temp, and a `--checked-pointers` build introducing an extra
evaluation of a side-effecting expression over a default build is not an
acceptable trade for the check. A non-trivial, side-effect-free object
expression (a runtime index, e.g. `k` in `arr[k].p[i]`) is evaluated exactly
once per checked access, into a compiler-generated temp shared by every
bound it feeds — not once per bound the way a bare local's re-cloned
expression is (free either way, since that folds to a stack-frame offset).
The same once-per-access-site treatment also applies to bounds propagation
across assignment and assignment-time bounds implication (below): a
non-trivial object expression in `q = arr[k].p;` or `arr[k].p = src;` is
evaluated once for that whole assignment's bounds, not once for propagation
alone. Two restrictions specific to member bounds:

- An identifier resolving to a **local** of whatever scope the struct
  happens to be *defined* in (as opposed to a sibling member or a global) is
  a compile error — the struct type can outlive that local, so trusting it
  would be unsound.
- A sibling that is itself a **bit-field** is a compile error — nothing
  extracts a bit-field's value through the member substitution this
  performs.

`[[cccc::single]]` and `bounds(unknown)` on a member work exactly as they do
on a variable, with no member-specific restriction.

- `(p + k)[i]` checks against `p`'s own declared bounds — arithmetic
  *within a single expression* still resolves back to the declared pointer.
- For `count(n)`/`byte_count(n)` (but not `bounds(lo, hi)`, which is already
  absolute), the *lower* bound at each access is the pointer's own live
  value at that point in the program — so reassigning `p` itself (not a
  different variable) and then accessing through it uses the new value, both
  as the base address and as the lower bound.

**Bounds propagation across assignment** (#919). `q = p + k;` on its own
still checks against `q`'s own declared bounds, not `p`'s — a plain
unchecked `q` has none, so by itself this would mean no check at all. But
when `q`'s *declaration* is one specific, statically-provable-safe shape, its
bounds are propagated: `q[i]` is checked against a **snapshot** of `p`'s own
absolute `[lo, hi)` range, taken at the moment of assignment — not against
`p`'s live declaration re-evaluated at the access (the way a direct access
through `p` itself works). This is deliberately a *simpler*, cheaper
guarantee than real dataflow analysis: no kill-set tracking over `p` or the
variables its bounds formula references, because the range is captured as
two absolute addresses (in a pair of compiler-generated hidden locals) the
instant the assignment runs, and later mutation of `p`/`n` is irrelevant to
an already-taken snapshot.

A pointer local `q` (declared with or without an initializer) is a
propagation *candidate* iff it is unchecked (`checked_kind == CHECKED_NONE`
— a pointer with its own declared bounds never needs propagation). Every
candidate is classified into exactly one of three outcomes:

- **NONE** — never assigned a checked-rooted value anywhere in the
  function, or address-taken outside the hidden temporary `to_assign()`
  builds for `q += k`/`q++`/`q--`. No propagation at all; `q[i]` is never
  checked, same as an ordinary unchecked pointer.
- **FULL** — **every** assignment to `q` reached in the function is
  checked-rooted (its address expression resolves, through `+`/`-`/casts,
  to a declared checked pointer variable or a #921 member access with an
  enforceable bounds form). `q[i]` is checked against a snapshot taken at
  the most recent assignment, unconditionally — the original #919/#941
  rule, unchanged.
- **OPT** (#942) — a *mix* of checked-rooted and non-checked-rooted
  assignments (or a checked-rooted assignment whose own source is itself
  OPT, see "Chained propagation" below). `q[i]` is still checked, but only
  on the paths where the value `q` actually holds at that point came from a
  rooted assignment — see below for how this is decided without any
  control-flow analysis.

Concretely, for the FULL case:

```c
int * [[cccc::array, cccc::count(n)]] p = ...;
int *q = p + 2;   // propagates: snapshot taken here, [p, p + n*4)
q[i];             // checked against the snapshot
q++; q[0];        // still checked -- q++ doesn't re-assign q's snapshot,
                  // and doesn't need to: the range is absolute, not
                  // relative to q's current value
q = p + 5;        // re-snapshots -- a later access uses the NEW range

int *s = malloc(...);
s = p;     // s's OWN initializer wasn't checked-rooted -- s is OPT, not
           // FULL (see below), NOT poisoned to NONE the way #919 alone
           // would have left it
```

**Path-sensitive propagation via runtime snapshot validity** (#942). An
OPT candidate's `[lo, hi)` snapshot temps are refreshed at **every**
assignment to `q`, not just the checked-rooted ones: a non-checked-rooted
store writes an explicit "invalid" sentinel (`lo = (char*)-1, hi =
(char*)0` — an inverted range no legitimate bound can ever produce) instead
of leaving the temps untouched, and the temps are also seeded with the
sentinel at function entry (covering an uninitialized declaration, a loop
back-edge that hasn't looped yet, or a `goto` past `q`'s own declaration).
The runtime check itself (`CHKRO`, distinct from the `CHKR` a FULL candidate
or a direct declared-checked access still emits) treats the sentinel as a
deliberate no-op rather than a violation.

This makes propagation exact per *executed* path with **no CFG, no join
operator, and no fixpoint over a control-flow graph at all** — a
conservative static join would still leave `q[i]` below unchecked, because
the fact is only live on one incoming edge of the `if`:

```c
int * [[cccc::array, cccc::count(n)]] p = ...;
int *q = malloc(...);
if (c) q = p;
q[i];   // ENFORCED when c took the `q = p` branch, silently unchecked
        // (no trap either way) when it didn't -- exactly per the path
        // actually taken at runtime, decided by which store actually ran

int *r;    // no initializer -- still a candidate (#942 extended
           // registration to cover this; #919 never registered `r` at all)
if (c) r = p;
r[i];   // same as `q` above: enforced iff `c` was true THIS run

int *s = p;
s[i];       // checked: s's most recent store (the init) was rooted
s = malloc(...);
s[j];       // NOT checked: s's most recent store was not rooted -- ordinary
            // flow-sensitivity/kill-set behavior falls out of the same
            // runtime mechanism for free, no separate tracking needed
```

A FULL candidate gets none of this machinery — no sentinel, no entry init,
plain `CHKR` — so its codegen is byte-for-byte identical to before #942.

A #921 member is a valid propagation *source* too, so the two features
compose: `int *q = obj.p;` propagates from `obj.p`'s own member bounds.

A self-rooted reassignment is **neutral**: `q = q + 1;` (or any store whose
rhs resolves, via `find_checked_base()`, straight back to `q` itself) neither
poisons `q` nor gets rewritten — the snapshot is an absolute `[lo, hi)` range,
so a self-store cannot invalidate it, the same fact that already lets a plain
`q++`/`q += k` preserve propagation. This is narrowly about the *left-hand
side's own descent*: `q = 1 + q;` is not recognised (`find_checked_base()`
only ever descends `->lhs`, consistent with how `q = 1 + p;` isn't recognised
as checked-rooted either) and still poisons `q`, conservatively. The
carve-out excludes `q`'s own declaration: `int *q = q;` has no prior value of
`q` to have been rooted in anything, so the initializer itself still poisons.

**Chained propagation** (#941). A local that is itself only propagated (never
declared checked) *can* act as a source for a further candidate:

```c
int * [[cccc::array, cccc::count(n)]] p = ...;
int *q = p + 2;   // propagates from p (a declared source)
int *r = q + 1;   // propagates from q (a chained source)
int *s = r + 1;   // propagates from r -- chains to arbitrary depth
```

This is decided by iterating the whole-function classification rule above to
a fixpoint: round 0 accepts only declared-checked sources as a *source*
(today's #919 rule); each later round additionally trusts whichever
candidates survived the *previous* round as sources too, so the
accepted-source set grows monotonically and the pass always terminates
(bounded by the number of candidates in the function, with a defensive round
cap on top). Seeding from the bottom (declared sources only, then growing)
rather than optimistically from the top (assume everything propagates, then
remove failures) is what keeps an unrooted cycle from ever self-validating:
`q = r + 1; r = q + 1;` with no declared root anywhere in the cycle never
propagates (classifies NONE), no matter how many rounds run, because neither
`q` nor `r` is ever a member of any round's *frozen* accepted-source set.

Chaining needs one more soundness argument beyond #919's own: a chained
source's bounds are runtime values living in its own snapshot temps, not a
recomputable expression, so `q`'s temps must be written before `r`'s
snapshot reads them. That holds because a chain source's own temps are
refreshed at *every* one of its assignments (rooted → real bounds, non-rooted
→ sentinel, per #942 above) plus at function entry, so `q`'s temps are always
in a well-defined state — real or sentinel — by the time any use of `q`,
including `r`'s snapshot read, is reached. `goto` past `q`'s own declaration
is covered by the same phase-B' entry sentinel, not left as a residual (see
below).

**#942 also threads OPT-ness through the chain**: a candidate chained from an
OPT source is itself OPT, even when its own single store is, in isolation,
unconditionally checked-rooted:

```c
int * [[cccc::array, cccc::count(n)]] p = ...;
int *q = malloc(...);
if (c) q = p;      // q is OPT
int *r = q + 1;    // r's own store IS rooted (from q) -- but r must still
                    // be OPT, not FULL, or r[i] would read q's sentinel
                    // through a plain CHKR and TRAP on this correct,
                    // unrooted-path code
r[i];              // enforced iff `c` was true this run, same as q[i] would be
```

The round loop's fixpoint test compares both the survivor count and the
OPT-survivor count between rounds; if the round cap is hit before either
converges, every remaining survivor is conservatively forced to OPT rather
than trusted as FULL — OPT only ever costs precision, so this is the only
sound direction to err in when the cap cuts a chain short.

**`CHKNT` propagation** (#943). The terminator-slot fact travels alongside
the snapshotted `[lo, hi)` range, FULL, OPT, and chained alike: a candidate's
`Obj.checked_prop_nt_elem` is non-zero iff *every* checked-rooted store into
it — declared source directly, or transitively through a #941 chain — is
rooted at an `[[cccc::ntarray]]` source, and every one of those sources
agrees on the same pointee element size. A mix (one `ntarray`-rooted store
and one plain-`array`-rooted store to the same candidate, or two
`ntarray`-rooted stores with different element sizes) disables the guard for
the whole candidate — on the plain-`array` path `hi` is never widened, so
`hi - elem_size` would be the *last real element*, not a terminator slot;
guarding it would falsely trap a legitimate write there:

```c
char * [[cccc::ntarray, cccc::count(n)]] s = ...;
char *q = s;
q[3] = 'x';        // traps: q's hi is a widened ntarray hi (element size 1)

int *r = (int *)s; // cast changes the pointee size -- CHKNT declines here:
r[0] = 1;          // access_size (4) != the source's element size (1)
```

An OPT candidate's own sentinel range (`[(char*)-1, (char*)0)`) no-ops the
same way it already does for `CHKRO` — `CHKNT` already declines whenever
`hi < elem_size`, and the sentinel's `hi == 0` satisfies that unconditionally
for any real element size, so no sentinel-aware variant of `CHKNT` was
needed. Read-modify-write through a propagated pointer (`q[n] += 1`,
`q[n]++`) and the `_Atomic` compare-exchange desugar are both covered too:
`to_assign()` (src/parse.c) desugars an RMW at **parse time**, before this
pass has resolved `q`'s bounds, so it leaves a back-link
(`Node.checked_rmw_mirror`) from the original deref to the synthesized store
node it built; the propagation pass's attach walk follows that link and
mirrors the just-resolved bounds — lo/hi/access-size/nt-terminator/optional
for the non-atomic desugar, hi/access-size/nt-terminator only for the atomic
one (matching #937's own direct-access split, which has no `lo` to give the
CAS-loop's `ND_CAS` node) — onto the mirror once they're known.

**Assignment-time bounds implication** (#944, Checked C's
`_Assume_bounds_cast` direction). The propagation pass above only ever
*widens* trust into a previously-*unchecked* target; a **separate** sibling
pass, `verify_checked_assign_bounds()` (disjoint target set — propagation
candidacy requires `checked_kind == CHECKED_NONE`, so an already-declared-
checked target is never a propagation candidate), now *verifies* trust the
other direction: an `ND_ASSIGN` whose LHS is itself declared checked, and
whose RHS is rooted at a **directly declared-checked** source (not a #941
chain — v1 boundary, see below), is rewritten so a new `CHKAB` opcode checks
that the source's own bounds imply the target's own declared bounds:

```c
int * [[cccc::array, cccc::count(4)]]  p = ...;   // declared bound: 4
int * [[cccc::array, cccc::count(10)]] q;         // declared bound: 10
q = p;   // now traps: p's bounds (4) don't imply q's own declared bounds (10)
```

`CHKAB rs_val, rs_slo, rs_shi` traps unless `slo <= val && val <= shi`;
codegen emits it twice per checked assignment — once with the target's own
declared `lo`, once with its `hi` — which together enforce
`[dlo, dhi) ⊆ [slo, shi]`. Ordering is the **inverse** of the propagation
pass's own snapshot: the target's declared bounds are self-referencing
(`[q, q + m*sizeof(T))` for a `count(m)` target), so `checked_assign_dst_lo/
hi` are deliberately left as bare expressions, evaluated **after** the
store, to see the just-written value — the source's bounds, by contrast, are
snapshotted into compiler-generated temps **before** the store (the rewrite
is `(temp = source bounds), (q = E)`, mirroring #919's own before-the-store
snapshot), since the source expression may itself alias or be overwritten by
the assignment.

v1 boundary, deliberate, not a residual gap:

- The RHS must be a **directly declared-checked** source
  (`checked_prop_source_bounds()`'s kind 1) — a #941-propagated local can
  hold the OPT sentinel range, which would need a sentinel-aware `CHKAB`
  variant; `q = r;` where `r` is itself only a propagation candidate is
  silently skipped, same as any other unrecognised source.
- Only a direct `q = E;` `ND_ASSIGN` is covered — function argument passing
  and return values are not.
- A target with no resolvable bounds form (`CB_NONE`/`bounds(unknown)`) is
  skipped, same rationale as the propagation pass's identically-named source
  exclusion.

Gated on `--checked-pointers` at **parse time**, unlike the rest of the
checked-pointer machinery (which populates its fields unconditionally and
gates only `CHKR`'s *emission* at codegen) — the snapshot temps cost real
stack slots and stores per propagating assignment, only worth paying when
something might enforce them. This has no pragma-ordering caveat: `#pragma
cccc config(checked_pointers = true)` is resolved during preprocessing, a
pass that runs to completion for the whole file before parsing begins, so
the flag is already set by the time any declaration is parsed regardless of
where in the file the pragma appears.

**Runtime enforcement is opt-in, off by default**: `--checked-pointers` /
`#pragma cccc config(checked_pointers = true)`. Not part of any
`-0`/`-1`/`-2`/`-3` preset. The compile-time rules (attribute parsing, type
checking, the arithmetic-rejection diagnostic on `single`) are always on
regardless of this flag — only the `CHKR` runtime check itself is gated.

**Native and serialized output.** `CHKR` enforcement is VM-only, by design —
the same as every other runtime safety flag in this document; there is no
equivalent check emitted into `-c=native`, `-m`/`--dump-expanded`, or
`-c=generated` output. `--checked-pointers` is accepted in those
modes but has no effect there — a `-c=native`/`-m`/`-c=generated ignores VM runtime
safety/debug options` warning is printed and the flag is dropped, it does
not error. The `[[cccc::single/array/ntarray]]`/`count`/`byte_count`/`bounds`
attributes themselves are always stripped from `-E`/`-m`/`-c=generated`/`-c=native`
output regardless of the flag (ABI-transparent, no change to unchecked
callers — see #482/#488), so native builds compile and run declarations
carrying checked-pointer attributes, they just get zero bounds enforcement
from them. The compile-time contract checks — `single`-pointer arithmetic
rejection, bounds side-effect rejection, a member bounds expression naming
an enclosing local or a bit-field sibling — are frontend checks independent
of `--checked-pointers` and still apply in every mode, including native.

**Why this exists**: `CHKB` (`--bounds-checks`) derives its bound from
`AllocHeader.size`, which only exists for VM-heap allocations — it has no
upper bound at all for a stack or global array. Checked pointers close that
gap: their bound comes from the declaration, not allocation metadata, so
`--checked-pointers` catches out-of-bounds accesses on stack and global
arrays that `--bounds-checks` structurally cannot. See
[VM.md](VM.md#safety-opcodes) for the `CHKR` opcode itself.

```c
int * [[cccc::array, cccc::count(n)]] a = some_stack_array;
a[n]; // traps under --checked-pointers; CHKB cannot catch this at all
```

**Terminator invariant** (#923/#938/#939). `ntarray`'s bounds widening
(`man/SAFETY.md`'s `ntarray` row above) makes a one-element terminator slot
writable at the end of the declared range, but it says nothing about what
may legally be written there. Two opcodes close the soundly-checkable part
of that gap: `CHKNT` traps a store of a **non-zero** value into the widened
terminator slot, keyed off the same `[lo, hi)` `CHKR` already computed for
the access (`addr == hi - elem_size && val != 0`); `CHKNTZ` (#939) traps a
store of a **non-all-zero-bytes** value for pointees whose store never
passes through a single value register. Both run under the same
`--checked-pointers` flag as `CHKR`, no separate opt-in.

**Which pointee types are guarded** (#939):

| Pointee | Opcode | How |
|---|---|---|
| Integer (`char`/`int`/.../`_BitInt`), pointer | `CHKNT` | value register, compared to 0 |
| `float`, `double` | `CHKNT` | value's raw bits transferred into an integer register first (`FR2R`/`FR2R_F32`), then compared to 0 -- so `-0.0`/`-0.0f` (a non-zero bit pattern) traps, even though it compares equal to `0.0` |
| `struct`, `union`, wide `_BitInt`, `_Decimal32/64/128` | `CHKNTZ` | source bytes scanned for any non-zero byte, **before** the underlying `memcpy` runs -- the slot is never actually clobbered when this traps. All bytes of the object must be zero, including padding: `= {0}` and compound-literal initializers zero the whole object first (`ND_MEMZERO`), so those patterns are safe, but a struct assembled member-by-member with stale/uninitialized padding can trap even when every named member reads as its own zero value |
| `long double`, vector, `_Complex`, anything else | *(unguarded)* | `long double`'s widened terminator slot is 16 bytes (`sizeof`), but the actual store is an 8-byte flat-double `FSTR` -- a guard would assert bytes it never inspected, a false assurance rather than an honest gap. Vectors and `_Complex` are excluded for the same reason: no opcode reads their full stored representation into a scannable register or byte range today |

`CHKNTZ` only guards a **whole-object** store through the pointer itself
(`tbl[n] = (Option){...}`); writing a single field of the terminator slot in
place (`tbl[n].a = 1;`) is not guarded at all -- see "Known coverage gaps"
below.

```c
struct Option { int a; char b; };
struct Option * [[cccc::ntarray, cccc::count(n)]] tbl = ...;
tbl[n] = (struct Option){0, 0}; // ok -- all-zero-bytes terminator
tbl[n] = (struct Option){1, 0}; // traps under --checked-pointers -- CHKNTZ

double * [[cccc::ntarray, cccc::count(n)]] a = ...;
a[n] = 0.0;  // ok
a[n] = -0.0; // traps -- non-zero bit pattern, even though -0.0 == 0.0
```

The widening rule is the same regardless of which bounds form declared the
range: **the terminator slot is the `elem_size` bytes beginning at the
declared end of the range.**

| Bounds form | Declared end | Terminator slot |
|---|---|---|
| `count(n)` | `p + n*sizeof(T)` | `[p + n*sizeof(T), p + (n+1)*sizeof(T))` |
| `byte_count(n)` | `p + n` | `[p + n, p + n + sizeof(T))` |
| `bounds(lo, hi)` | `hi` | `[hi, hi + sizeof(T))` |

`byte_count`'s widening is byte-granular, not element-granular: a
`byte_count(n)` whose `n` is not a multiple of `sizeof(T)` still widens by
exactly `sizeof(T)` bytes, so the resulting slot is not aligned to an
element boundary and no element-indexed access (`s[i]`) can ever land on it
— `CHKR` still admits the extra bytes, but `CHKNT` has nothing reachable to
guard there. This is a pre-existing property of declaring a `byte_count`
that doesn't divide evenly by the element size, not a new gap.

```c
int n = 3;
char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
s[3] = '\0'; // ok -- terminator slot, still null
s[3] = 'x';  // traps under --checked-pointers -- non-null write to the slot
```

`CHKNT` deliberately does **not** attempt the other half of the invariant —
verifying a null terminator is actually *present* somewhere in the declared
range. That is not soundly checkable from the declaration alone: Checked C's
`count(n)` on an `_Nt_array_ptr` is a **lower** bound on the valid extent, not
an assertion that a terminator exists within it —
`_Nt_array_ptr<char> s : count(0)` is a legal, terminator-free declaration,
and `count(5) = "hello world"` (no null in the declared range at all) is
conforming too. A check that scans `[lo, hi)` for a null and traps on absence
would false-positive on both. Worse, actually *locating* the real terminator
requires reading past `hi` — exactly the unbounded read the bounds
declaration exists to prevent. So presence validation is not implemented;
`CHKNT` only ever looks at the one slot the widening itself makes legal to
write.

Read-modify-write on the terminator slot (#937) is covered as directly as
plain assignment. `s[n] += 1` and `s[n]++` desugar (`to_assign()`,
`src/parse.c`) to `tmp = &s[n]; *tmp = *tmp + 1` — the synthesized `*tmp`
store deref now carries the same `checked_bounds_lo/hi`/
`checked_nt_terminator` as `s[n]` itself, so `CHKNT` traps it exactly like a
direct `s[n] = 1` (this includes the float/double bit-pattern-transfer path,
e.g. `a[n] += 1.0`). An `_Atomic`-qualified `ntarray` element's RMW takes a
separate CAS-loop desugar (`compare_and_swap` under the hood) instead of a
plain store; that path gets its own `CHKNT` emission ahead of the `ACAS`
opcode, checking the CAS's *desired* value — so an attempted non-null write
still traps even on a CAS iteration that would have failed the compare.
`CHKNTZ` has no CAS-loop counterpart: C has no `+=`/`++`/`--` operator on a
struct/union/wide-`_BitInt` value, so `to_assign()`'s CAS-loop desugar never
applies to those pointees — only plain assignment (`tbl[n] = ...`) or the
non-atomic RMW desugar (`tmp = &A; *tmp = *tmp op B`, which lowers to an
ordinary `CHKNTZ`-guarded store, same as any other) can reach one. `_Decimal`
does support `+=`/`++`/`--`; its `_Atomic` CAS-loop path is unaffected by
this ticket either way — `ND_CAS`'s own type check
(`src/codegen.c`, `sz` must be 1/2/4/8 and not `is_flonum`) governs whether
an `_Atomic _Decimal` RMW is accepted at all, independent of `CHKNTZ`.

**`CHKNT`/`CHKNTZ` propagate across assignment** (#943, extended #939). `q =
s;` where `s` is an `[[cccc::ntarray]]` source makes `q[n] = 'x'` trap
through `q` exactly the same way `s[n] = 'x'` traps directly — FULL, OPT,
and chained (#941) propagation candidates alike, through the
read-modify-write and `_Atomic` compare-exchange desugars, and (#939) for
struct-typed sources too. See "Bounds propagation across assignment" above
for the full mechanism (element-size matching, the mixed-source conflict
rule, and the RMW back-link) — propagation keys off element size only, so
the same size-match rule now also decides `CHKNTZ` attachment for a
propagated aggregate access.

Known coverage gaps, left as follow-up work rather than built into this pass:

- **`CHKNT`/`CHKNTZ` do not attempt the presence half of the invariant** —
  see above.
- **A member-wise write into a `struct`/`union` terminator slot is
  unguarded** (#950). `CHKNTZ` only ever sees a whole-object store
  through the pointer itself (`tbl[n] = (Option){...}`, an `ND_DEREF`
  lvalue) — the same shape `CHKNT` has always required. Writing a single
  field of the terminator slot in place (`tbl[n].a = 1;`) stores through an
  `ND_MEMBER` lvalue instead, which `checked_nt_terminator` is never
  attached to (`set_checked_deref_bounds()`/`checked_prop_attach_scan()`
  only stamp it on the outer `ND_DEREF`), so neither `CHKNT` nor `CHKNTZ`
  fires — only `CHKR`'s ordinary range check runs. This gap has no scalar
  analogue: an integer/pointer/float pointee has no members, so this access
  shape doesn't exist for `CHKNT`. It is new specifically because `CHKNTZ`
  extends the guard to pointee types that *do* have members.

`malloc`/`free`/`calloc`/`realloc`/`aligned_alloc`/`posix_memalign` route through the VM heap
(`MALC`/`MFRE`/`CALC`/`REALC`/`MALCA`/`PMEMA` opcodes) by default at every safety level, including
`-0`. This is what makes the heap safety features below usable without any special opt-in in
normal code.

- `-V` / `--no-vm-heap` **turns the VM heap off**, reverting all of the above to the host allocator
  via FFI. It is only valid at safety level 0 (default or explicit `-0`) with none of
  `--bounds-checks`/`--uaf-detection`/`--type-checks`/`--heap-canaries`/
  `--memory-leak-detection`/`--memory-tagging`/`--pointer-sanitizer` set; combining it with any
  of those, `-1`/`-2`/`-3`, or `--safety=basic/standard/max` is a hard compile-time error since
  they all require the VM heap (#845).
- `free_sized`/`free_aligned_sized` (C23) are routed through the same `MFRE` opcode as `free`,
  so a VM-heap-allocated pointer freed through either call is handled correctly.
- `aligned_alloc`/`posix_memalign` (#668) are intercepted the same way as `malloc`/`calloc`, via
  `MALCA`/`PMEMA`: the bump allocator pads *before* the `AllocHeader` so the returned pointer
  meets the requested alignment while the header is still recoverable via
  `((AllocHeader*)ptr) - 1`, giving them the same canaries/bounds-checks/UAF-detection/
  type-checks/leak-detection/tagging coverage as `malloc`. Freeing their result with plain
  `free()`/`free_sized()`/`free_aligned_sized()` works via the normal `MFRE` path (no fallback to
  the host allocator needed, since the pointer now carries a VM-heap header).
- Zero overhead when no other safety features are enabled.

## FFI Safety Features

The allow/deny/disable policy applies to registered FFI calls and runtime
native symbols returned by VM-managed `dlsym`.

- `--ffi-allow=func1,func2` **FFI function whitelist**
  - Comma-separated list of allowed FFI function names
  - When allow list is non-empty, only listed functions can be called via FFI
  - Enforced at runtime during registered FFI and runtime `dlsym` calls
  - Use with `cc_ffi_allow()` API for programmatic configuration
- `--ffi-deny=func1,func2` **FFI function blacklist**
  - Comma-separated list of denied FFI function names
  - Prevents specific functions from being called via FFI
  - Only checked when allow list is empty
  - Use with `cc_ffi_deny()` API for programmatic configuration
- `--disable-ffi` **Disable all FFI calls**
  - Completely blocks all foreign function calls at runtime
  - Overrides both allow and deny lists
  - Useful for sandboxing untrusted code
- `--ffi-errors-fatal` **Make FFI errors abort execution**
  - By default, FFI safety violations print diagnostics, skip the call, zero
    the native return registers, and continue
  - With this flag, violations abort execution as runtime errors
  - Provides strict enforcement mode for production environments
- `--ffi-type-checking` **Runtime type validation on FFI calls**
  - Validates argument counts for registered FFI function signatures
  - Requires exact arity for non-variadic functions
  - Requires at least the fixed argument count for variadic functions
  - Runtime `dlsym` calls are policy-checked but do not have registered
    signatures for arity checking yet

## Floating-Point Behavior

- `--trap-fp-divzero` **Abort on float division by zero**
  - By default, `float`/`double` division by zero follows IEEE-754: a
    finite value divided by `0.0` produces a correctly-signed infinity
    (and raises `FE_DIVBYZERO`, observable via `<fenv.h>`), and `0.0/0.0`
    produces NaN (raises `FE_INVALID`). Neither is undefined behavior,
    unlike integer division by zero, so this is not gated by any `-0`
    through `-3` safety tier and is not part of `CCCC_ALL_SAFETY`.
  - With this flag, any zero divisor in a float/double division aborts
    execution with the same `DIVISION BY ZERO` diagnostic used by integer
    division and `--overflow-checks`, for debugging code that assumes
    division by zero is always a bug.

## Example Usage

### Use-after-free
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

### Double-Free Detection
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

### Bounds Checking
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

### Checked Pointers
```c
// test_checked_bounds.c - Checked-pointer bounds checking example
int main(void) {
    int n = 5;
    int stack_arr[5] = {1, 2, 3, 4, 5};
    int * [[cccc::array, cccc::count(n)]] a = stack_arr;
    int x = a[5]; // Out of bounds -- and CHKB cannot catch this at all,
                  // since stack_arr has no AllocHeader to derive a bound from.
    return x;
}
```

```bash
$ ./cccc --checked-pointers test_checked_bounds.c

========== CHECKED BOUNDS VIOLATION ==========
Checked-pointer access out of declared bounds
Address:       0x14fffffdc
Access size:   4 bytes
Declared bounds: [0x14fffffc8, 0x14fffffdc)
PC: 0xe5 (offset: 229)
================================================
```

```c
// test_checked_nt.c - Checked-pointer terminator-invariant example (#923)
int main(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    s[3] = 'x'; // Non-null write into the widened terminator slot
    return 0;
}
```

```bash
$ ./cccc --checked-pointers test_checked_nt.c

========== CHECKED TERMINATOR VIOLATION ==========
Non-null store into a [[cccc::ntarray]] terminator slot
Address:         0x157ffffeb
Terminator slot: [0x157ffffeb, 0x157ffffec)
Value stored:    120
PC: 0xed (offset: 237)
====================================================
```

### Type Checking
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

### Uninitialized Variables
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

### Integer Overflow Detection

#### Addition Overflow
```c
// test_overflow_add.c - Addition overflow example
#include "limits.h"

int main() {
    // This will overflow: LLONG_MAX + 1
    long long x = LLONG_MAX;
    long long result = x + 1;  // Overflow!
    return 42;
}
```

```bash
$ ./cccc --overflow-checks -I./include test_overflow_add.c

========== INTEGER OVERFLOW ==========
Addition overflow detected
Operands: 9223372036854775807 + 1
PC:       0x94e8000a0 (offset: 20)
======================================
```

#### Subtraction Underflow
```c
// test_overflow_sub.c - Subtraction underflow example
#include "limits.h"

int main() {
    // This will underflow: LLONG_MIN - 1
    long long x = LLONG_MIN;
    long long result = x - 1;  // Underflow!
    return 42;
}
```

```bash
$ ./cccc --overflow-checks -I./include test_overflow_sub.c

========== INTEGER OVERFLOW ==========
Subtraction overflow detected
Operands: -9223372036854775808 - 1
PC:       0x8948000e8 (offset: 29)
======================================
```

#### Multiplication Overflow
```c
// test_overflow_mul.c - Multiplication overflow example
#include "limits.h"

int main() {
    // This will overflow: LLONG_MAX * 2
    long long x = LLONG_MAX;
    long long result = x * 2;  // Overflow!
    return 42;
}
```

```bash
$ ./cccc --overflow-checks -I./include test_overflow_mul.c

========== INTEGER OVERFLOW ==========
Multiplication overflow detected
Operands: 9223372036854775807 * 2
PC:       0xb348000a0 (offset: 20)
======================================
```

#### Division By Zero
```c
// test_overflow_div.c - Division by zero example

int main() {
    int x = 42;
    int y = 0;
    int result = x / y;  // Division by zero!
    return 42;
}
```

```bash
$ ./cccc --overflow-checks test_overflow_div.c

========== DIVISION BY ZERO ==========
Attempted division by zero
Operands: 42 / 0
PC:       0x9b28000d0 (offset: 26)
======================================
```

#### Signed Division Overflow
```c
// test_overflow_div_signed.c - Signed division overflow
#include "limits.h"

int main() {
    // This will overflow: LLONG_MIN / -1 = LLONG_MAX + 1 (unrepresentable)
    long long x = LLONG_MIN;
    long long result = x / -1;  // Signed division overflow!
    return 42;
}
```

```bash
$ ./cccc --overflow-checks -I./include test_overflow_div_signed.c

========== INTEGER OVERFLOW ==========
Division overflow detected
Operands: -9223372036854775808 / -1
Result would overflow (LLONG_MIN / -1 = LLONG_MAX + 1)
PC:       0x81a800108 (offset: 33)
======================================
```

#### Normal Arithmetic (No Overflow)
```c
// test_overflow_none.c - Valid arithmetic with overflow checks enabled

int main() {
    // Normal arithmetic operations that don't overflow
    int a = 10 + 20;      // 30
    int b = 50 - 20;      // 30
    int c = 6 * 7;        // 42
    int d = 84 / 2;       // 42

    // All checks pass, program continues normally
    return 42;
}
```

```bash
$ ./cccc --overflow-checks test_overflow_none.c
$ echo $?
42
```

**Note:** Overflow detection is **disabled by default** for zero overhead. When enabled with `--overflow-checks`, codegen emits specialized checked arithmetic opcodes (ADDC, SUBC, MULC, DIVC) that validate operations before completing them. Floating-point operations are not affected by this flag.

### Stack Overflow Detection

#### Deep Recursion
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

#### Large Stack Frame
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

### Dangling Pointers

`--dangling-pointers` (also enabled by `-3` / `--safety=max`) enforces two
dereference-time checks in `CHKP3`: a range check (#670) and a per-frame
epoch liveness check (#673, layered on top to close the range check's gap).

```c
// test_dangling_pointer.c - Dangling stack pointer example
int *get_local_address() {
    int x = 42;
    return &x;  // Address of local variable -- fine to take, fine to return.
}

int main() {
    int *ptr = get_local_address();
    int value = *ptr;  // Dereference of a dangling pointer -- caught here.
    return value;
}
```

```bash
$ ./cccc --dangling-pointers test_dangling_pointer.c

========== DANGLING STACK POINTER ==========
Dereferenced a pointer into a stack frame that has already returned
Address:    0x15fffffc8
Current SP: 0x15fffffe0 (address is below live stack -> frame is dead)
Current PC: 0x14 (offset: 20)
============================================
```

The stack grows downward, so any address that falls inside the stack
reservation but below the current stack pointer belongs to a frame that has
already returned — that's the range check (#670) firing above, the cheap
common case.

**The deeper-call case (#673):** passing the dangling pointer one call
*deeper* and dereferencing it there defeats the range check alone, because
the deeper call's own frame reuses the same stack memory (the address is
`>= sp` again by the time it's dereferenced):

```c
// test_dangling_deref_deeper_call.c - Dangling deref through a deeper call
int *get_local(void) {
    int x = 42;
    return &x;
}

void use(int *p) {
    int y = *p;  // Dereference happens one frame deeper than get_local()'s
    (void)y;     // caller -- caught by the epoch check, not the range check.
}

int main(void) {
    int *p = get_local();
    use(p);
    return 0;
}
```

```bash
$ ./cccc --dangling-pointers test_dangling_deref_deeper_call.c

========== DANGLING STACK POINTER ==========
Dereferenced a pointer into a stack frame that has already returned
Address:    0x31fffffc8
Creating frame's epoch is no longer live (dereferenced through a deeper call, #673)
Current PC: 0x2d (offset: 45)
============================================
```

Every `&local` is tagged at the moment it's taken with the current frame's
liveness epoch; `get_local()`'s epoch dies at `LEV3` along with the rest of
its frame, so `use()`'s dereference is flagged regardless of what address
range `use()`'s own frame happens to occupy.

**The interior-pointer case (#675):** an interior stack pointer with a
runtime-computed offset (e.g. `&arr[i]` for non-constant `i`) doesn't go
through a single `LEA3` the way a plain `&local` or constant-offset
`&s.field` does — the base `arr` gets its own `LEA3`, but the final address
is computed by a separate runtime add, so it was never itself the address
tagged in `stack_ptr_epochs`. A deeper-call dereference through one of these
used to rely on the range check alone (and could be missed the same way #670
originally documented for plain locals) until `STKTAG` closed the gap:

```c
// test_dangling_interior_deeper_call.c - Dangling deref through an interior pointer
int *get_local(int i) {
    int arr[8];
    arr[i] = 42;
    return &arr[i]; // runtime offset -- not a single LEA3
}

void use(int *p) {
    int pad[8]; // reoccupies the same memory arr used, defeating the range check
    pad[0] = 0;
    int y = *p; // NOT caught by the range check alone; caught by #675.
    (void)y;
    (void)pad;
}

int main(void) {
    int *p = get_local(3);
    use(p);
    return 0;
}
```

```bash
$ ./cccc --dangling-pointers test_dangling_interior_deeper_call.c

========== DANGLING STACK POINTER ==========
Dereferenced a pointer into a stack frame that has already returned
Address:    0x167ffffb4
Creating frame's epoch is no longer live (dereferenced through an interior pointer into a deeper call, #675)
Current PC: 0x4d (offset: 77)
============================================
```

`STKTAG`, emitted right after `arr`'s base `LEA3` because `&arr[i]` escapes
via the `return`, retains `arr`'s `[base, base+size)` extent tagged with
`get_local()`'s epoch. `CHKP3`'s exact `stack_ptr_epochs` lookup misses (the
dereferenced address is interior, not the tagged base), so it falls through
to the interval-stabbing lookup, finds `arr`'s retained interval containing
the address, and flags it — same as the plain-local case, one layer deeper.

**Discarded-value dereferences are still checked (#916):** a dereference
whose result is unused (`*p;`, `(void)*p`) is checked exactly like one whose
result is kept — the result is discarded, not the access:

```c
int *p = get_local();
*p;  // Still caught, even though nothing reads the loaded value.
```

### Pointer Alignment
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

### Temporal Memory Tagging
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

### Control Flow Integrity
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

### FFI Deny List
```c
// test_ffi_deny.c - FFI deny list example

int printf0(const char *fmt);

int main() {
    printf0("Attempting to call blocked function\n");
    return 0;
}
```

```bash
$ ./cccc --ffi-deny=printf0 test_ffi_deny.c

========== FFI SAFETY ERROR ==========
Error type: FFI Access Denied
Function:   printf0
Details:    Function in deny list
PC offset:  12
======================================
```

### FFI Allow List
```c
// test_ffi_allow.c - FFI allow list example

int printf0(const char *fmt);
void *malloc(unsigned long size);

int main() {
    printf0("This call is allowed\n");
    malloc(100);  // This will be blocked
    return 0;
}
```

```bash
$ ./cccc --ffi-allow=printf0 test_ffi_allow.c

This call is allowed

========== FFI SAFETY ERROR ==========
Error type: FFI Access Denied
Function:   malloc
Details:    Function not in allow list
PC offset:  25
======================================
```

### Disable All FFI
```c
// test_disable_ffi.c - Disable all FFI calls

int printf0(const char *fmt);

int main() {
    printf0("This FFI call will be blocked\n");
    return 0;
}
```

```bash
$ ./cccc --disable-ffi test_disable_ffi.c

========== FFI SAFETY ERROR ==========
Error type: FFI Disabled
Function:   printf0
Details:    All FFI calls are disabled via --disable-ffi
PC offset:  12
======================================
```

### Fatal FFI Errors
```bash
# Default behavior: warnings only
$ ./cccc --ffi-deny=printf0 test.c
<FFI error printed, program continues, returns 0>

# Fatal mode: abort on error
$ ./cccc --ffi-deny=printf0 --ffi-errors-fatal test.c
<FFI error printed, program aborts as a runtime error>
```

### FFI Type Checking - Argument Count Mismatch
```c
// test_ffi_arg_count.c - Wrong number of arguments
int strcmp(const char *s1, const char *s2);

int main() {
    // strcmp expects 2 arguments, but only 1 provided
    int result = strcmp("hello");
    return result;
}
```

```bash
$ ./cccc --ffi-type-checking test_ffi_arg_count.c

error: FFI function 'strcmp': argument count mismatch (requires 2, called with 1)
```

### FFI Type Checking - Type Mismatch
```c
// test_ffi_type_mismatch.c - Wrong argument type
unsigned long strlen(const char *s);

int main() {
    int not_a_string = 42;
    // strlen expects char*, but we're passing int
    unsigned long len = strlen(not_a_string);
    return len;
}
```

```bash
$ ./cccc --ffi-type-checking test_ffi_type_mismatch.c

test_ffi_type_mismatch.c:7: FFI function 'strlen': argument 1 type mismatch
  Expected: char*
  Actual:   int
    unsigned long len = strlen(not_a_string);
                               ^
```

### FFI Type Checking - Valid Calls
```c
// test_ffi_valid.c - All types match correctly
void *malloc(unsigned long size);
void free(void *ptr);
int strcmp(const char *s1, const char *s2);

int main() {
    void *ptr = malloc(100);  // OK: unsigned long argument
    int cmp = strcmp("a", "b");  // OK: two char* arguments
    free(ptr);  // OK: void* argument
    return 0;
}
```

```bash
$ ./cccc --ffi-type-checking test_ffi_valid.c
<Program compiles and runs successfully>
```

## API Usage

The FFI safety features can also be controlled programmatically using the CCCC API:

```c
#include "cccc.h"

int main() {
    CCCC vm;
    cc_init(&vm, 0);  // Initialize without debugger

    // Configure FFI safety
    cc_ffi_allow(&vm, "malloc");
    cc_ffi_allow(&vm, "free");
    cc_ffi_deny(&vm, "system");
    cc_ffi_deny(&vm, "exec");

    vm.disable_all_ffi = 0;           // Allow FFI (with restrictions)
    vm.ffi_errors_fatal = 1;          // Make errors fatal
    vm.enable_ffi_type_checking = 1;  // Enable type checking

    // Compile and run code...
    Token *tok = cc_preprocess(&vm, "program.c");
    Obj *prog = cc_parse(&vm, tok);
    cc_compile(&vm, prog);
    int exit_code = cc_run(&vm, argc, argv);

    // Clean up allow/deny lists
    cc_ffi_clear_allow_list(&vm);
    cc_ffi_clear_deny_list(&vm);

    cc_destroy(&vm);
    return exit_code;
}
```
