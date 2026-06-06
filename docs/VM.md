# JCC Virtual Machine

> Reference documentation for the JCC bytecode interpreter, instruction set, ABI, and execution model.

## Overview

JCC's C frontend produces a portable, register-based bytecode that runs on a built-in interpreter (the **VM**).  The VM is the runtime that powers `[[jcc::macro]]` execution and doubles as a self-contained, introspectable runtime for the memory-safety suite, the debugger, the profiler, and any program run without `-c=native`.  It is intentionally not a JIT-to-machine-code backend: every opcode is interpreted, which keeps the same binary that parses C also able to execute it (or to hand macro-expanded C off to `cc` / `clang` / `gcc` via `-c=native` — see the [README](../README.md)).  This design trades raw execution speed for portability, deep runtime instrumentation, and the ability to run untrusted or sandboxed code in a single self-contained binary.

Key properties:

* **Register-based ISA** — 32 general-purpose integer registers and 32 tagged floating-point registers.
* **Threaded dispatch** — The main interpreter loop uses computed-goto (`goto *op_table[op]`) with opcode bodies inlined at each label.
* **32-bit instruction words** — Opcodes and operands are stored as 32-bit little-endian words.  64-bit immediates consume two consecutive words.
* **Segmented memory** — Text, data, stack, and heap live in separate reserved virtual ranges that grow on demand.
* **Safety-aware opcodes** — Bounds checks, UAF detection, CFI, stack instrumentation, and provenance tracking are implemented as first-class instructions rather than external hooks.

## Architecture

### Register File

| Register | Name | Role |
|----------|------|------|
| `r0` | `REG_ZERO` | Hard-wired zero (writes are discarded) |
| `r1` | `REG_RA` | Return address (conventional) |
| `r2` | `REG_SP` | Reserved (the VM uses `vm->sp` directly) |
| `r3–r4` | — | Reserved |
| `r5–r9` | `REG_T0` … `REG_T4` | Caller-saved temporaries |
| `r10–r17` | `REG_A0` … `REG_A7` | Arguments / return values (caller-saved) |
| `r18–r25` | `REG_S0` … `REG_S7` | Callee-saved registers |
| `r26–r31` | `REG_T5` … `REG_T10` | Caller-saved temporaries |

The floating-point register file (`fregs[32]`) uses the **same indices** but stores a tagged union (`JCCFReg`).  Each slot carries a tag (`JCC_FREG_F64`, `JCC_FREG_F32`, `JCC_FREG_V4F32`, `JCC_FREG_V2F64`) so mixed-precision code is handled without silent reinterpretation.

### Memory Segments

| Segment | Grows | Purpose |
|---------|-------|---------|
| **Text** | Upward | Bytecode instructions (`JCCInstrWord`, 32-bit words) |
| **Data** | Upward | Global variables, string literals, static initialisers |
| **Stack** | Downward | Activation records, locals, spilled arguments |
| **Heap** | Upward | `malloc` / `free` when `--vm-heap` is active |

All segments are reserved upfront as large virtual ranges and committed in `poolsize` chunks (default 256 KiB elements, max 64 MiB elements).  This gives the VM stable base pointers while keeping resident memory modest.

### Calling Convention

* **Integer arguments** are placed in `REG_A0` … `REG_A7`.
* **Floating-point arguments** are placed in `FREG_A0` … `FREG_A7`.
* **Return values** come back in `REG_A0` (integer) or `FREG_A0` (float).
* **Struct returns** use a rotating pool of return buffers allocated in the data segment; the `RETBUF` opcode yields the next buffer address.
* **Spilled arguments** (more than 8) are pushed onto the stack before the call.

The `ENT3` opcode builds the stack frame, copies register arguments and stack-passed fixed arguments to their callee-local parameter slots, and optionally writes a stack canary. For variadic functions, `ENT3` still reserves and spills the first 8 argument slots so `va_arg` can consume any register-passed variadic tail; variadic arguments beyond those slots remain in the caller's stack area. `LEV3` restores `bp`, checks the canary, pops the return address, and resumes at the caller.

