# CCCC Bytecode Optimization

CCCC includes optional bytecode optimization passes that can improve execution performance. These are disabled by default and can be enabled with the `--optimize` flag.

## Quick Start

```bash
# No optimization (default)
./cccc program.c

# Enable optimization (level 1 = basic)
./cccc --optimize program.c
./cccc --optimize=1 program.c
./cccc -O1 program.c       # short form

# Standard optimization
./cccc -O2 program.c

# Aggressive optimization
./cccc -O3 program.c

# Aggressive optimization plus opcode fusion and extension elimination
./cccc -O4 program.c           # level 4 includes fuse + elim-ext
./cccc -ffuse program.c        # enable fuse pass only
./cccc -felim-ext program.c    # enable extension elimination only

# Mix -O level with per-pass overrides
./cccc -O3 -fno-cse program.c    # O3 without CSE
./cccc -O0 -fpeephole program.c  # only peephole
./cccc -ffold -fdce program.c    # exactly these two passes
```

## Optimization Levels

| Level | Flag | Description | Passes |
|-------|------|-------------|--------|
| 0 | (default) | No optimization | None |
| 1 | `-O1` / `--optimize=1` | Basic | Constant folding + dead-call elimination |
| 2 | `-O2` / `--optimize=2` | Standard | All level-1 passes + peephole + CSE for const functions + scalar local promotion + indexed load/store lowering |
| 3 | `-O3` / `--optimize=3` | Aggressive | All passes + copy propagation + dead-MOV3 elimination |
| 4 | `-O4` / `--optimize=4` | Fused | All level-3 passes + automatic opcode fusion + redundant extension elimination |

## Per-Pass Flags

Individual optimisation passes can be enabled or disabled independently of the
`-O` level, using gcc-style `-f<pass>` / `-fno-<pass>` flags.  They compose:
`-O<n>` sets the default pass set, then `-f` overrides are applied on top.

| Pass | Enable flag | Disable flag | Default level |
|------|-------------|--------------|---------------|
| Constant folding | `-ffold` | `-fno-fold` | `-O1`+ |
| Peephole reductions | `-fpeephole` | `-fno-peephole` | `-O2`+ |
| CSE (const functions) | `-fcse` | `-fno-cse` | `-O2`+ |
| Copy propagation | `-fcopy-prop` | `-fno-copy-prop` | `-O3`+ |
| Dead code elimination | `-fdce` | `-fno-dce` | `-O3`+ |
| Opcode fusion | `-ffuse` | `-fno-fuse` | `-O4`+ |
| Redundant extension elimination | `-felim-ext` | `-fno-elim-ext` | `-O4`+ |

Long-form equivalents (`--ffold`, `--fno-fold`, etc.) are also accepted.

Examples:

```bash
./cccc -O3 -fno-cse prog.c       # O3 minus CSE
./cccc -O0 -fpeephole prog.c     # only peephole
./cccc -ffold -fdce prog.c       # exactly fold + dce
./cccc -O3 -fno-copy-prop prog.c # O3 without copy propagation
```

### Per-Pass Overrides via `#pragma cccc config`

Individual optimisation passes can also be enabled or disabled inline using
`#pragma cccc config`. The key names use underscores (matching the `snake_case`
convention of existing config keys):

```c
#pragma cccc config(fold = true)           // enable constant folding
#pragma cccc config(cse = false)           // disable CSE
#pragma cccc config(fold, peephole)        // bare key = true for multiple passes
#pragma cccc config(dce = false, fuse = false)
```

Accepted pass keys: `fold`, `peephole`, `copy_prop`, `dce`, `cse`, `fuse`,
`elim_ext`. Boolean values `true`/`false` (or `1`/`0`) are accepted; a bare
key without `= value` defaults to `true`.

CLI `-f`/`-fno-` flags take precedence over `#pragma cccc config` for the same
pass — the pragma is silently ignored for any pass already pinned by the CLI.

### Per-Pass Overrides in `[[cccc::test]]` Suites

Inside `[[cccc::test]]` suite files, `-f<pass>` and `-fno-<pass>` flags are
accepted in the `flags=` attribute and trigger a lazy recompile with that
pass configuration:

```c
[[cccc::test(return = 42, flags = "-ffold -fno-cse")]]
int test_fold_only(void) { return 6 * 7; }
```

See [TESTING.md](TESTING.md) for the full per-test flags reference.

## Per-Function Optimization

