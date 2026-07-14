# CCCC Virtual Machine

> Reference documentation for the CCCC bytecode interpreter, instruction set, ABI, and execution model.

## Overview

CCCC's C frontend produces a portable, register-based bytecode that runs on a built-in interpreter (the **VM**).  The VM is the runtime that powers `[[cccc::macro]]` execution and doubles as a self-contained, introspectable runtime for the memory-safety suite, the debugger, the profiler, and any program run without `-c=native`.  It is intentionally not a JIT-to-machine-code backend: every opcode is interpreted, which keeps the same binary that parses C also able to execute it (or to hand macro-expanded C off to `cc` / `clang` / `gcc` via `-c=native` — see the [README](../README.md)).  This design trades raw execution speed for portability, deep runtime instrumentation, and the ability to run untrusted or sandboxed code in a single self-contained binary.

Key properties:

* **Register-based ISA** — 32 general-purpose integer registers and 32 floating-point registers.
* **Threaded dispatch** — The main interpreter loop uses computed-goto (`goto *op_table[op]`) with opcode bodies inlined at each label.
* **32-bit instruction words** — Opcodes and operands are stored as 32-bit little-endian words.  64-bit immediates consume two consecutive words.
* **Segmented memory** — Text, data, stack, and heap live in separate reserved virtual ranges that grow on demand.
* **Safety-aware opcodes** — Bounds checks, UAF detection, CFI, stack instrumentation, and provenance tracking are implemented as first-class instructions rather than external hooks.
* **GIL-serialized VM threads** — POSIX `pthread` wrappers can run VM entry functions on host pthreads, but bytecode execution is serialized by a recursive VM global interpreter lock. Blocking pthread calls release the GIL while waiting.

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

The floating-point register file (`fregs[32]`) uses the **same indices** and stores a flat `double` per slot.  The frontend already emits type-specific opcodes (`FADD3` vs `FADD3_F32`, `FLDR` vs `FLDR_F32`), so the register itself carries no precision tag.  A `float` value is held as the `double` that results from rounding to float precision and then widening — which is exact — so any register can be read as a `double` with no per-access branch, and the `*_F32` opcodes round their result through `(float)` before the (exact) widening store.  SIMD/vector register state lives in a separate wide-register file rather than in `fregs`.

### Memory Segments

| Segment | Grows | Purpose |
|---------|-------|---------|
| **Text** | Upward | Bytecode instructions (`InstrWord`, 32-bit words) |
| **Data** | Upward | Global variables, string literals, static initialisers |
| **Stack** | Downward | Activation records, locals, spilled arguments |
| **Heap** | Upward | `malloc` / `free` when `--vm-heap` is active |
| **TLS** | Upward | Per-thread copy of thread-local (`_Thread_local` / `__thread`) variables |

All segments are reserved upfront as large virtual ranges and committed in `poolsize` chunks (default 256 KiB elements, max 64 MiB elements).  This gives the VM stable base pointers while keeping resident memory modest.

### Thread-Local Storage (TLS)

Variables declared `_Thread_local`, `__thread`, or `thread_local` are placed in a dedicated TLS segment:

1. **Template (`vm->tls_template`)** — built once by `gen()` alongside the data segment.  Each TLS variable is allocated a slot and its initialiser is written here.  The `LDTLS3` opcode emits the byte offset baked at compile time.
2. **Per-thread copy (`vm->current_tls_seg`)** — allocated by `malloc` for the main thread and for each spawned `pthread_t` or `thrd_t`.  The thread inherits an `memcpy` of the template at creation time (C11 §6.2.4p4 — static initialisation).  On context switch the VM updates `vm->current_tls_seg` to point to the calling thread's copy.
3. **Access (`LDTLS3 rd, imm24`)** — loads the effective address `vm->current_tls_seg + imm24` into `rd`.  Subsequent loads/stores through that pointer are ordinary data-segment accesses.

TLS variable assignment (`vm->tls_template` write) happens in `gen()` at compile time; the per-thread copy is kept consistent via the pthreads context-switch wrappers in `src/stdlib/pthread.c`.

### Calling Convention

* **Integer arguments** are placed in `REG_A0` … `REG_A7`.
* **Floating-point arguments** are placed in `FREG_A0` … `FREG_A7`.
* **Return values** come back in `REG_A0` (integer) or `FREG_A0` (float).
* **Struct returns** use a rotating pool of return buffers allocated in the data segment; the `RETBUF` opcode yields the next buffer address.
* **Spilled arguments** (more than 8) are pushed onto the stack before the call.