## Instruction Encoding

Every instruction is one or more 32-bit words:

| Field | Size | Description |
|-------|------|-------------|
| Opcode | 32 bits | `JCC_OP` enum value |
| Operand words | 0–4 (or 6) | Defined per opcode by `OPS_X` |

Wide immediates (64-bit) are stored little-endian across **two consecutive** 32-bit words.

### Encoding Formats

* **RRR** — `[opcode] [rd:8 | rs1:8 | rs2:8 | unused:8]`  
  Used by three-register arithmetic, comparisons, and FP ops.

* **RR** — `[opcode] [rd:8 | rs1:8 | unused:16]`  
  Used by moves, negations, loads, stores, and conversions.

* **RI** — `[opcode] [rd:8 | unused:24] [immediate:64]`  
  Used by `LI3`, `LDA3`, `LTA3`, `LEA3`, `ADDI3`, and branch targets.

* **R** — `[opcode] [rd:8 | unused:56]`  
  Used by single-register operations such as `PSH3`, `POP3`, `JZ3`, `JNZ3`.

The `OPS_X` macro in `src/jcc.h` declares every opcode together with its operand-word count, which is used by the bytecode serialiser, disassembler, and profiler.

## Opcode Reference

Opcodes are grouped by function.  Operands are shown as `rd = destination`, `rs = source`, `rs1/rs2 = sources`.  All integer arithmetic is signed 64-bit unless the opcode name begins with `U`.

### Control Flow

| Opcode | Operands | Description |
|--------|----------|-------------|
| `JMP` | 1 | Unconditional jump to absolute PC (`target`) |
| `CALL` | 1 | Direct call: push return address, jump to `target` |
| `CALLI` | 1 | Indirect call through register (`rs`) |
| `CALLN` | 4 | Native-aware indirect call: tries dynamic symbol first, falls back to VM call |
| `JMPT` | 3 | Jump table: `table_pc`, `count`, `default_pc`; index in `REG_A0` |
| `JMPI` | 1 | Indirect jump through register |
| `JZ3` | 2 | Branch if `regs[rs] == 0` |
| `JNZ3` | 2 | Branch if `regs[rs] != 0` |

### Function Frame

| Opcode | Operands | Description |
|--------|----------|-------------|
| `ENT3` | 4 | Enter function: `stack_size`, `spill_param_count`, `float_param_mask`, `f32_param_mask` |
| `LEV3` | 0 | Leave function: restore frame, check canary, return to caller |
| `ADJ` | 2 | Adjust stack pointer by signed immediate |
| `PSH3` | 1 | Push `regs[rs]` onto the stack |
| `POP3` | 1 | Pop stack into `regs[rd]` |

### Integer Arithmetic (3-Register)

| Opcode | Description |
|--------|-------------|
| `ADD3` | `rd = rs1 + rs2` (overflow-check when `JCC_OVERFLOW_CHECKS`) |
| `SUB3` | `rd = rs1 - rs2` |
| `MUL3` | `rd = rs1 * rs2` |
| `DIV3` | `rd = rs1 / rs2` (signed; traps on divide-by-zero) |
| `UDIV3` | Unsigned division |
| `MOD3` | Signed remainder (traps on modulo-by-zero) |
| `UMOD3` | Unsigned remainder |
| `AND3` | Bitwise AND |
| `OR3` | Bitwise OR |
| `XOR3` | Bitwise XOR |
| `SHL3` | Left shift |
| `SHR3` | Arithmetic right shift (signed) |
| `USHR3` | Logical right shift (unsigned) |
| `ADDI3` | `rd = rs1 + immediate` (RI format) |
| `NEG3` | `rd = -rs1` |
| `NOT3` | `rd = !rs1` (logical) |
| `BNOT3` | `rd = ~rs1` (bitwise) |

### Comparisons (3-Register)

| Opcode | Description |
|--------|-------------|
| `SEQ3` | `rd = (rs1 == rs2)` |
| `SNE3` | `rd = (rs1 != rs2)` |
| `SLT3` | `rd = (rs1 <  rs2)` |
| `SLE3` | `rd = (rs1 <= rs2)` |
| `SGT3` | `rd = (rs1 >  rs2)` |
| `SGE3` | `rd = (rs1 >= rs2)` |