A single function can request its own optimization level using the `optimize`
attribute, regardless of the global `-O` flag.  **CCCC uses GCC-style
precedence: the attribute always wins over the global level for that function.**

```c
// This function is optimized at O3 even in a -O0 build.
[[cccc::optimize(3)]]
int hot_path(int a, int b) { return a * b + a; }

// This function follows the global -O level.
int cold_path(int a, int b) { return a + b; }
```

Three spellings are accepted:

| Spelling | Example |
|----------|---------|
| `[[cccc::optimize(N)]]` (C23 integer, recommended) | `[[cccc::optimize(2)]]` |
| `@optimize(N)` (@ shorthand) | `@optimize(2)` |
| `__attribute__((optimize("ON")))` (GCC-compatible string) | `__attribute__((optimize("O2")))` |

The string form accepts `"O0"`–`"O4"` with an optional leading `-`
(e.g. `"-O3"`), matching GCC conventions.

**How it interacts with global optimization:**

- Functions *with* an `optimize` attribute use their attribute level regardless
  of the global `-O`, `--optimize=N`, or `#pragma cccc config(optimisation=N)`.
- Functions *without* an attribute use the global level as normal.
- A global `-O0` build still optimizes attributed functions at their declared
  level.  The primary use case is selectively enabling optimization on hot
  functions in a debug (`-O0`) build.

**With `tools/tests.py --matrix`:** because attributed functions always use their
own level, their behaviour is identical at every pass combination swept by
`--matrix`.  Attribute-based tests are therefore naturally safe to include in the
matrix sweep without special casing.