The `ENT3` opcode builds the stack frame, copies register arguments and stack-passed fixed arguments to their callee-local parameter slots, and optionally writes a stack canary. For variadic functions, `ENT3` still reserves and spills the first 8 argument slots so `va_arg` can consume any register-passed variadic tail; variadic arguments beyond those slots remain in the caller's stack area. `LEV3` restores `bp`, checks the canary, pops the return address, and resumes at the caller.

### Tail-Call Optimisation

When codegen detects that a `return` statement's outermost expression is a direct call to a statically-known, in-VM, non-variadic, non-nested function whose return type is not a struct/union and whose argument count does not exceed 8, it emits `CALLT` instead of `CALL` followed by `LEV3`.

`CALLT` unwinds the current frame (`sp = bp`, restores saved `bp`) without consuming the return address already on the stack. The callee's subsequent `LEV3` therefore returns directly to the *original* caller, so deeply-recursive helpers and mutually-recursive function pairs execute in O(1) stack space regardless of recursion depth.

**Eligibility** (all must hold, checked at `-O1` and above):

* The call is the sole, outermost expression of the `return` — `return f() + 1` is not a tail call.
* The callee is a directly-addressed in-VM function (not FFI, not a function pointer).
* The callee is not variadic and not a nested function (nested functions require a static-link argument in `REG_A0`).
* The callee does not return a struct or union (struct returns use `RETBUF` and are incompatible with frame reuse).
* The call uses 8 or fewer arguments (stack-spilled arguments would fall below the unwound frame).

Mutually-recursive pairs (`A → B → A`) are handled correctly because `CALLT` leaves the caller's return address in place; `B`'s `CALLT` back to `A` reuses `B`'s frame, and so on.

**Interaction with `__builtin_return_address`**

Because `CALLT` unwinds the intermediate frame, `__builtin_return_address(0)` called from inside a tail-called function returns the *original caller's* return address — the frame for the tail-call site no longer exists on the stack.

Example with `-O1`:

```
// test_fn → tail_wrapper → ra_capture  (CALLT used for tail_wrapper → ra_capture)
static void *ra_capture(void) { return __builtin_return_address(0); }
static void *tail_wrapper(void) { return ra_capture(); }  // tail call → CALLT
```

Inside `ra_capture`:
* `__builtin_return_address(0)` → return address back into `test_fn` (the original caller); `tail_wrapper`'s frame has been unwound.
* `__builtin_return_address(1)` → `NULL` (sentinel: `test_fn` is outermost).
* `__builtin_pc_function_name(__builtin_return_address(0))` → `"test_fn"`, not `"tail_wrapper"`.

This is deterministic and by design: `CALLT` semantics guarantee that the callee's stack view is identical to what it would see if the tail-call site had never existed. Without `-O1` (no TCO), `tail_wrapper`'s frame is present and `__builtin_return_address(0)` returns a PC inside `tail_wrapper`.

### VM Threads

Both the POSIX `<pthread.h>` and C11 `<threads.h>` layers map thread creation to host pthreads while keeping VM execution correctness-first. Each VM thread receives an independent VM stack/register snapshot and enters the requested VM function with the `void *` argument in `REG_A0`. The VM's text, data, heap, globals, FFI registrations, and safety metadata remain shared by the `CCCC` instance.

A recursive global interpreter lock protects the interpreter state. The lock is held while bytecode executes and is released around blocking wrappers such as `pthread_join`, `mtx_lock`, and `cnd_wait`. This gives C code real blocking/wakeup semantics and prevents the interpreter state from racing, but it does not provide parallel bytecode execution.

`pthread_t`, `thrd_t`, `mtx_t`, `cnd_t`, and related thread objects are VM-managed handles, not host ABI layouts. The public headers intentionally expose small VM-facing structs so CCCC bytecode does not depend on platform-specific object sizes.

## Instruction Encoding

Every instruction is one or more 32-bit words:

| Field | Size | Description |
|-------|------|-------------|
| Opcode | 32 bits | `CCCC_OP` enum value |
| Operand words | 0–4 (or 6) | Defined per opcode by `OPS_X` |

Wide immediates (64-bit) are stored little-endian across **two consecutive** 32-bit words.

### Encoding Formats

* **RRR** — `[opcode] [rd:8 | rs1:8 | rs2:8 | unused:8]`  
  Used by three-register arithmetic, comparisons, and FP ops.

* **RRRS** — `[opcode] [rd:8 | base:8 | index:8 | scale:8] [offset:64]`
  Used by fused indexed load/store opcodes.

* **RR** — `[opcode] [rd:8 | rs1:8 | unused:16]`  
  Used by moves, negations, loads, stores, and conversions.