### Data Movement

| Opcode | Operands | Description |
|--------|----------|-------------|
| `LI3` | 3 | Load 64-bit immediate into `regs[rd]` |
| `LDA3` | 3 | `regs[rd] = data_seg + byte_offset` |
| `LTA3` | 3 | `regs[rd] = text_seg + byte_offset` (stores raw offset) |
| `LEA3` | 3 | `regs[rd] = bp + offset` (local variable address) |
| `MOV3` | 1 | Register-to-register move |

### Load / Store

Sizes: **B** = byte, **H** = half-word (16-bit), **W** = word (32-bit), **D** = double-word (64-bit).  Integer loads sign-extend to 64 bits.

| Opcode | Description |
|--------|-------------|
| `LDR_B` | `regs[rd] = *(char*)regs[rs]` |
| `LDR_H` | `regs[rd] = *(short*)regs[rs]` |
| `LDR_W` | `regs[rd] = *(int*)regs[rs]` |
| `LDR_D` | `regs[rd] = *(long long*)regs[rs]` |
| `STR_B` | `*(char*)regs[rs] = regs[rd]` |
| `STR_H` | `*(short*)regs[rs] = regs[rd]` |
| `STR_W` | `*(int*)regs[rs] = regs[rd]` |
| `STR_D` | `*(long long*)regs[rs] = regs[rd]` |

### Floating-Point Operations

All `F*` opcodes operate on `fregs[]`.  Comparisons write a boolean into an integer register.

| Opcode | Description |
|--------|-------------|
| `FADD3` | `fregs[rd] = fregs[rs1] + fregs[rs2]` (f64) |
| `FSUB3` | `fregs[rd] = fregs[rs1] - fregs[rs2]` (f64) |
| `FMUL3` | `fregs[rd] = fregs[rs1] * fregs[rs2]` (f64) |
| `FDIV3` | `fregs[rd] = fregs[rs1] / fregs[rs2]` (f64; traps on zero) |
| `FMOV3` | `fregs[rd] = fregs[rs1]` (f64) |
| `FNEG3` | `fregs[rd] = -fregs[rs1]` (f64) |
| `FEQ3` … `FGE3` | f64 comparisons |
| `FADD3_F32` … `FDIV3_F32` | f32 arithmetic |
| `FNEG3_F32` | f32 negation |
| `FEQ3_F32` … `FGE3_F32` | f32 comparisons |
| `FLDR` | `fregs[rd] = *(double*)regs[rs]` |
| `FSTR` | `*(double*)regs[rs] = fregs[rd]` |
| `FLDR_F32` | `fregs[rd] = *(float*)regs[rs]` |
| `FSTR_F32` | `*(float*)regs[rs] = fregs[rd]` |
| `FROUND_F32` | `fregs[rd] = (float)fregs[rs]` |
| `I2F3` | `fregs[rd] = (double)regs[rs]` |
| `F2I3` | `regs[rd] = (long long)fregs[rs]` |
| `I2F3_F32` | `fregs[rd] = (float)regs[rs]` |
| `F2I3_F32` | `regs[rd] = (long long)(float)fregs[rs]` |
| `FR2R` | Bit-pattern transfer `freg → reg` (f64) |
| `R2FR` | Bit-pattern transfer `reg → freg` (f64) |
| `FR2R_F32` | Bit-pattern transfer `freg → reg` (f32) |
| `R2FR_F32` | Bit-pattern transfer `reg → freg` (f32) |

### Type Conversion

| Opcode | Description |
|--------|-------------|
| `SX1` | Sign-extend 1 byte → 64 bits |
| `SX2` | Sign-extend 2 bytes → 64 bits |
| `SX4` | Sign-extend 4 bytes → 64 bits |
| `ZX1` | Zero-extend 1 byte → 64 bits |
| `ZX2` | Zero-extend 2 bytes → 64 bits |
| `ZX4` | Zero-extend 4 bytes → 64 bits |

