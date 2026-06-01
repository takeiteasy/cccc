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

### Phase 1: Constant Folding (`--optimize=1`)

Tracks constant values through register operations and rewrites bytecode when
the replacement fits in the original instruction footprint.

**What it optimizes:**
- `LI3` (load immediate) values are tracked
- `MOV3` (register copy) propagates constant status
- Sign and zero extension operations propagate constant status
- `ADDI3` (add immediate) becomes a same-width `LI3` when the source is constant
- Arithmetic operations (`ADD3`, `SUB3`, `MUL3`, `DIV3`, etc.) are evaluated when both operands are constants and rewritten to `MOV3` when the result is already available in a tracked register
- Unary operations (`NEG3`, `NOT3`, `BNOT3`) on constants are rewritten to `MOV3` when the result is already available in a tracked register

**Example:**
```c
int x = 42 + 0;  // Rewritten to reuse the known 42 register value
```

**Limitations:**
- Constants are invalidated at control flow boundaries (jumps, calls, returns)
- Constants are invalidated at known branch targets to avoid folding across control-flow joins
- Memory loads reset constant tracking for the destination register
- Two-word register operations are not expanded into wider `LI3` instructions; this keeps branch targets stable
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
- Counts and tracks NOP sequences from previous passes

> **Note:** DCE uses a **conservative approach** to ensure correctness. Full unreachable code elimination after unconditional jumps is not implemented as it requires comprehensive jump target analysis that could affect code with computed gotos, switch tables, or inline assembly. This design choice prioritizes safety over aggressive optimization.

---

## How It Works

The optimizer operates on the generated 32-bit bytecode words in `text_seg[]`
after codegen and before execution:

```
Source Code → Parser → AST → Codegen → Bytecode → [Optimizer] → VM Execution
```

Optimizations transform the bytecode in place. Instructions keep their original
word footprint so PC-index branch targets remain valid:
- Dead instructions are converted to NOPs (`MOV3 r0, r0` or `LI3 r0, 0`)
- NOPs execute with minimal overhead (write to zero register, which is discarded)
- Future work may compact NOPs out entirely

## Best Practices

1. **Development**: Use the default (no `--optimize` flag) for predictable debugging
2. **Testing**: Run the test suite with `--optimize=3` to catch optimization bugs
3. **Production**: Use `--optimize=2` for a good balance of speed and safety

## Combining with Safety Features

Optimization and safety features are independent:

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