See [COVERAGE.md — optimize attribute](COVERAGE.md#__attribute__optimize--ccccoptimizen--optimizen-cccc-specific)
for the complete attribute reference.

## Optimization Passes

Optimizer bytecode walks use the shared instruction-size metadata generated
from `OPS_X` in `src/cccc.h`. Each opcode declares its operand-word count beside
the opcode name, so optimizer passes, bytecode serialization, and debugger
disassembly stay aligned when instructions are added.

Code generation also performs structural lowering before the optional optimizer
runs. Dense `switch` statements use the VM's `JMPT` jump-table instruction, while
sparse switches use a balanced compare/jump tree. Label and function-call patch
resolution uses hash maps during codegen so large functions and multi-file
programs avoid quadratic patch scans.

At `--optimize=2` and above, code generation also promotes hot eligible
integer/pointer locals to VM saved registers. Promoted locals exclude volatile,
address-escaping, aggregate, array, captured, block, VLA, floating, complex, and
debugger-visible cases; dirty values are flushed back to their stack slots at
function exits.

The same levels also lower simple array and pointer dereferences of the form
`base + index * scale` to fused indexed VM opcodes. This removes the explicit
`MUL3 + ADD3 + LDR/STR` address sequence for scalar integer and floating-point
loads/stores when pointer-safety instrumentation is not active.

At `-O4` / `--optimize=4`, or whenever `-ffuse` is specified, a post-codegen
fusion pass scans the emitted bytecode for adjacent single-def/single-use
opcode pairs with a registered fused form. The pass keeps the existing codegen
lowerings above; it only rewrites remaining eligible arithmetic chains such as
`LI3 + MUL3 + ADD3` into `MULADDI3`.

At `--optimize=2` and above, `restrict`-qualified scalar pointer parameters are
cached in callee-saved registers (S4–S7). Loads of `*p` or `p[const]` for a
restrict param `p` hit the register directly on subsequent accesses within a
straight-line block; stores write through, and control-flow joins invalidate.
A store's write-through only fires when its own pointee type matches the
cache entry's tracked type (`param`'s declared pointee type); a type-punned
store at the same offset (e.g. `*(char *)p = c` against an `int *restrict p`
entry, narrower or wider) invalidates that param's whole cache instead of
splicing a partial value in, since a byte-granular value can't be expressed
as a single register copy (#757). Function calls invalidate the whole cache too — both before and after the
call itself, since evaluating the call's own arguments can fill an entry
(e.g. `f(*p)`) that must not survive the call. A pre-pass AST walk also
identifies locals provably derived from restrict params
(`int *q = p + k`) and extends the same cache to `*q` and `q[const]` accesses —
a store through `*q` updates the `(p, byte_offset)` slot rather than triggering a
global invalidate. Variable-offset derivations (`q = p + n`) still benefit from
targeted invalidation of only `p`'s slots. `for` loops of the form
`for (T i=0; i<n; i++) dst[i]=src[i]` where both pointers are restrict-qualified
are lowered to a single `MCPY` opcode (libc memcpy) at `--optimize=2`+.

Indexed load/store fusion and the `restrict` memcpy-loop lowering above are
disabled whenever any of `--bounds-checks`, `--uaf-detection`,
`--pointer-sanitizer`, `--type-checks`, or the invalid-arithmetic/
provenance-tracking flags are enabled — each of these fusions elides a
load/store and therefore bypasses the CHKP3/CHKT3 emission that the normal
load/store path relies on (`CCCC_FUSION_UNSAFE_FLAGS` in `src/codegen.c`).
This applies to each flag individually, not just their `-2`/`-3`/
`--pointer-sanitizer` combinations, so a standalone `--type-checks -O3` build
gets the same coverage as `-O0`.

The `restrict` value cache is the exception: instead of disabling it under
these flags, its cache-hit path re-derives the address and runs CHKP3/CHKT3
itself. A cache hit only reaches into an S-register, so this re-derivation
(and the checks) are pure overhead relative to a real load — in practice a
wash rather than a win, since the checks dominate the eliminated `LDR`. It
stays enabled under safety flags for the safety property, not throughput:
the hit-site checks catch cache staleness that the invalidation logic (see
below) doesn't anticipate, independent of whether the invalidation
bookkeeping itself is complete.

This `restrict` cache is the only aliasing-aware optimization in CCCC. It works
because `restrict` is a *scope-wide* non-aliasing promise about a parameter,
which is enough to justify caching loads across the whole function body. There
is no general alias analysis, memory-dependency tracking, dead-store
elimination, or load/store reordering pass — none of the passes below reason
about whether two arbitrary pointers can alias. This is also why
`__attribute__((malloc))` (a *point-wise* freshness fact about a call's return
value, see [COVERAGE.md](COVERAGE.md)) isn't exploited: feeding it into the
`restrict` cache, or any other reordering/elimination decision, would need the
same dataflow analysis the optimizer doesn't have. Adding a memory-dependency
pass is tracked as low-priority future work.

### Phase 1: Constant Folding (`--optimize=1`)

Tracks constant values through register operations and records foldable integer
operations for replacement with `LI3`. A final compaction pass rebuilds bytecode
when replacements change instruction width.

**What it optimizes:**
- `LI3` (load immediate) values are tracked
- `MOV3` (register copy) propagates constant status
- Sign and zero extension operations on constants become `LI3`
- `ADDI3` (add immediate) becomes a same-width `LI3` when the source is constant
- Arithmetic operations (`ADD3`, `SUB3`, `MUL3`, `DIV3`, etc.) are evaluated when both operands are constants and rewritten to `LI3`
- Unary operations (`NEG3`, `NOT3`, `BNOT3`) on constants are rewritten to `LI3`
- Dead-call elimination: direct, indirect (CALLN), and FFI (CALLF) calls to `[[gnu::pure]]` or `[[gnu::const]]` functions whose result is discarded are omitted; argument expressions are still evaluated so their side effects run

**Example:**
```c
int x = 42 + 0;  // Rewritten to load the folded constant directly
```

**Limitations:**
- Constants are invalidated at control flow boundaries (jumps, calls, returns)
- Constants are invalidated at known branch targets to avoid folding across control-flow joins
- Memory loads reset constant tracking for the destination register
- Division by zero, signed division overflow, invalid shifts, and overflow-checked signed arithmetic are not folded

---

### CSE for `[[gnu::const]]` Functions (`--optimize=2`)

Common-subexpression elimination for functions marked `[[gnu::const]]`
(or `__attribute__((const))`).  After the compaction pass, the optimizer
scans for duplicate calls to the same const function within a straight-line
basic block and replaces the second call with a register move reusing the
first result.

**Value numbering tracks:**
- Compile-time constants loaded by `LI3`
- Local-variable loads from specific stack slots via `LDR_LOCAL_*`
- Register copies via `MOV3`

CSE fires when all argument registers carry known value numbers that match
a previously seen call to the same function.  It does **not** fire:
- Across control-flow boundaries (branch targets reset the cache)
- For `[[gnu::pure]]` functions (which may read globals that change between calls)
- When any argument register holds a float/double value (float args use separate FREG registers not tracked by the integer VN state)

**Example:**
```c
[[gnu::const]] int square(int x) { return x * x; }

int a = square(5);  // first call — executes normally
int b = square(5);  // same constant arg — replaced by MOV at -O2+
```

Because `CALL` and `MOV3` both encode in 2 bytecode words, the replacement
is done in-place — no second compaction pass is required.

---

### Phase 2: Peephole Optimization (`--optimize=2`)

Pattern-matches small instruction sequences, removes redundancies, and enables
code-generation lowerings that reduce hot-loop bytecode dispatch.

**Patterns optimized:**
| Pattern | Replacement | Description |
|---------|-------------|-------------|
| `MOV3 ra, ra` | NOP | Self-move (no effect) |
| `LI3 rx, A; LI3 rx, B` | `LI3 rx, B` | Dead store (first overwritten) |
| `PSH3 rx; POP3 rx` | NOP | Push/pop same register |
| `JMP next_instr` | NOP | Jump to fall-through when the `JMP` is not itself a control-flow target |
| `base + index * scale; LDR/STR` | `LDR_INDEX_*` / `STR_INDEX_*` | Fused indexed load/store for simple scalar array and pointer accesses |

---

### Copy Propagation (`--optimize=3`)

Forward dataflow analysis that tracks which registers are copies of other registers and substitutes the original source register at all downstream uses. Where the substitution makes a chain redundant (e.g., `MOV3 r5, r10; MOV3 r11, r5` → `MOV3 r11, r10`), the intermediate copy is collapsed and the earlier MOV3 is eliminated if its destination has no remaining uses.

**What it eliminates:**
- Redundant register-to-register moves (`MOV3`) where the destination is overwritten before being read
- Copy chains: `a = b; c = a` becomes `c = b` and the `a = b` move is removed when `a` has no other uses
- Dead moves to argument/scratch registers before calls and control-flow

**How it works:**
- Sub-pass A (forward substitution): maintains a `copy_of[r]` table per basic block; when an instruction reads a register with a known copy fact, the read is redirected to the ultimate source
- Sub-pass B (dead-MOV3 NOP): tracks the most recent MOV3 that defined each register; if the register is re-defined before its value is read, the earlier MOV3 is NOP'd for removal by compaction

**Limitations:**
- Copy facts are cleared at all control-flow join points (loop headers, branch targets) — conservative; some across-block chains are missed
- Conditional branches (JZ3/JNZ3) flush the tracking table: the fall-through path continues correctly, but the pass cannot verify the taken-path is also dead-safe
- Only tracks integer registers (FMOV3 and float-register copies are not propagated)
- Opcodes whose operand word carries an address or packed immediate rather than a register encoding (`op_operand_word_is_immediate()`) are treated as fully opaque by **both** sub-passes: sub-pass B skips its generic decode for them entirely, and sub-pass A's `default:` arm bails out before substituting or invalidating anything for them
- SIMD/vector opcodes (see `docs/VM.md#simd--vector-operations`) are treated as fully opaque (`op_has_vector_operand()`): their `rd`/`rs1`/`rs2` bytes mix a third register namespace (`vregs[]`) with real `freg`/`greg` operands per-opcode in ways this pass's two-map (int/float) model cannot express safely — copy facts touching any of their operand bytes are conservatively invalidated rather than substituted
- **Invariant (byte-0 sources):** every opcode whose operand byte 0 is a *source* register (read, not written — e.g. a store's value register, a check's pointer argument) must be listed in `op_byte0_is_int_src()`. If it isn't, sub-pass B treats that byte-0 read as a definition and can NOP the still-live `MOV3` that fed it — a miscompile, not just a missed optimization. `CHKB` and `MARKP` were missing this listing until #755 (`CHKB`'s base-pointer source at byte 0 was being killed as a dead def, corrupting the pointer register on the third repeated `p[const]` access in a function under `--bounds-checks --optimize=3`); the safety-check family (`CHKP3`/`CHKA3`/`CHKT3`/`CHKPA`/`CHKB`/`MARKP`) is the group most likely to grow new members needing this treatment.
- **Invariant (immediate operand words):** every opcode whose operand word at `pc+1` holds an address or immediate rather than a register encoding must be listed in `op_operand_word_is_immediate()`, and **both** sub-passes consult it. Sub-pass B would otherwise misread byte 0 as a destination and NOP a live `MOV3`; sub-pass A would rewrite bytes 1-2 in place and corrupt the immediate itself. The stack-instrumentation / uninitialized-detection family (`SCOPEIN`, `SCOPEOUT`, `CHKI`, `MARKI`, `CHKL`, `MARKR`, `MARKW`, `STKTAG`) and `IOVFL` were missing until #759.
- **Invariant (implicit ABI-register effects):** every opcode that reads or writes a register *without* encoding it in its operand word (fixed-slot ABI registers, e.g. `MCPY`'s A0/A1/A2, or a packed-immediate op's hidden A0 result) must be listed in the shared `op_implicit_abi_regs()` table (`src/optimize.c`) — the single source of truth all three of these passes (copy-prop sub-pass A, sub-pass B, and `opt_elim_ext` below) consult for this class of opcode, replacing the three hand-maintained per-pass lists that used to drift out of sync. Sub-pass A clears all copy facts for any opcode the table lists (an opcode either has implicit effects or it doesn't — the pass doesn't track individual reads/writes at that granularity); sub-pass B `MARK_INT_USE`s every implicitly-read register before `KILL_INT_DEF`ing every implicitly-written one, so a still-live `MOV3` into a register the opcode both reads and writes is preserved. Missing an opcode from this table is a live miscompile, not just a missed optimization: `IOVFL`, `MSET`, and `VRAISE` fell through to `default: break` in sub-pass B until #760, meaning their implicit register reads were invisible and a live `MOV3` feeding `A0`/`A1`/`A2` immediately before one of them could be NOP'd — reproduced live for `IOVFL`: two back-to-back `__builtin_add_overflow` checks in the same straight-line block (no branch between them to reset tracking) each feed `IOVFL`'s `b` argument via `MOV3 A1, <t>`; the second write made sub-pass B judge the first dead, corrupting the first check's result under `--optimize=3`. `AXCHG`/`ACAS`/`IOVFL` additionally implicitly define `REG_A0` in a way invisible to sub-pass A's generic operand-word scan (since their operand word carries a packed immediate, not a register triple) — #759 reproduced this live: a stale `copy_of[A0]` fact from a `MOV3` feeding `IOVFL`'s `a` argument survived across the `IOVFL` that redefines `A0` with the overflow bool, and a later `JZ3` testing that bool got its condition register silently substituted back to the stale source, flipping which branch ran. `VRAISE` is modeled as fully *opaque* (reset, not "reads A0 / writes A0") rather than by its literal register effects: `op_VRAISE_fn`'s VM-handler case pushes a return address and jumps to a signal handler — a control-flow edge that `build_control_flow_targets()` cannot see statically, the same reason `VSIGNAL` is opaque.

