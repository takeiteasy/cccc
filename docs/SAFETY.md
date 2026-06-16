# CCCC Memory Safety Features

CCCC includes a suite of powerful memory safety features designed to detect common C programming errors at runtime. These features can be enabled individually or together to provide comprehensive protection against bugs like buffer overflows, use-after-free, and type confusion.

## Safety Levels (Quick Start)

CCCC provides preset safety levels that make it easy to choose the right combination of features without needing to understand every individual flag. Each level builds on the previous one, adding more comprehensive checks with increasing performance overhead.

### Level 0: None (`-0` or `--safety=none`)
**Purpose:** Maximum performance, zero safety overhead
**Overhead:** 0%
**Use when:** You've thoroughly tested your code and need maximum speed, or when running in a trusted/controlled environment

```bash
./cccc -0 program.c
./cccc --safety=none program.c
```

**Enabled features:** None

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
  - Dangling pointer detection (use-after-return)
  - Alignment checks
  - Provenance tracking (pointer origin validation)
  - Invalid pointer arithmetic detection
  - Stack variable instrumentation with runtime errors
  - Random canaries (unpredictable stack protection)

**Detects:** Everything possible - every memory safety bug CCCC can catch

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
  to the system `cc`, which has its own optimisation/sanitizer flags).
  Unknown keys and invalid values are still hard errors in native mode.
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
  - Requires VM heap mode (`-V` or any heap safety flag)
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
  - Requires VM heap mode (`-V` or any heap safety flag)
  - Performance overhead proportional to allocation size (memset on alloc/free)
- `--memory-leak-detection` **Memory leak detection**
  - Tracks all VM heap allocations in a host-memory linked list (AllocRecord)
  - Removes the record on free() and realloc()
  - Reports all unfreed allocations at program exit (in cc_destroy)
  - Shows address, size, and PC offset of allocation site for each leak
  - The report is printed to stdout after the program exits; exit code is unchanged
- `--uaf-detection` **Use-after-free detection**
  - Marks freed blocks instead of reusing them
  - Increments generation counter on each free
  - CHKP opcode checks if accessed pointer has been freed
  - Reports UAF with allocation details and generation number
- **Double-free detection** (always enabled)
  - Automatically detects attempts to free the same pointer twice
  - Works independently of UAF detection setting
  - Tracks freed state in allocation header
  - Prevents free list corruption and security vulnerabilities
  - Aborts execution with detailed error message including address, size, and generation
- `--bounds-checks` **Runtime array bounds checking**
  - Tracks requested vs allocated sizes for all heap allocations
  - CHKP opcode validates pointer is within allocated region
  - Checks against originally requested size (not rounded allocation)
  - Detects out-of-bounds array accesses with offset information
- `--type-checks` **Runtime type checking on pointer dereferences**
  - Tracks allocation type information in heap headers
  - CHKT opcode validates pointer type matches expected type on dereference
  - Detects type confusion bugs (e.g., casting `int*` to `float*`)
  - Only checks heap allocations (stack types not tracked at runtime)
  - Skips checks for `void*` and generic pointers (universal pointers)
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

Blocking calls — `read`, `write`, `pwrite`, `poll`, `accept`, `connect`, `wait`, `waitpid`, `sleep`, `usleep` — release the GIL while blocked so other VM threads can make progress. Non-blocking calls (`close`, `lseek`, `stat`, `open`, and most control-plane socket calls) intentionally hold the GIL.

**Thread-aware stack canaries**

Stack canary checks work correctly under threading. Each VM thread runs with its own `bp`/`sp`/`stack_seg` via `ExecState` (saved and restored on every GIL hand-off), so canary slots are isolated per thread. Stack canary protection enabled with `-1` or `--stack-canaries` is not disabled when `pthread_create` is used.

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

- `--dangling-pointers` **Dangling stack pointer detection**
  - Tracks all stack pointer creations via MARKA opcode
  - Invalidates pointers when function returns (LEV instruction)
  - CHKP validates pointer hasn't been invalidated before dereference
  - Detects use-after-return bugs (e.g., returning `&local_var`)
  - HashMap tracks: pointer value → {BP, stack offset, size}
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
  - CHKL opcode validates variable is alive before access
  - MARKR/MARKW opcodes track read/write counts for each variable
  - Detects use-after-scope and use-after-return bugs
  - Stack overflow detection: tracks high water mark, warns at 90% threshold
  - Integrated with `--dangling-pointers` for comprehensive temporal safety
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
  - Requires `-V` / `--vm-heap` to intercept malloc/free
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
- `--vm-heap` **Force VM heap allocation**
  - Intercepts malloc/free calls at compile time (codegen phase)
  - Routes malloc → MALC opcode, free → MFRE opcode
  - Enables memory safety features for user code using standard malloc/free
  - Without this flag, safety checks only apply to code directly using VM heap
  - Automatically enabled when any heap-related safety flag is used
  - Can be used standalone to enable double-free detection
  - Zero overhead when no safety features are enabled

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
$ ./cccc -f test_double_free.c

========== DOUBLE-FREE DETECTED ==========
Attempted to free already-freed memory
Address:  0x897640038
Size:     104 bytes
Allocated at PC offset: 11
Generation: 1
=========================================
```

**Note:** Double-free detection is always enabled when using VM heap (MALC/MFRE), regardless of which safety flags are active. It works with any memory safety feature (`-f`, `-k`, `-l`, `-b`, etc.) that routes allocations through the VM heap.

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
```c
// test_dangling_pointer.c - Dangling stack pointer example

int *get_local_address() {
    int x = 42;
    return &x;  // Return address of local variable (dangling pointer!)
}

int main() {
    int *ptr = get_local_address();
    int value = *ptr;  // Dereference dangling pointer!
    return value;
}
```

```bash
$ ./cccc --dangling-pointers test_dangling_pointer.c

========== DANGLING STACK POINTER ==========
Attempted to dereference invalidated stack pointer
Address:       0x92e5fffb0
Original BP:   invalidated (function has returned)
Stack offset:  -1
Size:          4 bytes
Current PC:    0x92e800080 (offset: 16)
==========================================
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
    // Without -T flag, this might reuse same address
    // With -T flag, memory is quarantined, gets new address
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
address will always diverge after a free, giving correct temporal violation reports. Always
combine with `-V` / `--vm-heap` when using programs that call `malloc`/`free` directly.

**Combining with VM Heap Mode:**
```bash
# For code using malloc/free, combine with --vm-heap
$ ./cccc --memory-tagging --vm-heap my_program.c

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