* **RI** — `[opcode] [rd:8 | unused:24] [immediate:64]`  
  Used by `LI3`, `LDA3`, `LTA3`, `LEA3`, `ADDI3`, and branch targets.

* **R** — `[opcode] [rd:8 | unused:56]`  
  Used by single-register operations such as `PSH3`, `POP3`, `JZ3`, `JNZ3`.

The `OPS_X` macro in `src/cccc.h` declares every opcode together with its operand-word count, which is used by the bytecode serialiser, disassembler, and profiler.

## Opcode Reference

Opcodes are grouped by function.  Operands are shown as `rd = destination`, `rs = source`, `rs1/rs2 = sources`.  All integer arithmetic is signed 64-bit unless the opcode name begins with `U`.

### Control Flow

| Opcode | Operands | Description |
|--------|----------|-------------|
| `JMP` | 1 | Unconditional jump to absolute PC (`target`) |
| `CALL` | 1 | Direct call: push return address, jump to `target` |
| `CALLT` | 1 | Tail call: unwind current frame, jump to `target` (direct) |
| `CALLI` | 1 | Indirect call through register (`rs`) |
| `CALLN` | 6 | Native-aware indirect call: tries dynamic symbol first, falls back to VM call |
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
| `ADD3` | `rd = rs1 + rs2` |
| `SUB3` | `rd = rs1 - rs2` |
| `MUL3` | `rd = rs1 * rs2` |
| `MULI3` | `rd = rs1 * immediate` |
| `MULADD3` | `rd = rs1 + rs2 * rs3` |
| `MULADDI3` | `rd = rs1 + rs2 * immediate` |
| `DIV3` | `rd = rs1 / rs2` (signed; traps on divide-by-zero) |
| `ADDC` | Checked signed addition; traps on overflow |
| `SUBC` | Checked signed subtraction; traps on overflow |
| `MULC` | Checked signed multiplication; traps on overflow |
| `DIVC` | Checked signed division; traps on divide-by-zero or `LLONG_MIN / -1` |
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
| `ULT3` | `rd = (rs1 <  rs2)`, unsigned 64-bit |
| `ULE3` | `rd = (rs1 <= rs2)`, unsigned 64-bit |

### Wide _BitInt Operations (N > 64)

Multi-word arithmetic and shift opcodes for `_BitInt(N)` types wider than 64 bits. All are operand-free — source and destination registers are read from the fixed argument register block (REG_A0–REG_A5):

- **Arithmetic:** `REG_A0` = dst, `REG_A1` = a, `REG_A2` = b, `REG_A3` = word-count, `REG_A4` = bit-width
- **Division/remainder:** additionally `REG_A5` = is_signed flag
- **Shifts:** `REG_A0` = dst, `REG_A1` = src, `REG_A2` = shift-amount, `REG_A3` = word-count, `REG_A4` = bit-width

| Opcode | Description |
|--------|-------------|
| `WIDE_ADD` | `dst[i] = a[i] + b[i]` |
| `WIDE_SUB` | `dst[i] = a[i] - b[i]` |
| `WIDE_MUL` | `dst[i] = a[i] * b[i]` |
| `WIDE_DIV` | `dst[i] = a[i] / b[i]` (signed per `REG_A5`) |
| `WIDE_MOD` | `dst[i] = a[i] % b[i]` (signed per `REG_A5`) |
| `WIDE_SHL` | `dst[i] = src[i] << shift_amount` (arithmetic) |
| `WIDE_SHR` | `dst[i] = src[i] >> shift_amount` (arithmetic, signed) |
| `WIDE_USHR` | `dst[i] = src[i] >> shift_amount` (logical, unsigned) |

### Data Movement

