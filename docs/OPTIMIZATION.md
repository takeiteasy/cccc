# CCCC Bytecode Optimization

CCCC includes optional bytecode optimization passes that can improve execution performance. These are disabled by default and can be enabled with the `--optimize` flag.

## Quick Start

```bash
# No optimization (default)
./cccc program.c

# Enable optimization (level 1 = basic)
./cccc --optimize program.c
./cccc --optimize=1 program.c

# Standard optimization
./cccc --optimize=2 program.c

# Aggressive optimization
./cccc --optimize=3 program.c

# Aggressive optimization plus automatic opcode fusion
./cccc --optimize=4 program.c
./cccc --fuse-ops program.c
```

## Optimization Levels

| Level | Flag | Description | Passes |
|-------|------|-------------|--------|
| 0 | (default) | No optimization | None |
| 1 | `--optimize` or `--optimize=1` | Basic | Constant folding + dead-call elimination |
| 2 | `--optimize=2` | Standard | All level-1 passes + peephole + CSE for const functions + scalar local promotion + indexed load/store lowering |
| 3 | `--optimize=3` | Aggressive | All passes |
| 4 | `--optimize=4` | Fused | All level-3 passes + automatic opcode fusion |

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

At `--optimize=4`, or whenever `--fuse-ops` is specified, a post-codegen
fusion pass scans the emitted bytecode for adjacent single-def/single-use
opcode pairs with a registered fused form. The pass keeps the existing codegen
lowerings above; it only rewrites remaining eligible arithmetic chains such as
`LI3 + MUL3 + ADD3` into `MULADDI3`.

At `--optimize=2` and above, `restrict`-qualified scalar pointer parameters are
cached in callee-saved registers (S4–S7). Loads of `*p` or `p[const]` for a
restrict param `p` hit the register directly on subsequent accesses within a
straight-line block; stores write through, and control-flow joins invalidate.
A pre-pass AST walk also identifies locals provably derived from restrict params
(`int *q = p + k`) and extends the same cache to `*q` and `q[const]` accesses —
a store through `*q` updates the `(p, byte_offset)` slot rather than triggering a
global invalidate. Variable-offset derivations (`q = p + n`) still benefit from
targeted invalidation of only `p`'s slots. `for` loops of the form
`for (T i=0; i<n; i++) dst[i]=src[i]` where both pointers are restrict-qualified
are lowered to a single `MCPY` opcode (libc memcpy) at `--optimize=2`+.

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

### Phase 3: Dead Code Elimination (`--optimize=3`)

Removes demonstrably dead code using conservative analysis.

**What it removes:**
- Consecutive `MOV3` to the same destination register (first is dead)
- NOP sequences from previous passes are removed by bytecode compaction

> **Note:** DCE uses a **conservative approach** to ensure correctness. Full unreachable code elimination after unconditional jumps is not implemented as it requires comprehensive jump target analysis that could affect code with computed gotos, switch tables, or inline assembly. This design choice prioritizes safety over aggressive optimization.

---

### Phase 4: Automatic Opcode Fusion (`--optimize=4`, `--fuse-ops`)

Runs use-def fusion analysis in-process on the generated text segment and
rewrites registered adjacent single-use chains to fused opcodes. Current
rewrites include:

| Pattern | Replacement |
|---------|-------------|
| `LI3 imm; ADD3` | `ADDI3` |
| `LI3 imm; MUL3` | `MULI3` |
| `MUL3; ADD3` | `MULADD3` |
| `LI3 imm; MUL3; ADD3` | `MULADDI3` |

The pass skips branch targets and uses the normal bytecode compactor afterward,
so branches, jump tables, source maps, function ranges, and serialized text
relocations remain valid.

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
- `[opt] dead code: N instructions removed, M NOPs present`
- `[opt] fused ops: rewrote N adjacent def-use pairs`
