# JCC Bytecode Optimization

JCC includes optional bytecode optimization passes that can improve execution performance. These are disabled by default and can be enabled with the `--optimize` flag.

## Quick Start

```bash
# No optimization (default)
./jcc program.c

# Enable optimization (level 1 = basic)
./jcc --optimize program.c
./jcc --optimize=1 program.c

# Standard optimization
./jcc --optimize=2 program.c

# Aggressive optimization
./jcc --optimize=3 program.c
```

## Optimization Levels

| Level | Flag | Description | Passes |
|-------|------|-------------|--------|
| 0 | (default) | No optimization | None |
| 1 | `--optimize` or `--optimize=1` | Basic | Constant folding |
| 2 | `--optimize=2` | Standard | Constant folding + Peephole |
| 3 | `--optimize=3` | Aggressive | All passes |

## Optimization Passes

Optimizer bytecode walks use the shared instruction-size metadata generated
from `OPS_X` in `src/jcc.h`. Each opcode declares its operand-word count beside
the opcode name, so optimizer passes, bytecode serialization, and debugger
disassembly stay aligned when instructions are added.

Code generation also performs structural lowering before the optional optimizer
runs. Dense `switch` statements use the VM's `JMPT` jump-table instruction, while
sparse switches use a balanced compare/jump tree. Label and function-call patch
resolution uses hash maps during codegen so large functions and multi-file
programs avoid quadratic patch scans.

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

### Phase 2: Peephole Optimization (`--optimize=2`)

Pattern-matches small instruction sequences and removes redundancies.

**Patterns optimized:**
| Pattern | Replacement | Description |
|---------|-------------|-------------|
| `MOV3 ra, ra` | NOP | Self-move (no effect) |
| `LI3 rx, A; LI3 rx, B` | `LI3 rx, B` | Dead store (first overwritten) |
| `PSH3 rx; POP3 rx` | NOP | Push/pop same register |
| `JMP next_instr` | NOP | Jump to fall-through when the `JMP` is not itself a control-flow target |

---

### Phase 3: Dead Code Elimination (`--optimize=3`)

Removes demonstrably dead code using conservative analysis.

**What it removes:**
- Consecutive `MOV3` to the same destination register (first is dead)
- NOP sequences from previous passes are removed by bytecode compaction

> **Note:** DCE uses a **conservative approach** to ensure correctness. Full unreachable code elimination after unconditional jumps is not implemented as it requires comprehensive jump target analysis that could affect code with computed gotos, switch tables, or inline assembly. This design choice prioritizes safety over aggressive optimization.

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
3. **VM-only workflows** (debugger, safety suite, `--vm-profile`): Use `--optimize=2` for a good balance of speed and safety
4. **Production builds**: Use `-c=native` — the system compiler handles optimisation, and the JCC frontend cost is the only JCC-specific overhead in the loop

## Combining with Safety Features

Both `--optimize` and the `-0` … `-3` safety levels (and the individual flags
they expand to) are **VM-only** — they are rejected under `-c=native`, which
hands optimisation and instrumentation off to the system compiler. The examples
below are for the VM path:

```bash
# Maximum safety, no optimization
./jcc -3 program.c

# No safety, maximum optimization
./jcc -0 --optimize=3 program.c

# Standard safety with standard optimization
./jcc -2 --optimize=2 program.c
```

## Verbose Output

Enable verbose mode to see optimization statistics:

```bash
./jcc -v --optimize=3 program.c
```

With verbose enabled, the optimizer reports:
- `[opt] constant folding: tracked N constant expressions`
- `[opt] peephole: removed N redundant instructions`
- `[opt] dead code: N instructions removed, M NOPs present`