### VM Memory Management

These opcodes implement the C standard library heap when `--vm-heap` is enabled.

| Opcode | Arguments | Description |
|--------|-----------|-------------|
| `MALC` | `REG_A0` = size | Allocate; pointer returned in `REG_A0` |
| `MFRE` | `REG_A0` = ptr | Free pointer (detects double-free) |
| `MCPY` | `REG_A0` = dest, `REG_A1` = src, `REG_A2` = count | `memcpy` |
| `REALC` | `REG_A0` = ptr, `REG_A1` = new_size | `realloc` |
| `CALC` | `REG_A0` = nmemb, `REG_A1` = size | `calloc` |

### Safety Opcodes

These are emitted by the compiler when the corresponding safety flag is set.  At runtime they are no-ops if the flag is cleared, so uninstrumented bytecode runs at full speed.

| Opcode | Description | Controlled by |
|--------|-------------|---------------|
| `CHKB` | Array bounds check on `base + scaled_offset` | `JCC_BOUNDS_CHECKS` |
| `CHKI` | Uninitialised-variable read check (`bp+offset`) | `JCC_UNINIT_DETECTION` |
| `MARKI` | Mark variable at `bp+offset` as initialised | `JCC_UNINIT_DETECTION` |
| `MARKA` | Record stack address for dangling-pointer tracking | `JCC_DANGLING_DETECT` / `JCC_STACK_INSTR` |
| `CHKPA` | Validate pointer arithmetic against provenance | `JCC_INVALID_ARITH` + `JCC_PROVENANCE_TRACK` |
| `MARKP` | Record pointer provenance (`origin`, `base`, `size`) | `JCC_PROVENANCE_TRACK` |
| `CHKP3` | Pointer validity (NULL, UAF, heap range) | `JCC_POINTER_CHECKS` |
| `CHKA3` | Pointer alignment check | `JCC_ALIGNMENT_CHECKS` |
| `CHKT3` | Heap type-tag check on dereference | `JCC_TYPE_CHECKS` |

### Stack Instrumentation Opcodes

| Opcode | Description |
|--------|-------------|
| `SCOPEIN` | Activate variables belonging to a lexical scope |
| `SCOPEOUT` | Deactivate variables and detect dangling pointers |
| `CHKL` | Check variable liveness before access (use-after-scope / use-after-return) |
| `MARKR` | Record a read access to a stack variable |
| `MARKW` | Record a write access; marks variable as initialised |

### Non-Local Jumps

| Opcode | Description |
|--------|-------------|
| `SETJMP` | Save `pc`, `sp`, `bp`, (and shadow-sp if CFI) into `jmp_buf`; return 0 |
| `LONGJMP` | Restore context from `jmp_buf`; return value in `REG_A1` (never 0) |

### Dynamic Linking

| Opcode | Description |
|--------|-------------|
| `DLOPEN` | `dlopen(path, mode)` — result in `REG_A0` |
| `DLSYM` | `dlsym(handle, symbol)` — result in `REG_A0` |
| `DLCLOSE` | `dlclose(handle)` — result in `REG_A0` |
| `DLERROR` | Return last dynamic-link error string in `REG_A0` |

### FFI

| Opcode | Description |
|--------|-------------|
| `CALLF` | Registered foreign-function call via `libffi`.  Operands: `ffi_idx`, `nargs`, `double_arg_mask` |
| `CALLN` | Native-aware indirect call (dynamic symbol or VM function).  Operands: `rs`, `meta`, `double_arg_mask` |

### Bit-Manipulation Builtins

| Opcode | Description |
|--------|-------------|
| `CLZ` | Count leading zeros (`operand2` = 32 or 64) |
| `CTZ` | Count trailing zeros |
| `POPCOUNT` | Population count (width-agnostic, 64-bit) |
| `FFS` | Find first set bit (0 → 0) |
| `BSWAP` | Byte swap (`operand2` = 2, 4, or 8) |

### Checked Arithmetic