**Example:**
```c
// Generated for: struct Point p = make_point(10, 32);
// After call, r10 = return-buffer address, r5 = &p (local copy)

// Before copy-prop (-O2):
MOV3 r5, r10   // r5 = return buffer
MOV3 r11, r5   // r11 = r5 (MCPY source)

// After copy-prop (-O3):
MOV3 r5, r10   // kept (r5 used elsewhere)
MOV3 r11, r10  // r11 directly references root — r5 copy eliminated
```

---

### Phase 3: Dead Code Elimination (`--optimize=3`)

Removes demonstrably dead code using conservative analysis.

**What it removes:**
- Consecutive `MOV3` to the same destination register (first is dead)
- NOP sequences from previous passes are removed by bytecode compaction

> **Note:** DCE uses a **conservative approach** to ensure correctness. Full unreachable code elimination after unconditional jumps is not implemented as it requires comprehensive jump target analysis that could affect code with computed gotos, switch tables, or inline assembly. This design choice prioritizes safety over aggressive optimization.

---

### Phase 4a: Redundant Extension Elimination (`--optimize=4`, `-felim-ext`)

Forward-dataflow pass that eliminates sign-extension (`SX1`/`SX2`/`SX4`) and
zero-extension (`ZX1`/`ZX2`/`ZX4`) opcodes whose result is provably identical
to the source value.