| Opcode | Operands | Description |
|--------|----------|-------------|
| `LI3` | 3 | Load 64-bit immediate into `regs[rd]` |
| `LDA3` | 3 | `regs[rd] = data_seg + byte_offset` |
| `LTA3` | 3 | `regs[rd] = text_seg + byte_offset` (stores raw offset) |
| `LEA3` | 3 | `regs[rd] = bp + offset` (local variable address) |
| `RETADDR` | 3 | `regs[rd] = return address of the nth caller frame`. Walks the saved-bp chain `level` steps from `vm->bp`; each step loads `frame[0]` (saved old-bp). Bounds-checks each frame against the live stack (`vm->sp ≤ frame < vm->initial_sp`). On success, sets `regs[rd] = frame[+1]` (the `Pc` return address stored by `CALL`/`CALLI`). Returns `NULL` (0) past the outermost frame. Format: `[RETADDR][rd:8\|unused:56][level:i64]`. Lowered from `__builtin_return_address(n)`. The returned value (a `Pc`/`uint32_t` offset) can be passed to `__builtin_pc_function_name` or `__builtin_pc_source_location` for symbolization; see the [Source Map API](TOOLING.md#source-map-api). |
| `DYNOBJSZ` | 3 | `regs[rd] = runtime byte-size of the object at regs[rs]`. Reads `AllocHeader.requested_size` for base pointers into the VM heap (enabled by `--vm-heap` / `-V`); checks `heap_seg ≤ ptr < heap_end` and validates the `AllocHeader` magic (`0xDEADBEEF`) and `freed` flag. Returns the conservative fallback — `(size_t)-1` (type 0/1) or `0` (type 2/3) — for non-heap, freed, and interior pointers. Format: `[DYNOBJSZ][rd:8\|rs:8\|unused:48][type:i64]`. Lowered from `__builtin_dynamic_object_size(ptr, type)` when the pointer's size cannot be resolved at compile time. |
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

### Atomic Operations

Atomic load, store, and read-modify-write opcodes for concurrency. The access width is encoded in a 64-bit operand word (`(size << 1) | is_unsigned`). All atomic opcodes tag the accessed address in the shadow metadata for mixed-access detection.

| Opcode | Operands | Description |
|--------|----------|-------------|
| `ALDR` | 3 | `regs[rd] = *(T*)regs[rs]` — atomic load; `width_enc` in i64 |
| `ASTR` | 3 | `*(T*)regs[rs] = regs[rd]` — atomic store; `width_enc` in i64 |
| `AXCHG` | 2 | Atomic exchange: `old = *(T*)REG_A0; *(T*)REG_A0 = (T)REG_A1; REG_A0 = old`; `width_enc` in i64 |
| `ACAS` | 2 | Atomic compare-and-swap: if `*(T*)REG_A0 == *(T*)REG_A1` then `*(T*)REG_A0 = (T)REG_A2; REG_A0 = 1` else `*(T*)REG_A1 = *(T*)REG_A0; REG_A0 = 0`; `width_enc` in i64 |

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
| `FMADD3` | `fregs[rd] = fregs[rs1] + fregs[rs2] * fregs[rs3]` (f64, two roundings; emitted by fusion pass) |
| `FMADD3_F32` | f32 two-rounding fused multiply-add |
| `FMADD3_FMA` | f64 single-rounding fused multiply-add via `fma()` (emitted under `--fma`) |
| `FMADD3_F32_FMA` | f32 single-rounding fused multiply-add via `fmaf()` (emitted under `--fma`) |
| `FMSUB3` | `fregs[rd] = fregs[rs2]*fregs[rs3] - fregs[rs1]` (f64, two roundings; emitted by fusion pass) |
| `FMSUB3_F32` | f32 two-rounding fused multiply-subtract |
| `FMSUB3_FMA` | f64 single-rounding fused multiply-subtract via `fma(rs2,rs3,-rs1)` (emitted under `--fma`) |
| `FMSUB3_F32_FMA` | f32 single-rounding fused multiply-subtract via `fmaf(rs2,rs3,-rs1)` (emitted under `--fma`) |
| `FNMSUB3` | `fregs[rd] = fregs[rs1] - fregs[rs2]*fregs[rs3]` (f64, two roundings; emitted by fusion pass) |
| `FNMSUB3_F32` | f32 two-rounding fused negated multiply-subtract |
| `FNMSUB3_FMA` | f64 single-rounding via `fma(-rs2,rs3,rs1)` (emitted under `--fma`) |
| `FNMSUB3_F32_FMA` | f32 single-rounding via `fmaf(-rs2,rs3,rs1)` (emitted under `--fma`) |
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
| `MSET` | `REG_A0` = dest, `REG_A2` = count | `memset` to 0; backs `ND_MEMZERO` (pre-zero for partial aggregate initialisers) |
| `REALC` | `REG_A0` = ptr, `REG_A1` = new_size | `realloc` |
| `CALC` | `REG_A0` = nmemb, `REG_A1` = size | `calloc` |

### Safety Opcodes

These are emitted by the compiler when the corresponding safety flag is set.  At runtime they are no-ops if the flag is cleared, so uninstrumented bytecode runs at full speed.

| Opcode | Description | Controlled by |
|--------|-------------|---------------|
| `CHKB` | Array bounds check on `base + scaled_offset` | `CCCC_BOUNDS_CHECKS` |
| `CHKI` | Uninitialised-variable read check (`bp+offset`) | `CCCC_UNINIT_DETECTION` |
| `MARKI` | Mark variable at `bp+offset` as initialised | `CCCC_UNINIT_DETECTION` |
| `MARKA` | Record stack address for dangling-pointer tracking | `CCCC_DANGLING_DETECT` / `CCCC_STACK_INSTR` |
| `CHKPA` | Validate pointer arithmetic against provenance | `CCCC_INVALID_ARITH` + `CCCC_PROVENANCE_TRACK` |
| `MARKP` | Record pointer provenance (`origin`, `base`, `size`) | `CCCC_PROVENANCE_TRACK` |
| `CHKP3` | Pointer validity (NULL, UAF, heap range) | `CCCC_POINTER_CHECKS` |
| `CHKA3` | Pointer alignment check | `CCCC_ALIGNMENT_CHECKS` |
| `CHKT3` | Heap type-tag check on dereference | `CCCC_TYPE_CHECKS` |

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
| `CALLF` | Registered foreign-function call via `libffi`.  Operands: `ffi_idx`, `nargs`, `double_arg_mask` (2 words), `float_arg_mask` (2 words) — total 6 operand words |
| `CALLN` | Native-aware indirect call (dynamic symbol or VM function).  Operands: `rs`, `meta` (bits 0-15 = nargs, bit 16 = returns_double, bit 17 = returns_float), `double_arg_mask` (2 words), `float_arg_mask` (2 words) — total 6 operand words |

#### Declaring Libraries from Source

`extern` declarations are normally resolved against libraries passed on the
command line with `-l`/`--library`. `#pragma cccc link("name")` queues an
additional library from within the source file itself, merged into the same
list before FFI resolution:

```c
#pragma cccc link("m")          // -> libm.{dylib,so}/m.dll via the normal search
#pragma cccc link("libfoo.dylib") // an explicit filename is used as-is

extern double sqrt(double x);
```

The pragma accepts one or more comma-separated string literals (`link("a", "b")`)
and goes through the same `find_requested_library` / `cc_dlopen` /
`register_dynamic_externs` path as `-l`, so the requested libraries are also
passed to the system linker when compiling with `-c=native`.

When using `-E` (preprocessed output) or `-G` (serialized C output), any
queued libraries are re-emitted at the top of the output as
`#pragma comment(lib, "name")` — the most portable spelling — so downstream
compilers can honour them.

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
| `ADDC` / `SUBC` / `MULC` / `DIVC` | Trapping signed arithmetic emitted for standard `+`, `-`, `*`, and `/` when `--overflow-checks` is enabled. |
| `IOVFL` | Overflow-checked `add`/`sub`/`mul`.  Inputs in `REG_A0`/`REG_A1`, result pointer in `REG_A2`; overflow boolean returned in `REG_A0`.  Operand encodes `op_type`, width, and signedness. |

### Struct Return Buffers

| Opcode | Description |
|--------|-------------|
| `RETBUF` | Return the next buffer from the rotating pool (for non-scalar returns) |

### Trap / Debugger

| Opcode | Description |
|--------|-------------|
| `BTRAP` | Unreachable / builtin trap.  Breaks into the debugger REPL when `-g` is active; otherwise prints a message and aborts.  Emitted for `__builtin_unreachable()`, `__builtin_trap()`, and `__builtin_debugtrap()`. |
| `VSIGNAL` | VM-managed `signal(sig, handler)`. Stores the per-signal action (DFL / IGN / VM function pointer) in `vm_sigslots`. Native dispositions are coordinated with the host-fault dispatcher so guest handlers and `SIG_IGN` take precedence without removing an active default crash trap. Returns the previous handler. |
| `VRAISE` | VM-managed `raise(sig)`. Delivers the signal synchronously from VM context: IGN is a no-op; DFL delegates to the host OS (and may enter the host-fault debugger); a VM handler is invoked by pushing the return address and jumping to the handler. `SIGTRAP` with `-g` enters the debugger REPL instead. |

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

### Fused Indexed Load / Store

These replace simple `base + index * scale + offset` address sequences for
scalar array and pointer accesses. The integer operand word stores `rd`, `base`,
`index`, and an 8-bit byte scale; the following 64-bit immediate stores the byte
offset. Codegen emits these opcodes at `--optimize=2` and above when pointer
safety instrumentation is not active.

| Opcode | Description |
|--------|-------------|
| `LDR_INDEX_B` | `regs[rd] = *(char*)(regs[base] + regs[index] * scale + offset)` |
| `LDR_INDEX_H` | `regs[rd] = *(short*)(regs[base] + regs[index] * scale + offset)` |
| `LDR_INDEX_W` | `regs[rd] = *(int*)(regs[base] + regs[index] * scale + offset)` |
| `LDR_INDEX_D` | `regs[rd] = *(long long*)(regs[base] + regs[index] * scale + offset)` |
| `STR_INDEX_B` | `*(char*)(regs[base] + regs[index] * scale + offset) = regs[rd]` |
| `STR_INDEX_H` | `*(short*)(regs[base] + regs[index] * scale + offset) = regs[rd]` |
| `STR_INDEX_W` | `*(int*)(regs[base] + regs[index] * scale + offset) = regs[rd]` |
| `STR_INDEX_D` | `*(long long*)(regs[base] + regs[index] * scale + offset) = regs[rd]` |
| `FLDR_INDEX` | `fregs[rd] = *(double*)(regs[base] + regs[index] * scale + offset)` |
| `FSTR_INDEX` | `*(double*)(regs[base] + regs[index] * scale + offset) = fregs[rd]` |
| `FLDR_INDEX_F32` | `fregs[rd] = *(float*)(regs[base] + regs[index] * scale + offset)` |
| `FSTR_INDEX_F32` | `*(float*)(regs[base] + regs[index] * scale + offset) = fregs[rd]` |

## Bytecode File Format (`.c4`)

Saved bytecode files are self-contained and can be loaded into a fresh VM instance without recompilation.  The format is versioned (current version **1**).

```
+---------------+  offset 0
| Magic "CCCC\0" |  4 bytes
+---------------+
| Version       |  4 bytes (int)
+---------------+
| Flags         |  4 bytes (CCCCFlags bitfield)
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
| FFI entries   |  name_len, name, num_args, returns_double, returns_float,
|               |  is_variadic, num_fixed_args, double_arg_mask,
|               |  is_dynamic_placeholder, is_asm_passthru,
|               |  asm_src_len, asm_src (asm_src_len bytes)
+---------------+
| FFI policy    |  disable_all_ffi (int),
|               |  allow_count, allow_list strings,
|               |  deny_count, deny_list strings
+---------------+
| TLS tmpl size |  8 bytes (byte length of TLS template)
+---------------+
| TLS template  |  tls_template_size bytes
|               |  (pointer slots stripped to addend, re-anchored on load)
+---------------+
| TLS reloc cnt |  8 bytes
+---------------+
| TLS relocs    |  N × (tls_offset, target_segment, target_offset, addend)
|               |  each field 8 bytes; target_segment: 0=data, 1=text
+---------------+
| [V3] Sym cnt  |  8 bytes — count of exported symbol entries (optional)
+---------------+
| [V3] Sym entries | N × (pc_offset: 8 bytes, name_len: 4 bytes, name: name_len bytes)
|               |  pc_offset is the instruction index of the exported function
+---------------+
| [V3] Reloc cnt|  8 bytes — count of unresolved text-relocation entries
+---------------+
| [V3] Text relocs | N × (location: 8 bytes, name_len: 4 bytes, name: name_len bytes)
|               |  location is the text-seg slot to patch when the symbol is resolved
+---------------+
| [V3] Addr reloc cnt | 8 bytes — count of unresolved function-pointer address sites
+---------------+
| [V3] Addr relocs | N × (location: 8 bytes, name_len: 4 bytes, name: name_len bytes)
|               |  location is the instruction-word index of the lo-word of the LTA3
|               |  i64 immediate; resolved by writing cc_pc_to_byte_offset(target_pc)
+---------------+
```

The V3 sections are appended at the end and are optional — older `.c4` files that
lack them are still valid.  Presence is detected by `cursor < end` in the reader.

On load, the loader re-anchors global pointers, function-pointer offsets, FFI entries, return-buffer addresses, and TLS template pointer slots to the new VM’s segment bases.

**Data reloc / TLS reloc record layout** (each field is a signed 64-bit integer):
```
data_offset     — byte offset within the data/TLS segment where a pointer slot lives
target_offset   — byte offset of the pointed-to symbol within its segment
addend          — addend baked into the pointer (usually 0)
target_segment  — 0 = data segment, 1 = text segment
```

### Module Loading — `.c4d` files

Bytecode dynamic modules (`.c4d`, built via `DynamicLib(kind=bytecode)`) can be
appended into a running VM at runtime:

```c
#include "cccc.h"
int cc_load_module(VirtualMachine *vm, const char *path);
```

`cc_load_module` loads the `.c4d` file into a staging VM via `cc_load_bytecode`,
then merges its segments into the host VM:

1. **Text append**: the staging text words (indices 1…N, skipping the `main_offset`
   metadata slot at 0) are appended to `vm->text_seg`. All absolute-PC operands
   in the appended block are patched by `pc_shift = host_vm->text_ptr` — the
   instruction-index offset at append time.
2. **PC-typed operands patched**: `JMP`/`CALL`/`CALLT` (operand 0), `JZ3`/`JNZ3`
   (operand 1), `JMPT` (operand 0 and operand 2 plus inline table entries), `LTA3`
   (64-bit byte-offset immediate, shifted by `pc_shift × sizeof(InstrWord)`).
3. **Data append**: staging data bytes are appended; all data-reloc pointer slots
   are re-anchored to the host VM's segment base addresses.
4. **TLS merge**: TLS template and relocs from the module are appended and adjusted.
5. **Return-buffer merge**: return-buffer pool entries are merged with adjusted
   `data_shift` offsets.
6. **FFI merge**: FFI table entries from the module are merged (name strings are
   transferred; `asm_src` passthru entries remain independently rehydratable).
7. **Symbol resolution**: the host VM's pending text relocations (`vm->compiler.text_relocs`)
   are scanned against the module's exported symbol table.  Any CALL site whose
   target symbol name appears in the module is patched to `sym.pc_offset + pc_shift`
   and marked resolved.  Address relocations (`vm->compiler.addr_relocs`) are also
   resolved: each records the lo-word PC of an LTA3 i64 immediate for a cross-module
   function-pointer; the resolved value is `cc_pc_to_byte_offset(sym.pc_offset + pc_shift)`
   written via `cc_write_i64_at`.

### Static Library Linking — `.c4a` files and `cc_link_bytecode`

Bytecode static libraries (`.c4a`, built via `StaticLib(kind=bytecode)`) are linked
at **compile time** by the `--link` flag or by the build system's `LinkWith` on a
bytecode executable target.

```c
#include "cccc.h"
int cc_link_bytecode(VirtualMachine *vm, const char *path);
```

`cc_link_bytecode` is a thin wrapper around `cc_load_module` that performs the same
segment-append and symbol-resolution steps.  It is called by the compiler after
generating a bytecode executable with unresolved external `CALL` sites:

1. The compiler emits text-relocation entries instead of erroring when a called
   symbol is declared but not yet defined (`deferred_link` mode).  Function-pointer
   address-of expressions against cross-module symbols similarly emit address-relocation
   entries rather than erroring.
2. After compilation, each `--link lib.c4a` file is processed by `cc_link_bytecode`.
3. Any text relocation whose name matches a symbol exported by the library has its
   CALL site patched to the correct PC.  Any address relocation whose name matches
   has its LTA3 i64 immediate patched to the byte-offset of the resolved symbol.
4. After all libraries are linked, any remaining unresolved text or address relocations
   cause a hard link error.
5. The final bytecode (with all CALL and function-pointer sites resolved) is written
   to `-o <file>`.

The build system automatically adds `--link dep.c4a` flags for all `LinkWith` edges
on a `kind=bytecode` executable target.  `.c4a` dependencies are compiled standalone
first (with `--compile=bytecode`), then linked into the executable in a separate
step.

### Asm-Passthru Rehydration

FFI entries created by `--asm-passthru` cannot survive serialisation as raw function pointers because the compiled shared library is unlinked immediately after `dlopen`.  The `.c4` format stores the original assembly source string (`asm_src`) alongside each such entry (flagged `is_asm_passthru = 1`).  On load, `cc_rehydrate_asm_passthru()` recompiles each `asm_src` string into a fresh temporary shared library, `dlopen`s it, and resolves the function pointer — making the round-trip transparent to the program.

Rehydration applies the same FFI allow/deny policy as other symbol lookups: if `--disable-ffi` is active, or the symbol is on the deny list, the entry is left unresolved and a `CALLF` targeting it will fail at execution time with `error: FFI function … not resolved`.

Because recompilation uses the host’s native C compiler at `.c4` run time, the resulting bytecode file is architecture-portable but requires a C compiler to be available when it is executed.

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

`main()` is invoked with a synthetic return address of `0`.  `cc_run()` passes `argc` in `REG_A0` and `argv` in `REG_A1`; for loaded `.c4` files, `argv[0]` is the `.c4` path and arguments after `--` are forwarded to the program.  When `LEV3` pops this sentinel, it sets `vm->pc = CCCC_INVALID_PC` and the dispatch loop returns `(int)vm->regs[REG_A0]` as the process exit code.

### Debugger Integration

When `CCCC_ENABLE_DEBUGGER` is set, every dispatch checks breakpoints, single-step flags, and step-over / step-out conditions before executing the opcode.  `BTRAP` can also force entry into the interactive REPL.

## Safety Integration

The VM does not rely on external sanitizer libraries.  Instead, the compiler injects safety opcodes at compile time and the interpreter implements the checks inline:

* **Bounds checks** — `CHKB` before every array-subscript or pointer-dereference that the compiler can annotate with a size.
* **UAF detection** — `CHKP3` consults `AllocHeader` metadata (magic `0xDEADBEEF`, `freed` bit, generation counter).
* **Uninitialised reads** — `CHKI` / `MARKI` maintain a per-address hash map of initialised stack slots.
* **Stack canaries** — `ENT3` writes a canary word; `LEV3` validates it before returning.
* **CFI** — A shadow stack mirrors the real stack; `CALL` pushes to both, `CALLT` pops the current frame's entry (consuming one shadow-slot but not pushing a new one), and `LEV3` compares before trusting the return address.
* **Provenance tracking** — `MARKP` records `(origin, base, size)`; `CHKPA` rejects arithmetic that leaves the object.
* **Dangling pointers** — `MARKA` records stack addresses; `SCOPEOUT` detects live pointers to variables that are going out of scope.

Because every check is guarded by a runtime flag, the same bytecode can run with full safety or with zero overhead simply by changing the active `CCCCFlags` mask.

## Opcode Profiling

The VM can collect dynamic execution statistics:

* **Per-opcode counts** — How many times each opcode was executed.
* **Bigram profile** — Transition frequencies `prev → cur` (stored in a flat `OP_COUNT × OP_COUNT` array).
* **Trigram profile** — Transition frequencies `prev2 → prev → cur` (heap-allocated `OP_COUNT³` array, enabled on demand).

Enable profiling with `--vm-profile` (text report to stderr). Combine it with `--json` to also write the same data as JSON to stdout.  The JSON schema includes `total_opcodes`, `total_bigrams`, per-opcode arrays, and per-bigram arrays with percentages.

Static n-gram mining (`cccc --ngrams`) and use-def fusion analysis (`cccc --fusion-candidates`) complement the dynamic data by showing which sequences are common in the bytecode *and* hot at runtime. `--optimize=4` / `--fuse-ops` uses the same in-process use-def analysis to apply registered fused-op rewrites automatically.

## Performance Notes

The VM is the runtime for compile-time macro bodies and for VM-only workflows (the safety suite, the debugger, the profiler, quick iteration without a system compiler).  For production code, `-c=native` hands macro-expanded C to `cc` / `clang` / `gcc` and skips the VM entirely, so the interpreter cost only matters for the things that *run on it*.

Seven optimisations have significantly reduced interpreter overhead:

1. **Inlined threaded dispatch** — Opcode logic lives at computed-goto labels; there is no function call per instruction.
2. **Fused local load/store** — The common `LEA3 + LDR/STR` pair for local variables is collapsed into a single `LDR_LOCAL_*` / `STR_LOCAL_*` opcode, saving one dispatch and one register-pressure hop per access.
3. **Scalar local promotion** — Hot eligible integer, pointer, and floating-point locals are held in callee-saved VM registers at `--optimize=2` and above. Integer/pointer locals use `REG_S0`–`S3`; `float`/`double` locals use `FREG_S0`–`S3`.
4. **Fused indexed load/store** — Simple array and pointer accesses use `LDR_INDEX_*` / `STR_INDEX_*`, removing separate index multiply and address-add opcodes in hot loops.
5. **Automatic opcode fusion** — `--optimize=4` / `--fuse-ops` rewrites adjacent single-def/single-use arithmetic chains to fused opcodes such as `MULI3`, `MULADD3`, `MULADDI3`, `FMADD3`, `FMADD3_F32`, `FMSUB3`, `FMSUB3_F32`, `FNMSUB3`, and `FNMSUB3_F32`.
6. **Fused floating-point multiply-add/subtract** — `FMUL3+FADD3` chains fuse to `FMADD3`; `FMUL3+FSUB3` fuses to `FMSUB3` (minuend form) or `FNMSUB3` (accumulating-subtract form). Dead-FMOV3 elimination in copy-prop restores adjacency when float local promotion inserts a register-copy between the multiply and subtract.
7. **Tail-call optimisation** — `return f(args)` patterns that meet eligibility criteria emit `CALLT` instead of `CALL + LEV3`, reducing tail-recursive calls to O(1) stack depth (see [Tail-Call Optimisation](#tail-call-optimisation) above).

The dominant cost remains the interpreter itself (as opposed to compile time); see [TOOLING.md](TOOLING.md#benchmarks) for full numbers and [TOOLING.md](TOOLING.md#profiling) for analysis tooling.