| Opcode | Description |
|--------|-------------|
| `IOVFL` | Overflow-checked `add`/`sub`/`mul`.  Inputs in `REG_A0`/`REG_A1`, result pointer in `REG_A2`; overflow boolean returned in `REG_A0`.  Operand encodes `op_type`, width, and signedness. |

### Struct Return Buffers

| Opcode | Description |
|--------|-------------|
| `RETBUF` | Return the next buffer from the rotating pool (for non-scalar returns) |

### Trap / Debugger

| Opcode | Description |
|--------|-------------|
| `BTRAP` | Unreachable / builtin trap.  Breaks into the debugger REPL when `-g` is active; otherwise prints a message and aborts.  Emitted for `__builtin_unreachable()`, `__builtin_trap()`, and `__builtin_debugtrap()`. |
| `VSIGNAL` | VM-managed `signal(sig, handler)`.  Stores the per-signal action (DFL / IGN / VM function pointer) in `vm_sigslots` and installs an async-safe native shim when a VM handler is registered. Returns the previous handler. |
| `VRAISE` | VM-managed `raise(sig)`.  Delivers the signal synchronously from VM context: IGN is a no-op; DFL delegates to the host OS; a VM handler is invoked by pushing the return address and jumping to the handler.  `SIGTRAP` with `-g` enters the debugger REPL instead. |

### Fused Local Load / Store

These replace the common `LEA3 + LDR/STR` two-opcode sequence for local variables, eliminating one dispatch per scalar access.

| Opcode | Description |
|--------|-------------|
| `LDR_LOCAL_B` | `regs[rd] = *(char*)(bp + offset)` |
| `LDR_LOCAL_H` | `regs[rd] = *(short*)(bp + offset)` |
| `LDR_LOCAL_W` | `regs[rd] = *(int*)(bp + offset)` |
| `LDR_LOCAL_D` | `regs[rd] = *(long long*)(bp + offset)` |
| `STR_LOCAL_B` | `*(char*)(bp + offset) = regs[rd]` |
| `STR_LOCAL_H` | `*(short*)(bp + offset) = regs[rd]` |
| `STR_LOCAL_W` | `*(int*)(bp + offset) = regs[rd]` |
| `STR_LOCAL_D` | `*(long long*)(bp + offset) = regs[rd]` |
| `FLDR_LOCAL` | `fregs[rd] = *(double*)(bp + offset)` |
| `FSTR_LOCAL` | `*(double*)(bp + offset) = fregs[rd]` |
| `FLDR_LOCAL_F32` | `fregs[rd] = *(float*)(bp + offset)` |
| `FSTR_LOCAL_F32` | `*(float*)(bp + offset) = fregs[rd]` |

## Bytecode File Format (`.jbc`)

Saved bytecode files are self-contained and can be loaded into a fresh VM instance without recompilation.  The format is versioned (current version **7**).

```
+---------------+  offset 0
| Magic "JCC\0" |  4 bytes
+---------------+
| Version       |  4 bytes (int)
+---------------+
| Flags         |  4 bytes (JCCFlags bitfield)
+---------------+
| Text size     |  8 bytes (bytes, not words)
+---------------+
| Data size     |  8 bytes
+---------------+
| Main offset   |  8 bytes (instruction index of main())
+---------------+
| Data reloc cnt|  8 bytes
+---------------+
| Text segment  |  text_size bytes
+---------------+
| Data segment  |  data_size bytes
+---------------+
| Data relocs   |  N × (data_offset, target_segment, target_offset, addend)
+---------------+
| Ret buf count |  8 bytes
+---------------+
| Ret buf size  |  8 bytes
+---------------+
| Ret buf offsets| N × 8 bytes
+---------------+
| FFI count     |  8 bytes
+---------------+
| FFI entries   |  name_len, name, num_args, returns_double,
|               |  is_variadic, num_fixed_args, double_arg_mask,
|               |  is_dynamic_placeholder
+---------------+
| FFI policy    |  disable_all_ffi (int),
|               |  allow_count, allow_list strings,
|               |  deny_count, deny_list strings
+---------------+
```