`SX*/ZX*` opcodes are among the most-executed in integer-heavy CCCC programs —
typically 15–20% of all dispatched instructions.  Much of this is spurious:
codegen conservatively emits an extension after every narrow load and after
every integer arithmetic operation whose type promotion rules require one, even
when the value is already in the target range.

**Redundancy conditions** — `OP rd, rs` is eliminated when `rs` is already in
the target range:

| Op | Eliminated when ext-state of `rs` is |
|----|---------------------------------------|
| `SX1` | `SX1` |
| `SX2` | `SX1`, `SX2`, `ZX1` (`ZX1`=[0,255] ⊂ SX2 range) |
| `SX4` | `SX1`, `SX2`, `SX4`, `ZX1`, `ZX2` |
| `ZX1` | `ZX1` |
| `ZX2` | `ZX1`, `ZX2` |
| `ZX4` | `ZX1`, `ZX2`, `ZX4` |

**State producers** — instructions that seed range state for downstream extensions:

| Opcode | Range guarantee |
|--------|----------------|
| `LDR_B`, `LDR_LOCAL_B`, `LDR_INDEX_B` | SX1 (signed byte load) |
| `LDR_H`, `LDR_LOCAL_H`, `LDR_INDEX_H` | SX2 (signed halfword load) |
| `LDR_W`, `LDR_LOCAL_W`, `LDR_INDEX_W` | SX4 (signed word load) |
| `SX1`/`SX2`/`SX4` | SX1/SX2/SX4 |
| `ZX1`/`ZX2`/`ZX4` | ZX1/ZX2/ZX4 |
| any other write with a real register in operand byte 0 | cleared (unknown) |

