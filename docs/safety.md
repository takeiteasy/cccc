# Memory Safety

Runtime detection of common C bugs, from zero-overhead to maximum paranoia.

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

**Enabled features:** None. The VM heap allocator is still active by default at every level (add `-V`/`--no-vm-heap` to opt back into the host allocator); see [VM Heap Allocator](safety-vm-heap.md#vm-heap-allocator) below.

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

**Detects:** Stack overruns that reach the canary or return address (reported when the function returns), heap buffer overflows, memory leaks, integer overflow/underflow, format string bugs

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
    [Advanced pointer tracking](safety-pointers.md)
    for what it catches)
  - Alignment checks
  - Provenance tracking (pointer origin validation)
  - Invalid pointer arithmetic detection
  - Stack variable instrumentation (per-activation liveness tracking,
    read/write counts — see below for what it does and doesn't catch)
  - Random canaries (unpredictable stack protection)

**Detects:** Most memory safety bugs CCCC can catch, including dereferencing a
stack pointer whose owning frame has already returned -- whether the
dereference happens in the frame that got control back or one or more calls
*deeper* than that, including
through an interior pointer with a runtime-computed offset (e.g. `&arr[i]`
for non-constant `i`) whose owning array/struct escapes its frame

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

`--test-run[=LEVEL]` (see [testing.md](testing-running.md#--test-runlevel-smoke-test-the-program-itself-before-compiling)) reuses these same preset names/levels (`LEVEL` accepts `none`/`basic`/`standard`/`max` or `0`/`1`/`2`/`3`, defaulting to `max`) to run the program once under the VM's safety instrumentation before handing it to `-c=native`. It's a way to get this level's crash/safety-violation coverage as a pre-compile gate — e.g. `-1`/`--safety=basic`'s low-overhead checks as a cheap CI smoke test before every native build — without needing a `[[cccc::test]]` suite.

---

## Configuring Safety from Source: `#pragma cccc config(...)`

`#pragma cccc config(key = value, ...)` is a file-scope pragma that sets
safety levels and individual safety-check flags directly from source,
without needing matching command-line flags. It is useful for self-contained
test files and examples that must run with a specific safety profile
regardless of how they're invoked.

```c
#pragma cccc config(safety = 2)
#pragma cccc config(bounds_checks = true, memory_tagging = false)
#pragma cccc config(overflow_checks) // bare key is shorthand for "= true"
```

### Keys

| Key | Values | Effect |
|-----|--------|--------|
| `safety` | `0`-`3` | Same as `-0`/`-1`/`-2`/`-3` (none/basic/standard/max) |
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

- **Command-line flags always win.** If a safety setting was explicitly
  passed on the command line (`-0`/`-1`/`-2`/`-3`, `--safety=...`,
  `--bounds-checks`, etc.), `config(...)` silently has no effect on that
  setting — the CLI value is kept.
- **Ignored in serialized-output modes.** With `-c=native`,
  `-m`/`--dump-expanded` or `-c=generated`, `config(...)` has no effect on
  `vm->flags` (the output is C for the system `cc`, which has its own
  sanitizer flags). The exception is `checked_pointers`, which `-c=native`
  and `-m` enforce in their output (see
  [Native and serialized output](safety-checked-pointers.md#checked-pointers)). This
  prints a `-Wignored-features` warning (on by default under `-Wall`) naming
  the pragma; it is otherwise silent by default, the same as every other
  `-Wignored-features` diagnostic (`_Atomic`, etc.). Unknown keys and invalid
  values are still hard errors in these modes.
- **Unknown keys and out-of-range values are hard compile errors**, e.g.
  `config(frobnicate = 1)` or `config(safety = 9)`.

---

## Guide pages

| Page | Covers |
|---|---|
| [safety-memory.md](safety-memory.md) | Stack/heap canaries, poisoning, leak, use-after-free and double-free detection |
| [safety-bounds.md](safety-bounds.md) | Runtime array bounds checks |
| [safety-type-checks.md](safety-type-checks.md) | Pointer type checks and uninitialized-read detection |
| [safety-overflow.md](safety-overflow.md) | Signed integer overflow detection |
| [safety-threads.md](safety-threads.md) | Thread safety and floating-point traps |
| [safety-dangling.md](safety-dangling.md) | Dangling stack pointer detection |
| [safety-pointers.md](safety-pointers.md) | Alignment, provenance, tagging, format strings and control-flow integrity |
| [safety-checked-pointers.md](safety-checked-pointers.md) | Checked-pointer syntax and bounds declarations |
| [safety-checked-propagation.md](safety-checked-propagation.md) | How bounds propagate across assignments |
| [safety-checked-interfaces.md](safety-checked-interfaces.md) | Bounds-safe interfaces, bounds casts and enforcement |
| [safety-checked-types.md](safety-checked-types.md) | `ntarray`, guarded pointee types and checked arrays |
| [safety-checked-regions.md](safety-checked-regions.md) | `_Checked`/`_Unchecked` regions |
| [safety-vm-heap.md](safety-vm-heap.md) | The VM heap allocator |
| [safety-ffi.md](safety-ffi.md) | Allow/deny lists and type checking for FFI calls |