On load, the loader re-anchors global pointers, function-pointer offsets, FFI entries, and return-buffer addresses to the new VM’s segment bases.

## Execution Model

### Threaded Dispatch

`vm_eval` (in `src/vm.c`) builds a static jump table from `OPS_X`:

```c
static void *op_table[] = {
#define X(NAME, OPERANDS) [NAME] = &&op_##NAME,
    OPS_X
#undef X
};
```

The central loop reads the next opcode, increments the cycle counter, optionally profiles it, and dispatches with `goto *op_table[op]`.  Each opcode body ends by jumping back to `dispatch`.

### Exit Detection

`main()` is invoked with a synthetic return address of `0`.  When `LEV3` pops this sentinel, it sets `vm->pc = JCC_INVALID_PC` and the dispatch loop returns `(int)vm->regs[REG_A0]` as the process exit code.

### Debugger Integration

When `JCC_ENABLE_DEBUGGER` is set, every dispatch checks breakpoints, single-step flags, and step-over / step-out conditions before executing the opcode.  `BTRAP` can also force entry into the interactive REPL.

## Safety Integration

The VM does not rely on external sanitizer libraries.  Instead, the compiler injects safety opcodes at compile time and the interpreter implements the checks inline:

* **Bounds checks** — `CHKB` before every array-subscript or pointer-dereference that the compiler can annotate with a size.
* **UAF detection** — `CHKP3` consults `AllocHeader` metadata (magic `0xDEADBEEF`, `freed` bit, generation counter).
* **Uninitialised reads** — `CHKI` / `MARKI` maintain a per-address hash map of initialised stack slots.
* **Stack canaries** — `ENT3` writes a canary word; `LEV3` validates it before returning.
* **CFI** — A shadow stack mirrors the real stack; `CALL` pushes to both, `LEV3` compares before trusting the return address.
* **Provenance tracking** — `MARKP` records `(origin, base, size)`; `CHKPA` rejects arithmetic that leaves the object.
* **Dangling pointers** — `MARKA` records stack addresses; `SCOPEOUT` detects live pointers to variables that are going out of scope.

Because every check is guarded by a runtime flag, the same bytecode can run with full safety or with zero overhead simply by changing the active `JCCFlags` mask.

## Opcode Profiling

The VM can collect dynamic execution statistics:

* **Per-opcode counts** — How many times each opcode was executed.
* **Bigram profile** — Transition frequencies `prev → cur` (stored in a flat `OP_COUNT × OP_COUNT` array).
* **Trigram profile** — Transition frequencies `prev2 → prev → cur` (heap-allocated `OP_COUNT³` array, enabled on demand).

Enable profiling with `--vm-profile` (text report to stderr). Combine it with `--json` to also write the same data as JSON to stdout.  The JSON schema includes `total_opcodes`, `total_bigrams`, per-opcode arrays, and per-bigram arrays with percentages.

Static n-gram mining (`jcc --ngrams`) and use-def fusion analysis (`jcc --fusion-candidates`) complement the dynamic data by showing which sequences are common in the bytecode *and* hot at runtime — the strongest candidates for new fused opcodes.

## Performance Notes

The VM is the runtime for compile-time macro bodies and for VM-only workflows (the safety suite, the debugger, the profiler, quick iteration without a system compiler).  For production code, `-c=native` hands macro-expanded C to `cc` / `clang` / `gcc` and skips the VM entirely, so the interpreter cost only matters for the things that *run on it*.

Two optimisations have significantly reduced interpreter overhead:

1. **Inlined threaded dispatch** — Opcode logic lives at computed-goto labels; there is no function call per instruction.
2. **Fused local load/store** — The common `LEA3 + LDR/STR` pair for local variables is collapsed into a single `LDR_LOCAL_*` / `STR_LOCAL_*` opcode, saving one dispatch and one register-pressure hop per access.

The dominant cost remains the interpreter itself (as opposed to compile time); see [BENCHMARKS.md](BENCHMARKS.md) for full numbers and [PROFILING.md](PROFILING.md) for analysis tooling.