State is cleared conservatively at every control-flow join point, and at
every opcode with an implicit register effect per the shared
`op_implicit_abi_regs()` table (see the Copy Propagation invariant above) —
this includes unconditional control-flow (`CALL`/`CALLT`/`CALLI`/`CALLN`/
`CALLF`/`JMP`/`JMPI`/`ENT3`/`LEV3`, all treated as opaque: a callee's `A0`
return value must not inherit the caller's stale extension state) and
zero-operand implicit-write opcodes (`MALC`, `SETJMP`, `VRAISE`, …). This
query runs *before* the pass's `size < 2` bail-out, which used to make every
zero-operand opcode invisible to it entirely — until #761, a later `SX4`/
`ZX4` on `A0` could be judged redundant against range state that predated
one of these opcodes and deleted, producing a value that was never actually
extended. `AXCHG`/`ACAS`/`IOVFL` need the same treatment for a different
reason: their operand word carries a packed immediate, not a register
encoding, so the "any other write" row above must not read byte 0 of that
word as a destination register to clear (harmless on its own — it only
forgoes an optimization — but wrong on its face); their real implicit `A0`
write is handled by the `op_implicit_abi_regs()` query instead.

**When redundant:**
- `rd == rs`: replaced with a NOP (same as peephole NOP; compacted away)
- `rd != rs`: replaced with `MOV3 rd, rs` (copy-prop may later simplify)

This extends the peephole pass's narrower adjacency-only rule
(`LDR_W + adjacent SX4`) to cover non-adjacent pairs, chained extensions, and
cross-width subsumptions.

**Example:**

```c
// Unsigned short loaded from a struct field.  Codegen emits:
//   LDR_H r1, [r0]    → state[r1] = SX2 (sign-extending halfword load)
//   ZX2   r1, r1      → NOT eliminated: ZX2 != SX2 (unsigned needs clearing)
//   ...               (intervening instructions)
//   ZX2   r2, r1      → ELIMINATED: state[r1] already ZX2 after first ZX2

// Signed int local reused after a cast.  Codegen emits:
//   LDR_LOCAL_W r3, [bp+8]  → state[r3] = SX4
//   SX4          r3, r3     → ELIMINATED: state[r3] is already SX4
```

Observed reduction on integer-heavy workloads: ~50% fewer SX4/ZX4/ZX1
dispatches, ~10–15% reduction in total opcode count.

---

### Phase 4b: Automatic Opcode Fusion (`--optimize=4`, `--fuse-ops`)

Runs use-def fusion analysis in-process on the generated text segment and
rewrites registered adjacent single-use chains to fused opcodes. Current
rewrites include:

| Pattern | Replacement |
|---------|-------------|
| `LI3 imm; ADD3` | `ADDI3` |
| `LI3 imm; MUL3` | `MULI3` |
| `MUL3; ADD3` | `MULADD3` |
| `LI3 imm; MUL3; ADD3` | `MULADDI3` |
| `FMUL3; FADD3` | `FMADD3` (f64 two-rounding) |
| `FMUL3_F32; FADD3_F32` | `FMADD3_F32` (f32 two-rounding) |
| `FMUL3; FSUB3` (minuend form: `a*b - c`) | `FMSUB3` (f64 two-rounding) |
| `FMUL3_F32; FSUB3_F32` (minuend form) | `FMSUB3_F32` (f32 two-rounding) |
| `FMUL3; FSUB3` (subtrahend form: `c - a*b`) | `FNMSUB3` (f64 two-rounding) |
| `FMUL3_F32; FSUB3_F32` (subtrahend form) | `FNMSUB3_F32` (f32 two-rounding) |

The pass skips branch targets and uses the normal bytecode compactor afterward,
so branches, jump tables, source maps, function ranges, and serialized text
relocations remain valid.

### Floating-Point Fusion Semantics

The `FMADD3`, `FMADD3_F32`, `FMSUB3`, `FMSUB3_F32`, `FNMSUB3`, and `FNMSUB3_F32`
opcodes produced by the default fusion path use **two separate roundings** — the
product is computed and rounded to `double` (or `float`) first, then added or
subtracted. This is bit-identical to the unfused sequence and matches GCC with
`-ffp-contract=off`.

To enable **true single-rounding FMA** (using the hardware `fma()`/`fmaf()`
intrinsic, a single rounding), pass `--fma`:

```bash
# Emit FMADD3_FMA / FMADD3_F32_FMA / FMSUB3_FMA / FMSUB3_F32_FMA
# / FNMSUB3_FMA / FNMSUB3_F32_FMA — single rounding
./cccc --fma program.c
```

`--fma` implies `--fuse-ops`. The single-rounding path gives a small additional
speedup on multiply-accumulate loops but **may produce different floating-point
results** from the default path or from GCC `-ffp-contract=off`. Only use it if
your program can tolerate slightly different FP values (e.g. when comparing
against GCC with `-ffp-contract=fast`).

---

## How It Works

The optimizer operates on the generated 32-bit bytecode words in `text_seg[]`
after codegen and before execution. The compiler pipeline forks at the bytecode
stage:

```
                                                     ┌─→ [Optimizer] → VM Execution
Source Code → Parser → AST → Codegen → Bytecode ───┤
                                                     └─→ -c=native → cc / clang / gcc
```

The optimizer is a **VM-only pass** — it rewrites bytecode before the
interpreter runs it. `-c=native` serialises the (unoptimised) bytecode to C
and hands it to a system compiler, so `--optimize` is rejected in `-c=native`
mode and the system compiler does the optimisation instead. See the
[README](../README.md#compile-natively-production) for the production path.

Optimizations first transform or mark bytecode, then rebuild the text segment
when instruction widths change or NOPs can be removed. The compaction pass
retargets direct branches and calls, dense `JMPT` jump tables, `LTA3` text
addresses, the entry point, debugger/source metadata, function ranges, and
serialized text relocations so PC-index targets remain valid after compaction.

## Best Practices

1. **Development (VM)**: Use the default (no `--optimize` flag) for predictable debugging
2. **Testing**: Run the test suite with `--optimize=3` to catch optimization bugs
3. **VM-only workflows** (debugger, safety suite, `--vm-profile`): Use `--optimize=2` for a good balance of speed and safety; use `--optimize=4` when measuring maximum VM throughput
4. **Production builds**: Use `-c=native` — the system compiler handles optimisation, and the CCCC frontend cost is the only CCCC-specific overhead in the loop

## Combining with Safety Features

Both `--optimize` and the `-0` … `-3` safety levels (and the individual flags
they expand to) are **VM-only** — they are rejected under `-c=native`, which
hands optimisation and instrumentation off to the system compiler. The examples
below are for the VM path:

```bash
# Maximum safety, no optimization
./cccc -3 program.c

# No safety, maximum optimization
./cccc -0 --optimize=3 program.c

# No safety, maximum VM optimization
./cccc -0 --optimize=4 program.c

# Standard safety with standard optimization
./cccc -2 --optimize=2 program.c
```

## Verbose Output

Enable verbose mode to see optimization statistics:

```bash
./cccc -v --optimize=3 program.c
```

With verbose enabled, the optimizer reports:
- `[opt] constant folding: tracked N constant expressions`
- `[opt] peephole: removed N redundant instructions`
- `[opt] copy-prop: rewrote N uses, eliminated M MOV3`
- `[opt] dead code: N instructions removed, M NOPs present`
- `[opt] elim-ext: removed N redundant extensions`
- `[opt] fused ops: rewrote N adjacent def-use pairs`
