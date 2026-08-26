# CCCC Virtual Machine

> Reference documentation for the CCCC bytecode interpreter, instruction set, ABI, and execution model.

## Overview

CCCC's C frontend produces a portable, register-based bytecode that runs on a built-in interpreter (the **VM**).  The VM is the runtime that powers `[[cccc::comptime]]` execution and doubles as a self-contained, introspectable runtime for the memory-safety suite, the debugger, the profiler, and any program run without `-c=native`.  It is intentionally not a JIT-to-machine-code backend: every opcode is interpreted, which keeps the same binary that parses C also able to execute it (or to hand macro-expanded C off to `cc` / `clang` / `gcc` via `-c=native` — see the [README](../README.md)).  This design trades raw execution speed for portability, deep runtime instrumentation, and the ability to run untrusted or sandboxed code in a single self-contained binary.

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

`r0`/`REG_ZERO` must never be used as an address or scratch register in codegen: writes to it are discarded, so a subsequent load through it always reads from address 0 rather than whatever value was "stored". This is distinct from `r0` legitimately being a load/store *destination* for a discarded-value result — the integer `op_LDR_*_fn` ops guard `rd != REG_ZERO` before writing back, so they simply skip the write; the float `op_FLDR_fn`/`op_FLDR_F32_fn` ops have no such guard and write `fregs[0]` unconditionally, which is harmless only because nothing treats `fregs[0]` as load-bearing. A codegen path that computes an address into `dest_reg` and then loads through that same register must first redirect any `REG_ZERO` destination to a real temporary (see the `gen_expr` guard at `src/codegen_expr.c` ahead of `ND_DEREF`/`ND_MEMBER`, added for ticket #916 — a discarded float/double deref used to compute its address into `r0`, i.e. address 0, and segfault on the load).

The floating-point register file (`fregs[32]`) uses the **same indices** and stores a flat `double` per slot.  The frontend already emits type-specific opcodes (`FADD3` vs `FADD3_F32`, `FLDR` vs `FLDR_F32`), so the register itself carries no precision tag.  A `float` value is held as the `double` that results from rounding to float precision and then widening — which is exact — so any register can be read as a `double` with no per-access branch, and the `*_F32` opcodes round their result through `(float)` before the (exact) widening store.

A third register file, `vregs[32]` (`VReg`, see [SIMD / Vector Operations](#simd--vector-operations)), holds up to 512-bit SIMD state — GNU `vector_size` values and (future) autovectorized loop temporaries. It is a raw lane- and width-agnostic union (64 bytes); like `fregs`, the opcode determines the active lane view, and the instruction operand's "scale" byte carries the active width/lane count, not the register.

### Memory Segments

| Segment | Grows | Purpose |
|---------|-------|---------|
| **Text** | Upward | Bytecode instructions (`InstrWord`, 32-bit words) |
| **Data** | Upward | Global variables, string literals, static initialisers |
| **Stack** | Downward | Activation records, locals, spilled arguments |
| **Heap** | Upward | `malloc` / `free` when the VM heap allocator is active (the default; `-V`/`--no-vm-heap` routes it to the host allocator) |
| **TLS** | Upward | Per-thread copy of thread-local (`_Thread_local` / `__thread`) variables |

All segments are reserved upfront as large virtual ranges and committed in `poolsize` chunks (default 256 KiB elements, max 64 MiB elements).  This gives the VM stable base pointers while keeping resident memory modest.

A global (and TLS, see below) object is placed at its own **declared alignment**, not a hardcoded 8 bytes: an explicit `_Alignas(N)` overrides the type's own alignment, capped at `CCCC_MAX_DATA_ALIGN` (64 bytes — the widest alignment any type requests today, a 512-bit vector, #722). `_Alignas(N)` with `N > 64` is accepted but only gets 64-byte placement, a known limitation. `cc_effective_align()` (`src/codegen_emit.c`) computes this for every data-segment/TLS-template allocation site, including `cc_load_module`'s cross-module re-anchoring (#1136). This does not extend to **local** (stack-frame) variables — see [Function Frame](#function-frame)'s own note.

### Thread-Local Storage (TLS)

Variables declared `_Thread_local`, `__thread`, or `thread_local` are placed in a dedicated TLS segment:

1. **Template (`vm->tls_template`)** — built once by `gen()` alongside the data segment.  Each TLS variable is allocated a slot, rounded to its own declared alignment (see above), and its initialiser is written here.  The `LDTLS3` opcode emits the byte offset baked at compile time.
2. **Per-thread copy (`vm->current_tls_seg`)** — allocated by `posix_memalign` (aligned to `CCCC_MAX_DATA_ALIGN`, #1136 — before that, a plain `malloc`, which only guarantees the platform's default `max_align_t`) for the main thread and for each spawned `pthread_t` or `thrd_t`.  The thread inherits an `memcpy` of the template at creation time (C11 §6.2.4p4 — static initialisation).  On context switch the VM updates `vm->current_tls_seg` to point to the calling thread's copy.
3. **Access (`LDTLS3 rd, imm24`)** — loads the effective address `vm->current_tls_seg + imm24` into `rd`.  Subsequent loads/stores through that pointer are ordinary data-segment accesses.  Since both the base and the offset are now alignment-correct, this address itself carries the TLS variable's full declared alignment.

TLS variable assignment (`vm->tls_template` write) happens in `gen()` at compile time; the per-thread copy is kept consistent via the pthreads context-switch wrappers in `src/stdlib/pthread.c`.

### Calling Convention

* **Integer arguments** are placed in `REG_A0` … `REG_A7`.
* **Floating-point arguments** are placed in `FREG_A0` … `FREG_A7`.
* **Return values** come back in `REG_A0` (integer) or `FREG_A0` (float).
* **Struct returns** use a rotating pool of return buffers allocated in the data segment; the `RETBUF` opcode yields the next buffer address.
* **Struct/union arguments** are passed by address: the caller passes a pointer to its own object in the integer arg register, and the callee's own prologue (`gen_function`, `src/codegen_func.c`) copies the pointee into a fresh frame-local scratch slot (`alloc_wide_bitint_temp`'s per-function pool, the same one vectors/decimals below reuse) and rebinds the parameter's slot to point at the copy instead — true by-value semantics: a write through the parameter never aliases the caller's argument (#1078; before this fix, nothing ever copied, so a write silently mutated the caller's object). This copy only exists in a CCCC-emitted callee's own prologue — an **FFI cfunc** (a real host C function registered via `cc_register_cfunc`/`cc_register_variadic_cfunc`) receives the same by-address struct argument with no prologue of its own to copy it, so any FFI wrapper that reads a guest struct argument through that pointer must snapshot it itself before mutating anything through it. The `v*`-family stdio/syslog FFI wrappers (`src/stdlib/format_printf.c`/`format_scanf.c`/`stdio.c`/`posix_lang.c`) learned this the hard way (#1085): each takes a guest `va_list` (a plain struct, `include/stdarg.h`) by this same by-address convention and used to extract arguments straight out of the caller's own struct in place, silently advancing the caller's own `va_list` on every host. Fixed with `CCCC_VA_LOCAL` (`src/stdlib/va_ffi_helper.h`), a snapshot-before-extract macro used at all twelve wrapper sites.
* **Spilled arguments** (more than 8) are pushed onto the stack before the call.
* `FREG_A0` … `FREG_A7` share raw register indices with `REG_A0` … `REG_A7`
  (both 10-17, see `src/internal.h`), so a float value and an integer address
  can never both be live "in" the same `A`-register at once. Codegen paths
  that compute a member/variable address and then load a flonum through it
  (e.g. `gen_expr`'s `ND_VAR`/`ND_MEMBER` cases in `src/codegen_expr.c`) must use a
  separate temp register for the address rather than reusing the destination
  register.

The `ENT3` opcode builds the stack frame, copies register arguments and stack-passed fixed arguments to their callee-local parameter slots, and optionally writes a stack canary. For variadic functions, `ENT3` still reserves and spills the first 8 argument slots so `va_arg` can consume any register-passed variadic tail; variadic arguments beyond those slots remain in the caller's stack area. `LEV3` restores `bp`, checks the canary, pops the return address, and resumes at the caller.

A nested function's synthesized `__static_link` parameter is always the first param (prepended at declaration, `src/parse_decl.c`), so under the ordinary layout it lives at `bp[-1]`; `--stack-canaries`/`-3` reserves `bp[-1]` for the canary word instead and shifts every param one slot lower, to `bp[-2]` (`canary_bias`, `assign_lvar_offsets`, `src/codegen_stmt.c`, #445). A multi-hop static-link chase (a nested function reading a variable owned by an ancestor two or more levels up, `emit_static_chain_var_addr`/the sibling-call walk in `codegen_expr.c`) must apply that same canary-aware hop distance at *every* link, not just the first — `static_link_hop_bytes()` (`src/codegen_addr.c`) is the single source of truth both call sites share, after #1082 found each had independently hardcoded the no-canary distance and silently walked into the wrong slot (SIGSEGV) the moment a chain reached depth 2 under either flag.

### Tail-Call Optimisation

When codegen detects that a `return` statement's outermost expression is a direct call to a statically-known, in-VM, non-variadic, non-nested function whose return type is not a struct/union and whose argument count does not exceed 8, it emits `CALLT` instead of `CALL` followed by `LEV3`.

`CALLT` unwinds the current frame (`sp = bp`, restores saved `bp`) without consuming the return address already on the stack. The callee's subsequent `LEV3` therefore returns directly to the *original* caller, so deeply-recursive helpers and mutually-recursive function pairs execute in O(1) stack space regardless of recursion depth.

**Eligibility** (all must hold, checked at `-O1` and above):

* The call is the sole, outermost expression of the `return` — `return f() + 1` is not a tail call.
* The callee is a directly-addressed in-VM function (not FFI, not a function pointer).
* The callee is not variadic and not a nested function (nested functions require a static-link argument in `REG_A0`).
* The callee does not return a struct, union, or a wide `_BitInt` (over 64 bits) — these returns are materialised through a frame-relative scratch buffer (`RETBUF` for struct/union, a compiler-synthesized scratch slot for wide `_BitInt`), which `CALLT`'s frame reuse would invalidate.
* The call uses 8 or fewer arguments (stack-spilled arguments would fall below the unwound frame).
* No argument carries the address of one of the caller's own locals or parameters — directly (`&x`), via pointer arithmetic on a frame-local base (`buf + i`), via array/struct/union decay, or via a local whose address is already known to escape the frame (a pointer variable holding `&x`, `&x` stored into a global, etc.). `CALLT` reuses the caller's frame, so a pointer into that frame would dangle the instant the callee's own prologue or body overwrites the slot.
* The `return` statement's implicit cast to the function's own return type must be representation-preserving with respect to the callee's return type — an identity conversion (e.g. `int → int`) or one that leaves the value's register representation unchanged (e.g. `long`/pointer/enum, all read the same 64-bit register). A genuine narrowing or rounding at the return site itself (`return (unsigned char) g(x);`, `return (float) g(x);`) disqualifies the call: `CALLT` hands the callee's raw return value straight to the *original* caller, with no later point at which the narrowing/rounding could be applied.

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

More generally, a level-`n` `__builtin_return_address` walk counts only the frames actually present on the stack — it has no way to know how many tail calls were elided along the way. Each `CALLT` in the chain being walked shifts every level below it down by one, so a lookup that would have returned a real return address without TCO can instead hit the outermost sentinel (`NULL`) early, or land on a different frame's return address than a naive (non-TCO) frame count would suggest. `tests/test_builtin_return_address_callt.c` (pinned to `-O1`, `CCCC_MATRIX_SKIP`) asserts this collapsed-frame behavior directly; `tests/test_builtin_return_address_notco.c` (pinned to `-O0`) covers the same nonzero-level lookups through a helper chain with no tail calls, for comparison.

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
| `JMPI` | 1 | Indirect jump through register. Only emitted for computed goto (`goto *expr`); a register that resolves to a dynamic-symbol or FFI token target is not a supported use of computed goto and is a runtime error |
| `JZ3` | 2 | Branch if `regs[rs] == 0` |
| `JNZ3` | 2 | Branch if `regs[rs] != 0` |

### Function Frame

| Opcode | Operands | Description |
|--------|----------|-------------|
| `ENT3` | 4 | Enter function: `stack_size`, `spill_param_count`, `float_param_mask`, `f32_param_mask`. Bit 31 of the `float_param_mask`/`f32_param_mask` half-words (unused by real params, which are capped at 8) each carry a lazy frame-epoch push flag patched in by codegen after the function body is generated — see "Dangling pointers" below. |
| `LEV3` | 0 | Leave function: restore frame, check canary, return to caller |
| `ADJ` | 2 | Adjust stack pointer by signed immediate |
| `PSH3` | 1 | Push `regs[rs]` onto the stack |
| `POP3` | 1 | Pop stack into `regs[rd]` |

Local/parameter offsets (`assign_stack_offsets`, `src/codegen_stmt.c`) are word slots counted down from `bp`, and only get 8-byte (one-slot) alignment — unlike the data segment/TLS template above, a local's declared alignment `> 8` (an `_Alignas(16)+` local, a wide `__int128`/`_BitInt`, or a 16/32/64-byte vector) is **not honoured** (#1136, tracked as a follow-up). `bp` itself cannot simply be aligned at `ENT3`: the calling convention pins `bp[+1]` to the return address and `bp[+2..]` to stack-passed arguments, so its absolute parity varies from call to call. A real fix needs a hidden aligned-base slot computed once at function entry with indirect addressing for the over-aligned local — out of scope for the allocator-level fix the rest of this section describes.

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

#### Wide _BitInt bitfields (#1125)

A bitfield whose *declared type* is itself a wide `_BitInt` (`T f : W;`,
`T`'s width over 64 bits) is not accessed through either the ordinary
scalar-register shift/mask idiom (the value doesn't fit one register) or a
whole-container load/store CALLF (the *bit packing* within the struct is
compact regardless of container width — `struct { _BitInt(256) f : 193; }`
packs `f` into bits `[0,193)`, though its container spans 32 bytes; the
struct itself is 32 bytes, not just enough to cover those bits, since #1127
made a named bitfield member's declared type also set the struct's own
size/alignment floor to its storage unit — so nothing guarantees the full
container is even present *at that offset*, only that the struct is at least
as large as the container). Instead two dedicated runtime helpers
(`src/stdlib/wide_bitint.c`, registered via `register_wide_bitint_functions`
alongside the `__cccc_bitint_*` family above) walk only the exact bytes the
field spans, one byte at a time:

- **`__cccc_bitfield_extract(dst, base, bit_off, width, words, is_signed)`** —
  reads `width` bits starting `bit_off` bits into `base` into the low bits
  of the `words`-word buffer at `dst`, then sign- or zero-extends up through
  the rest of `dst` (per `is_signed`) so the result is a full value of the
  bitfield's *declared* type, not just its `bit_width`.
- **`__cccc_bitfield_insert(base, src, bit_off, width)`** — writes the low
  `width` bits of `src` into the bitfield at `bit_off`, leaving every other
  bit in the spanned bytes untouched (the RMW proper — a neighbouring
  bitfield packed into the same bytes must survive).

Codegen reaches these from two places: the ordinary member-read path
(`ND_MEMBER`, `src/codegen_expr.c`) for `extract`, and — since a bitfield's
assignment type is its container type, which would otherwise route through
the generic struct/union/wide-`_BitInt` `MCPY` fast path in `ND_ASSIGN` —
a dedicated bitfield-aware arm ahead of that fast path for `insert`. The
same `insert` helper is also called directly (host-side, not via CALLF —
this runs at parse time, not inside the VM) from the global-initializer RMW
in `write_gvar_data` (`src/parse_init.c`), replacing that function's former
own word-array loop, which had the same past-the-object overwrite risk.

### Data Movement

| Opcode | Operands | Description |
|--------|----------|-------------|
| `LI3` | 3 | Load 64-bit immediate into `regs[rd]` |
| `LDA3` | 3 | `regs[rd] = data_seg + byte_offset` |
| `LTA3` | 3 | `regs[rd] = text_seg + byte_offset` (stores raw offset) |
| `LEA3` | 3 | `regs[rd] = bp + offset` (local variable address). Format: `[LEA3][rd:8\|LEA3_NO_RECORD:1\|unused:55][offset:i64]`. When `CCCC_DANGLING_DETECT` is active, tags the result with the current frame's liveness epoch in `vm->stack_ptr_epochs` (#673) — unless `LEA3_NO_RECORD` is set, in which case the tag is skipped (#676). Codegen sets the flag only when a local's address is proven never to escape its creating frame (compiler-internal bookkeeping addresses, or a user local whose escape analysis found no call-argument/return/pointer-store use); see `man/SAFETY.md`. |
| `STKTAG` | 5 | Tags `[bp+offset, bp+offset+size)` with the current frame's liveness epoch in `vm->stack_intervals`, for interior dangling-pointer resolution (#675) and `DYNOBJSZ` stack-buffer sizing (#648). Format: `[STKTAG][unused:32][offset:i64][size:i64]`. Emitted immediately after the `LEA3` base of an *escaping* array/struct local or parameter (never for scalars, which are already covered exactly by `stack_ptr_epochs`). No-op unless `stack_extents_enabled()` is true — `CCCC_DANGLING_DETECT`, or the program contains a `DYNOBJSZ` opcode (`vm->dynobjsz_present`, set by a one-time incremental scan of the text segment before the first `cc_run_at`, so it survives a `.c4` save/reload) — *and* the current frame is one that pushed an epoch (see the lazy-activation note under "Dangling pointers" below; a function whose body emits `STKTAG` always pushes one). See `man/SAFETY.md`. |
| `RETADDR` | 3 | `regs[rd] = return address of the nth caller frame`. Walks the saved-bp chain `level` steps from `vm->bp`; each step loads `frame[0]` (saved old-bp). Bounds-checks each frame against the live stack (`vm->sp ≤ frame < vm->initial_sp`). On success, sets `regs[rd] = frame[+1]` (the `Pc` return address stored by `CALL`/`CALLI`). Returns `NULL` (0) past the outermost frame. Format: `[RETADDR][rd:8\|unused:56][level:i64]`. Lowered from `__builtin_return_address(n)`. The returned value (a `Pc`/`uint32_t` offset) can be passed to `__builtin_pc_function_name` or `__builtin_pc_source_location` for symbolization; see the [Source Map API](TOOLING.md#source-map-api). |
| `DYNOBJSZ` | 3 | `regs[rd] = runtime byte-size remaining at regs[rs]`. First tries the **heap** path: looks up the containing allocation in `vm->sorted_allocs` (binary search for the largest tracked base address ≤ `ptr`) for pointers into the VM heap (`heap_seg ≤ ptr < heap_end` — `malloc`/`calloc`/`realloc`/`reallocarray` require the VM heap (i.e. not `-V`/`--no-vm-heap`); `alloca`/VLA, which lower to the `ALCA` opcode, always qualify), validates the `AllocHeader` magic (`0xDEADBEEF`) and `freed` flag, and returns `requested_size - offset`. Handles both base pointers (offset 0) and interior pointers (`p + k`) uniformly. On a heap miss, tries the **stack** path (#648): stabs `vm->stack_intervals` (`stack_interval_stab`, same max-epoch resolution `CHKP3` uses, #675) for an escaping fixed-size stack array/struct/union tagged by `STKTAG`, and returns `hi - ptr` only if the matched interval's epoch is still in `vm->live_epochs` — a match whose epoch has retired (frame already returned) is dangling, not resolvable, and falls through. Returns the conservative fallback — `(size_t)-1` (type 0/1) or `0` (type 2/3) — if both paths miss (non-heap and non-stack-tracked, freed, out-of-bounds `requested_size` offset, or dangling). Format: `[DYNOBJSZ][rd:8\|rs:8\|unused:48][type:i64]`. Lowered from `__builtin_dynamic_object_size(ptr, type)` when the pointer's size cannot be resolved at compile time. Using this opcode at all sets `vm->dynobjsz_present`, activating `STKTAG`/epoch bookkeeping independently of `CCCC_DANGLING_DETECT` (see `STKTAG` above) — but only for the functions that actually need it: see the lazy per-function activation note under "Dangling pointers" below. |
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

`atomic_thread_fence`/`atomic_signal_fence` (#1188) get no opcode at all — they lower to `ND_FENCE`, which just evaluates the `order` argument (for its side effects) and emits nothing. The GIL is held for the whole `vm_eval` and released only at explicit blocking cfunc points, never between bytecode instructions, so no cross-thread reordering of guest memory accesses is ever observable under the VM; guest signal handlers dispatch at safe points (`cccc_call_guest_callback`) rather than asynchronously, so `atomic_signal_fence` needs no barrier here either. `-c=native` has no GIL and gets a real `__atomic_thread_fence`/`__atomic_signal_fence` instead.

### Floating-Point Operations

All `F*` opcodes operate on `fregs[]`.  Comparisons write a boolean into an integer register.

| Opcode | Description |
|--------|-------------|
| `FADD3` | `fregs[rd] = fregs[rs1] + fregs[rs2]` (f64) |
| `FSUB3` | `fregs[rd] = fregs[rs1] - fregs[rs2]` (f64) |
| `FMUL3` | `fregs[rd] = fregs[rs1] * fregs[rs2]` (f64) |
| `FDIV3` | `fregs[rd] = fregs[rs1] / fregs[rs2]` (f64; IEEE-754 semantics -- finite/0.0 is a correctly-signed infinity, 0.0/0.0 is NaN, neither is UB; traps only under the opt-in `--trap-fp-divzero`) |
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
| `I2F3` | `fregs[rd] = (double)regs[rs]` (signed source) |
| `F2I3` | `regs[rd] = (long long)fregs[rs]`, saturating: NaN → 0, ≥2⁶³ or +Inf → `LLONG_MAX`, <-2⁶³ or -Inf → `LLONG_MIN`, `FE_INVALID` raised on all three |
| `U2F3` | `fregs[rd] = (double)(unsigned long long)regs[rs]`; selected instead of `I2F3` whenever the cast source is an unsigned 64-bit integer, since `I2F3` reinterprets the register as signed |
| `F2U3` | `regs[rd] = (unsigned long long)fregs[rs]`, saturating: NaN → 0, ≥2⁶⁴ or +Inf → `ULLONG_MAX`, ≤-1 or -Inf → `0`, `FE_INVALID` raised on all three; a value in (-1, 0), e.g. `-0.5`, truncates to `0` with **no** exception, per well-defined `(unsigned long long)` cast semantics. Selected instead of `F2I3` whenever the cast destination is an unsigned 64-bit integer |
| `I2F3_F32` | `fregs[rd] = (float)regs[rs]` (signed source) |
| `F2I3_F32` | `regs[rd] = (long long)(float)fregs[rs]`, same saturating rules as `F2I3` |
| `U2F3_F32` | `fregs[rd] = (float)(unsigned long long)regs[rs]`, same selection rule as `U2F3` |
| `F2U3_F32` | `regs[rd] = (unsigned long long)(float)fregs[rs]`, same saturating rules as `F2U3` |
| `FR2R` | Bit-pattern transfer `freg → reg` (f64) |
| `R2FR` | Bit-pattern transfer `reg → freg` (f64) |
| `FR2R_F32` | Bit-pattern transfer `freg → reg` (f32) |
| `R2FR_F32` | Bit-pattern transfer `reg → freg` (f32) |

### SIMD / Vector Operations

Vector opcodes operating on `vregs[]` (see [Register File](#register-file)). Introduced for GNU `__attribute__((vector_size(N)))` support (`COVERAGE.md`) and reserved as the shared substrate for a future interpreter autovectorizer. **128-, 256-, and 512-bit vectors are supported** (16/32/64-byte total size) — e.g. `v4f32`/`v8f32`/`v16f32`, `v2f64`/`v4f64`/`v8f64`, and the corresponding integer lane layouts at each width; any other width is rejected with a diagnostic. As with `F*` vs `F*_F32`, the opcode — not the register — carries the active lane *type*; the instruction operand's "scale" byte carries the active *width* (`VLDR`/`VSTR`: byte count) or *lane count* (every other op), so one opcode per element family now serves all three widths (opcode names no longer carry an `X2`/`X4`/... lane-count suffix). `VLDR`/`VSTR` move the operand-carried byte count with a byte-copy (no alignment fault on unaligned addresses); arithmetic reads/writes the lane view named by the opcode, looping up to the operand-carried lane count.

| Opcode | Description |
|--------|-------------|
| `VLDR` | `vregs[rd] = *(byte[width])regs[rs]`, `width` from the operand's scale byte (16/32/64) |
| `VSTR` | `*(byte[width])regs[rs] = vregs[rd]` |
| `VMOV3` | `vregs[rd] = vregs[rs1]` (full-register copy, all 64 bytes) |
| `VSPLAT_F64` / `VSPLAT_F32` | `vregs[rd].lane[i] = fregs[rs1]` for `i` in `[0, count)` (scalar broadcast from a float register); `count` from the operand |
| `VSPLAT_I64` / `VSPLAT_I32` / `VSPLAT_I16` / `VSPLAT_I8` | Same, broadcasting from an integer register |
| `VEXTRACT_F64` / `VEXTRACT_F32` | `fregs[rd] = vregs[rs1].lane[imm]` — lane index is an immediate in the RRRS "scale" field |
| `VEXTRACT_I64` / `VEXTRACT_I32` / `VEXTRACT_I16` / `VEXTRACT_I8` | Same, into an integer register |
| `VINSERT_F64` / `VINSERT_F32` | `vregs[rd].lane[imm] = fregs[rs1]` |
| `VINSERT_I64` / `VINSERT_I32` / `VINSERT_I16` / `VINSERT_I8` | Same, from an integer register |
| `VADD_F64`, `VSUB_F64`, `VMUL_F64`, `VDIV_F64`, `VNEG_F64` | Per-lane `+ - * /` and unary negate, f64 lanes, `count` (2/4/8) from the operand |
| `VADD_F32`, `VSUB_F32`, `VMUL_F32`, `VDIV_F32`, `VNEG_F32` | Same, f32 lanes (`count` 4/8/16) |
| `VADD_I64`, `VSUB_I64`, `VMUL_I64`, `VDIV_I64`, `VMOD_I64`, `VNEG_I64` | Per-lane `+ - * / %` and unary negate, i64 lanes (`count` 2/4/8). `VDIV`/`VMOD` trap on a zero divisor or `INT64_MIN/-1` overflow (see below) |
| `VADD_I32`, `VSUB_I32`, `VMUL_I32`, `VDIV_I32`, `VMOD_I32`, `VNEG_I32` | Same, i32 lanes (`count` 4/8/16) |
| `VADD_I16`, `VSUB_I16`, `VMUL_I16`, `VDIV_I16`, `VMOD_I16`, `VNEG_I16` | Same, i16 lanes (`count` 8/16/32) |
| `VADD_I8`, `VSUB_I8`, `VMUL_I8`, `VDIV_I8`, `VMOD_I8`, `VNEG_I8` | Same, i8 lanes (`count` 16/32/64) |
| `VAND`, `VOR`, `VXOR`, `VNOT` | Bitwise `& \| ^ ~`, width-agnostic (looped over `i64[0..words)`, `words` = byte width / 8, from the operand) — integer lanes only, no per-lane-family variant needed |
| `VCEQ_*`, `VCNE_*`, `VCLT_*`, `VCLE_*` (one family per lane type, `F64`/`F32`/`I64`/`I32`/`I16`/`I8`) | Per-lane comparison: writes all-ones (`-1`) if true, `0` if false, into a same-width **signed** integer lane (GCC semantics); `count` from the operand |
| `VCLTU_*`, `VCLEU_*` (`I64`/`I32`/`I16`/`I8` only) | Unsigned-view ordered comparison — selected by lane element signedness at codegen; `>`/`>=` are parsed as swapped-operand `<`/`<=`, so no separate opcodes exist |
| `VSEL_8`, `VSEL_16`, `VSEL_32`, `VSEL_64` | GNU vector `?:` select, by lane byte width: `rd[i] = cond[i] ? then[i] : rd[i]` for `i` in `[0, count)` — `rd` is pre-loaded by codegen with the else-arm, so this is a **read-modify-write** on `rd` (like `VINSERT_*`), not a pure write |
| `VCVT_I32_F32`, `VCVT_F32_I32`, `VCVT_I64_F64`, `VCVT_F64_I64` | `__builtin_convertvector` lane conversion (truncating toward zero for float→int, C cast semantics); `count` from the operand |

**Operand encoding (#722).** Every vector opcode is `RRRS`-encoded (`ENCODE_RRRS`/`DECODE_RRRS`, `internal.h`) — `[rd:8|rs1:8|rs2:8|scale:8]`. Two-register ops (`VLDR`/`VSTR`/`VSPLAT_*`/`VNEG_*`/`VNOT`/`VCVT_*`) leave `rs2` unread and use `scale` for the byte width (`VLDR`/`VSTR`) or lane count (everything else), the same way `VEXTRACT_*`/`VINSERT_*` already ignore `rs2` and use `scale` for the lane index. This keeps a vector value flowing through a **single** `vregs[]` index end to end (the same contract `gen_expr`'s `dest_reg` and the temp-register allocator use for every other value kind) — widening to 256/512-bit only grows the register and adds an operand field, it does not tile multiple registers per value.

**Per-lane integer division/modulo trapping (`VDIV_I*`/`VMOD_I*`).** Mirrors scalar `DIVC`'s trapping policy (not `DIV3`'s non-trapping `LLONG_MIN` return) — a zero divisor in *any* lane prints a `DIVISION BY ZERO` diagnostic naming the lane and aborts; `MIN/-1` overflow in any lane traps the same way for `VDIV_*` (`VMOD_*` returns `0` for that lane instead, matching `a % -1 == 0` for all `a`). This makes vector integer division/modulo deliberately stricter than default-build scalar `/`.

**Frontend lowering (codegen.c `gen_vector_expr`).** A vector local/global lives in a memory slot sized to its own byte width (16/32/64), exactly like a small struct — no frame-layout changes were needed beyond sizing the slot correctly (`assign_stack_offsets`/`var_stack_slots`). The *value* of a vector expression flows through a `vregs[]` index in `dest_reg`, mirroring how float-typed expressions flow through `fregs[]` even though float locals also live in memory: `VLDR`/`VSTR` move between the slot and a vreg around each operation, carrying the value's byte width in the operand. `v[i]` subscript is **not** lowered through `VEXTRACT`/`VINSERT` — it is intercepted at parse time (before array-decay pointer arithmetic, which would misinterpret the vector's `base` field) and rewritten to `*(elem_ty*)(&v + i)`, reusing ordinary scalar load/store and supporting a runtime-variable index for free. `VEXTRACT`/`VINSERT` remain reserved for the autovectorizer (horizontal reductions), though `__builtin_shuffle`'s lowering (both the constant- and runtime-mask forms) also reuses this same subscript machinery (see below) rather than `VEXTRACT`/`VINSERT` directly.

**`__builtin_shuffle` (constant-mask and runtime-mask forms).** CCCC accepts `__builtin_shuffle(v, {i0,...,iN-1})` (1-vector permute) or `__builtin_shuffle(v1, v2, {i0,...,iN-1})` (2-vector blend) with a bare, compile-time-constant brace list mask (CCCC's original constant-mask form, closer to clang's `__builtin_shufflevector` than the syntax below), as well as `__builtin_shuffle(v, mask)` / `__builtin_shuffle(v1, v2, mask)` with `mask` an ordinary integer-vector expression — a named local, function parameter, or runtime-computed value — matching upstream GCC's general vector-typed mask argument. A `(vTYPE){...}` compound literal mask is rejected as ambiguous with the two-vector form's second argument (both start with `(`); a bare `{` unambiguously marks a constant mask, so the parser distinguishes the forms with one token of lookahead there, and (since the 1-vector runtime form and the 2-vector form both start with a non-brace expression) by checking whether a further `,` follows the second parsed argument.

No new opcode for either form: the parser materializes the source vector(s) (and, for the runtime form, the mask) into hidden locals (so side effects run once), then builds a chain of per-lane scalar reads/writes through the same vector-subscript lvalue lowering `v[i]` uses — this per-lane approach is width-agnostic and works unchanged at 256/512-bit, and works with a runtime index exactly as it does with a constant one. The runtime mask must be an integer vector with the same lane count and element byte width as the shuffled vector (validated at parse time, rejected with a diagnostic otherwise). The two forms diverge on out-of-range indices: the constant form range-checks every index at compile time and rejects an out-of-range one outright, while the runtime form can't be checked until run time, so each index is taken modulo the lane count (1-vector) or twice the lane count (2-vector) before use — matching GCC's documented wraparound semantics.

**Optimizer interaction.** The bytecode optimizer's copy-propagation and dead-code passes (`optimize.c`, `-O3`+) generically decode instruction operand bytes as living in either the int or float register file. Vector opcodes introduce a third namespace (`vregs[]`) and mix it with real `freg`/`greg` operands in opcode-specific ways that model can't express safely — `op_has_vector_operand()` in `optimize.c` treats every vector opcode as opaque to those passes (never substituted, conservatively invalidated), at the cost of a missed optimization opportunity but never a miscompile. See `man/OPTIMIZATION.md`.

**By-value function args/returns (#714, widened by #722).** Reuses the struct-by-value call ABI rather than a register (FReg/GReg) convention, since a vector doesn't fit an 8-byte arg slot. Argument: the caller materializes the vector's value (a `vregs[]` register) into a fresh frame scratch slot sized to the argument's own byte width via `VSTR`, then passes that slot's address in the integer arg register — a caller-side copy, unlike a struct-by-value arg's own copy, which happens in the *callee's* prologue instead (#1078); both end up giving true by-value semantics, just on opposite sides of the call. Return: the callee requests a buffer from the `RETBUF` rotating pool (1024 bytes, comfortably covering the 64-byte maximum), `VSTR`s the result into it, and returns the buffer address; the caller `VLDR`s out of it. The callee reads a vector parameter the same way a struct parameter is read — the param's stack slot holds a pointer to the value, not the value itself (`gen_addr`'s param-slot dereference). Not supported through the native FFI marshalling path (int/float-only slot classification), a variadic `...` parameter (register spilling for `va_arg` is int/float-only), or a GNU/Apple block invocation (no by-memory ABI for aggregates at all) — each rejected at compile time. Tail-call elimination (`can_emit_tail_call`) excludes any vector-returning call and any call with a vector argument, since `CALLT` reuses the caller's frame and both the RETBUF buffer and the argument scratch slot are frame-local.

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

These opcodes implement the C standard library heap (the default; `-V`/`--no-vm-heap` routes `malloc`/`free`/etc. to the host allocator instead).

| Opcode | Arguments | Description |
|--------|-----------|-------------|
| `MALC` | `REG_A0` = size | Allocate; pointer returned in `REG_A0` |
| `ALCA` | `REG_A0` = size | Allocate a bare `__builtin_alloca` call's storage (automatic, **frame**-scoped — lives until the function returns); pointer returned in `REG_A0`. Identical register/ABI shape and `AllocHeader`/`sorted_allocs` tracking to `MALC` (so `CHKB`/`CHKBN`/`CHKP3`/`DYNOBJSZ` see it exactly like any other allocation) but the resulting `AllocHeader.kind = ALLOC_KIND_ALLOCA` excludes it from leak-detection's `AllocRecord` list (#979) — it is never meant to be freed by the guest program. Reclaimed only at frame exit (`LEV3`, never `HREL`) — see the Heap Reclamation section below |
| `ALCV` | `REG_A0` = size | Allocate a VLA's backing storage (automatic, **block**-scoped — lives until the end of the declaring block, C11 6.8.3); pointer returned in `REG_A0`. Identical shape to `ALCA`, tagged `AllocHeader.kind = ALLOC_KIND_FRAME` instead. Split from `ALCA` (#981) specifically because the two have different lifetimes: reusing one `AllocKind` for both would let a block-exit reclaim (`HREL`) sweep a still-live bare-`alloca`'d block declared in the same block, corrupting it the moment the address was reused. `Node.is_vla_alloca_call` (set only by `new_alloca()`, `src/parse_init.c` — reached only through a VLA declaration's lowering, `ND_ASSIGN(ND_VLA_PTR, alloca(...))`) is what tells codegen which of `ALCA`/`ALCV` to emit for a given `alloca(...)` call |
| `ALCB` | `REG_A0` = size | Allocate a `__block` variable's heap box; pointer returned in `REG_A0`. Same shape as `ALCA`/`ALCV`, tagged `AllocHeader.kind = ALLOC_KIND_BLOCK_BOX` instead — split into its own opcode (#981's prerequisite) so heap reclamation targeting `ALLOC_KIND_FRAME`/`ALLOC_KIND_ALLOCA` storage can never sweep a `__block` box, which `Block_copy` is expected to let legitimately outlive its declaring frame. Before this split, `ALCA` alone backed both under a single `AllocHeader.is_internal` bool |
| `HMRK` | `depth` (i32 immediate) | Heap-reclamation watermark (#981): push `{vm->bp, depth, vm->heap_ptr}` onto `vm->heap_marks`, truncating any stale entry for this `(bp, depth)` first (loop re-entry, backward `goto`). Emitted at the start of any block that declares a VLA, only when reclamation could apply (see the Heap Reclamation section below). Identical single-word-immediate shape to `SCOPEIN` |
| `HREL` | `depth` (i32 immediate) | The matching block-exit release for `HMRK`: find this `(bp, depth)`'s mark and rewind `vm->heap_ptr` to it, sweeping only `ALLOC_KIND_FRAME` (VLA) storage — never a bare alloca or a `__block` box. Emitted at the natural (fall-through) exit of the same block `HMRK` was emitted for; a `break`/`continue`/`goto`/`return` out of the block skips it, which only forfeits that block's reclamation (never over-reclaims) |
| `MFRE` | `REG_A0` = ptr | Free pointer (detects double-free) |
| `MCPY` | `REG_A0` = dest, `REG_A1` = src, `REG_A2` = count | `memcpy` |
| `MSET` | `REG_A0` = dest, `REG_A2` = count | `memset` to 0; backs `ND_MEMZERO` (pre-zero for partial aggregate initialisers) |
| `REALC` | `REG_A0` = ptr, `REG_A1` = new_size | `realloc` |
| `CALC` | `REG_A0` = nmemb, `REG_A1` = size | `calloc` |
| `REALCA` | `REG_A0` = ptr, `REG_A1` = nmemb, `REG_A2` = size | `reallocarray`; overflow-checked `nmemb * size` delegates to `REALC`'s logic, returning `NULL` (original allocation untouched) on overflow rather than truncating (#699) |
| `MALCA` | `REG_A0` = size, `REG_A1` = alignment | `aligned_alloc`; pointer returned in `REG_A0` (`NULL` if `alignment` isn't a power of two) |
| `PMEMA` | `REG_A0` = memptr, `REG_A1` = alignment, `REG_A2` = size | `posix_memalign`; status (`0`/`EINVAL`/`ENOMEM`) returned in `REG_A0`, allocated pointer written through `memptr` |

`MALC`/`CALC`/`REALC` allocate with the default 8-byte alignment; `MALCA`/`PMEMA`
(#668) pad the bump pointer *before* the `AllocHeader` so the returned user
pointer lands on the requested alignment, while every consumer that recovers
the header via `((AllocHeader*)ptr) - 1` (`MFRE`, `REALC`, `DYNOBJSZ`, `CHKB`,
`CHKBN`, `CHKP3`, `CHKT3`) keeps working unmodified. Padding is only ever added
upward and is never reclaimed, consistent with the bump allocator having no
free-list reuse. Both opcodes share `MALC`'s canary/poisoning/leak-detection/
tagging tail via a common `vm_heap_bump_alloc_ex` helper, so `aligned_alloc` and
`posix_memalign` allocations get the same heap safety coverage as `malloc`.
`ALCA`/`ALCV`/`ALCB` share the same helper with `AllocHeader.kind` set to
`ALLOC_KIND_ALLOCA`/`ALLOC_KIND_FRAME`/`ALLOC_KIND_BLOCK_BOX` respectively —
the sole difference from `MALC` (`ALLOC_KIND_USER`) (#979/#981).

#### Heap Reclamation (#981)

The VM heap is a pure bump allocator — `heap_ptr` only ever advances, and
`MFRE` never moves it back (it only sets `AllocHeader.freed`). `HMRK`/`HREL`
(block exit) and an equivalent check in `LEV3` (frame exit) rewind it back
down for `alloca`/VLA storage specifically, when doing so is provably safe:

- **Gate.** Reclamation requires `vm->heap_reclaim_enabled`, computed once
  per top-level call (`cc_run_at_regs`, right after the `DYNOBJSZ`
  text-segment scan resolves `dynobjsz_present`) as
  `cc_heap_reclaim_flags_ok(vm->flags) && !vm->dynobjsz_present`.
  `cc_heap_reclaim_flags_ok` (`src/cccc.h`) requires every address-keyed
  safety feature to be off: bounds checks, UAF/dangling-pointer detection,
  memory tagging (`CCCC_POINTER_CHECKS`), type checks, uninitialized-read
  detection, leak detection, and heap canaries — a reclaimed address can be
  handed out again by a later allocation, and every one of those features
  relies on that never happening. True at `-0`; false at `-1`/`-2`/`-3`, or
  under any explicit combination of those flags. `DYNOBJSZ` has the
  identical hazard (it resolves interior pointers through `sorted_allocs`
  regardless of which flags are set) but isn't resolvable until after a
  full text-segment scan, which can't run until codegen has finished — so
  codegen.c uses `cc_heap_reclaim_flags_ok` alone (the flags-only, "is this
  even worth emitting" half) to decide whether to emit `HMRK`/`HREL` at
  all, while every opcode handler re-checks the complete
  `vm->heap_reclaim_enabled` at runtime and no-ops if it's false.
- **Threads.** Reclamation is additionally gated on `vm->thread_records ==
  NULL` at every check site — the heap is a single VM-wide arena, not
  per-thread (unlike `frame_epochs`/`stack_intervals`), so once any pthread
  has been created, two threads' allocations can interleave in it and a
  per-frame LIFO rewind on one thread could sweep another thread's still-
  live VLA. Reclamation is simply never attempted once a thread exists —
  a documented residual, not a partial/unsound attempt.
- **Mechanism.** `heap_rewind_to` (`src/ops.c`) scans `vm->sorted_allocs`
  backward from the top while each entry's base address is at or above the
  target watermark — this is exactly the contiguous suffix of allocations
  made since the mark was taken, since the bump allocator's append order
  is address order. It stops at the first entry whose `AllocKind` isn't
  sweepable for the caller (`HREL` sweeps only `ALLOC_KIND_FRAME`; `LEV3`
  and a guest `longjmp` sweep both `ALLOC_KIND_FRAME` and
  `ALLOC_KIND_ALLOCA`) — a genuine `malloc` (`ALLOC_KIND_USER`) or a
  `__block` box (`ALLOC_KIND_BLOCK_BOX`) always pins the bump pointer at
  its own end, giving a correct *partial* rewind rather than refusing the
  whole watermark. `sorted_allocs` is truncated to match, which is what
  keeps its "append order is address order" invariant true even though
  addresses can now be reused (see its own comment, `src/cccc.h`).
- **CALLT / longjmp.** A tail call (`CALLT`) reuses the caller's frame
  before the caller's own `LEV3` would run, and can carry a heap-resident
  VLA/alloca pointer as one of its own arguments — so `CALLT` only drops
  the caller's `heap_marks` entries (never rewinding) rather than
  reclaiming, leaving that storage to live on as it does today. A guest
  `longjmp` (which skips every abandoned frame's `LEV3`) and the host-level
  longjmp path used by a failed `__builtin_assert` (`testing.c`) both do
  reclaim, mirroring `frame_epoch_truncate_to`'s exact monotonic-address
  argument for discarding every entry belonging to a frame deeper than the
  target.

### Safety Opcodes

These are emitted by the compiler when the corresponding safety flag is set.  At runtime they are no-ops if the flag is cleared, so uninstrumented bytecode runs at full speed.

| Opcode | Description | Controlled by |
|--------|-------------|---------------|
| `CHKB` | Array bounds check on `base + scaled_offset` (`p + n`, and via subscript desugaring `*(a+i)`, `a[i]` too), resolving `base` (exact or interior pointer) via `vm->sorted_allocs`. **Formation-time** check only (#983): a result exactly one past the allocation's end (`eff == size`) is allowed, since forming such a pointer is legal C — see `CHKD` below for the dereference-time check that catches an actual out-of-bounds access | `CCCC_BOUNDS_CHECKS` |
| `CHKBN` | Array bounds check on `base - scaled_offset` (`p - n`) — `CHKB`'s subtracting-form sibling (#982); same operand shape, resolution, and one-past-the-end formation allowance (#983), opposite sign. Never emitted for a pointer *difference* (`&a - &b`, result type `ptrdiff_t`/`long`) — `scaled_offset` there is `new_sub`'s ptr-ptr divide result, not a scaled byte offset, so bounds-checking it would compare an unrelated value against the allocation's size | `CCCC_BOUNDS_CHECKS` |
| `CHKI` | Uninitialised-variable read check (`bp+offset`) | `CCCC_UNINIT_DETECTION` |
| `MARKI` | Mark variable at `bp+offset` as initialised | `CCCC_UNINIT_DETECTION` |
| `CHKPA` | Validate pointer arithmetic against provenance | `CCCC_INVALID_ARITH` + `CCCC_PROVENANCE_TRACK` |
| `MARKP` | Record pointer provenance (`origin`, `base`, `size`) | `CCCC_PROVENANCE_TRACK` |
| `CHKP3` | Pointer validity (NULL, UAF, heap range, dangling stack deref), resolving the pointer (exact or interior) via `vm->sorted_allocs` | `CCCC_POINTER_CHECKS` |
| `CHKA3` | Pointer alignment check | `CCCC_ALIGNMENT_CHECKS` |
| `CHKT3` | Heap type-tag check on dereference (effective-type model), resolving the base pointer via `vm->sorted_allocs` | `CCCC_TYPE_CHECKS` |
| `CHKR` | Checked-pointer range check (`[[cccc::single/array/ntarray]]`): traps unless `addr != 0 && lo <= addr && addr + size <= hi` | `CCCC_CHECKED_BOUNDS` |
| `CHKRO` | Optional checked-pointer range check (#942): identical to `CHKR`, except the sentinel `lo == (char*)-1 && hi == (char*)0` is a no-op instead of a violation | `CCCC_CHECKED_BOUNDS` |
| `CHKNT` | Checked-pointer null-terminator guard for `[[cccc::ntarray]]` (`count()`/`byte_count()`/`bounds()`): traps a store of a non-zero value into the widened terminator slot (`addr == hi - elem_size && val != 0`) | `CCCC_CHECKED_BOUNDS` |
| `CHKNTZ` | Checked-pointer null-terminator guard (#939) for the memcpy-lowered `ntarray` pointees `CHKNT` cannot reach (struct/union, wide `_BitInt`/`_Decimal`): traps unless every byte at the source address is zero, checked before the `MCPY` it guards | `CCCC_CHECKED_BOUNDS` |
| `CHKAB` | Checked-pointer assignment-time bounds implication (#944, Checked C's `_Assume_bounds_cast` direction): traps unless `slo <= val && val <= shi` | `CCCC_CHECKED_BOUNDS` |
| `CHKD` | **Dereference-time** bounds check (#983), `CHKB`/`CHKBN`'s formation-time counterpart: traps unless `off + access_size <= header->size`, resolving the dereferenced address via `vm->sorted_allocs` the same way `CHKB`/`CHKP3` do. Emitted at every load/store site that reaches memory: scalar loads/stores, struct/union/wide-`_BitInt`/`_Decimal` `MCPY` copies, vector `VLDR`/`VSTR`, and (#985) the atomic ops `ALDR`/`ASTR`/`AXCHG`/`ACAS` (`ACAS` gets two — object pointer and `expected` pointer). Silently passes for a NULL pointer (`CHKP3`'s job) or a non-heap address (no bound known, same limitation `CHKB`/`CHKBN` already have) | `CCCC_BOUNDS_CHECKS` |

`CHKB`/`CHKBN` and `CHKP3` resolve their pointer's containing allocation via
the same `sorted_allocs_find` binary search `DYNOBJSZ` uses (#647): the
largest tracked base address ≤ the pointer. This lets both checks recognise
**interior pointers** (`p = q + k`) into a heap allocation, not only exact
base pointers — an out-of-bounds index or a use-after-free reached through
an interior pointer is now caught (#650). `CHKB`/`CHKBN`'s bound is
`AllocHeader.size` (the aligned/usable size); a negative effective offset is
only rejected once it steps before the *resolved allocation's* start, so
`p[-1]` on an interior pointer that stays within the allocation is valid.
Note the gaps this leaves: `AllocHeader` only exists for VM-heap
allocations, so **`CHKB`/`CHKBN` have no upper bound at all for a stack or
global array** — `CHKB` (ADD form) can only reject a literal negative
offset there (`a[-1]`); `CHKBN` (SUB form) rejects nothing at all for a
non-heap base, since a `p - n` there isn't necessarily an array's own start
the way a bare subscript's base is (see the comment on `chkb_common`,
`src/ops.c`).

**Formation vs. dereference (#983).** Forming a one-past-the-end pointer on
heap memory (`p + n` where `n == size`) is legal C — only dereferencing it
is undefined. `CHKB`/`CHKBN` allow it (`eff == size` is not an error) since
they run at pointer *formation*; `CHKD` is a separate check that runs at
every *dereference* site and traps on exactly this case, so `int *e = p + 4;`
(forming) is clean while `p[4] = 1;`/`*e = 1;` (dereferencing) still traps.
This split exists because `CHKB`/`CHKBN` used to be the *only* runtime check
on a subscript at all (`a[i]` desugars to `*(a+i)`), so simply relaxing
their comparison to `>` would have silently stopped a genuine out-of-bounds
access (`a[size]`) from being caught. The resolution relies on
`heap_alloc_for_ptr`'s existing inclusive upper bound (`off > h->size` is
its rejection condition, not `off >= h->size`), which is what lets `CHKD`
reliably resolve the allocation an exactly-one-past address belongs to.
`CHKD` has the same "no bound known" gap as `CHKB`/`CHKBN` for a stack or
global array (no `AllocHeader` to resolve against). It is also emitted
ahead of the atomic ops (`ALDR`/`ASTR`/`AXCHG`/`ACAS`, #985) — this was
initially deferred because `AXCHG`/`ACAS`'s own operand words carry the
#497 register-aliasing hazard, but a standalone `CHKD` instruction emitted
ahead of them never touches that word, so it doesn't reopen it. `CHKR`
(#770/#482-484) closes the upper-bound
gap generally: its
`[lo, hi)` bounds come from the checked pointer's declaration
(`count()`/`byte_count()`/`bounds()`, or the implicit `[p, p+sizeof(T))` for
`[[cccc::single]]`), recomputed at each checked access, never from
`vm->sorted_allocs` — so it is uniform across heap, stack and global storage,
unlike every other opcode in this table. See
[SAFETY.md](SAFETY.md#checked-pointers) for the full attribute/lowering
reference. Like every opcode in this table, `CHKR` has no equivalent in
`-c=native`/`-m`/`-c=generated` output — VM-only by design (#924); those modes warn
and ignore `--checked-pointers` rather than emitting an inline check.
`CHKA3` is unaffected — alignment is pure address arithmetic and was already
correct for interior pointers.

`CHKRO` (#942) is `CHKR`'s sibling for a checked-pointer-bounds-propagation
candidate (`src/parse_checked.c`'s `propagate_checked_bounds()`) that is only
checked-rooted on *some* paths, not every path — "OPT" in
[SAFETY.md](SAFETY.md#checked-pointers)'s propagation writeup, as opposed to
a "FULL" candidate (every store checked-rooted), which still emits plain
`CHKR` with identical codegen to before #942. An OPT candidate's snapshot
temps are refreshed at every assignment, rooted or not — a non-rooted store
writes the `[lo=-1, hi=0)` sentinel instead of skipping the refresh, and the
temps are also seeded with it at function entry — so whichever store
actually executed on the current path (or none at all) is what `CHKRO` sees.
This makes propagation exact per executed path with no CFG/join/fixpoint
needed: a static, join-based dataflow pass would still leave `q[i]` in `q =
malloc(...); if (c) q = p; q[i];` permanently unchecked (the fact isn't live
on every incoming edge of the join), whereas `CHKRO` enforces it exactly on
the runs where `c` took the `q = p` branch. Wire format is identical to
`CHKR` (`op_CHKR_fn`/`op_CHKRO_fn` in `src/ops.c` share one `chkr_common()`
body, differing only in whether the sentinel short-circuits).

`CHKNT` (#923/#938) covers the one gap `CHKR`'s bounds widening for
`[[cccc::ntarray]]` opens: `count()`/`byte_count()`/`bounds()` all widen the
checked range by one element (`sizeof(T)` bytes at the declared end of the
range) so the terminator slot is a legal write target (`CHKR` already
enforces this), but nothing stopped that write from putting a non-null value
there and silently destroying the invariant the widening exists to serve.
`CHKNT` is emitted from the store path (`src/codegen_expr.c`'s `ND_ASSIGN` case),
not from `gen_addr` alongside `CHKR`, because it needs the value being
stored, which `gen_addr` never sees. A second emission site (#937) sits in
the `ND_CAS` case: `to_assign()` (`src/parse_expr.c`) desugars a read-modify-write
(`s[n] += 1`, `s[n]++`) into a synthesized store deref that now carries the
same checked-pointer fields as the original access, so it reaches the
`ND_ASSIGN` site like any other checked store; an `_Atomic`-qualified
`ntarray` element's RMW desugars into a CAS loop instead, so its `ND_CAS`
node carries the fields and gets its own `CHKNT` emission, checking the
CAS's *desired* value ahead of the `ACAS` opcode. It deliberately does **not** attempt to
verify a null terminator is present anywhere in the declared range: in
Checked C, `count(n)` on an `_Nt_array_ptr` is a *lower* bound on the valid
extent (`count(0)` is a legal, terminator-free declaration), so a presence
scan would false-positive on conforming code, and locating the real
terminator would require reading past `hi` — exactly the unbounded read this
feature exists to prevent. See [SAFETY.md](SAFETY.md#checked-pointers)'s
"Terminator invariant" section for the full reasoning and its known gaps.

`CHKNT` also propagates (#943) across a `CHKR`/`CHKRO` bounds-propagation
candidate (`src/parse_checked.c`'s `propagate_checked_bounds()`): `Obj.checked_prop_
nt_elem` carries the terminator-slot fact (non-zero iff every checked-rooted
store into the candidate was `ntarray`-rooted at the same pointee element
size) alongside the already-propagated `[lo, hi)` snapshot, so a store
through a *propagated* pointer into an `ntarray` source's widened terminator
slot emits `CHKNT` exactly like a direct-access store does. The
read-modify-write and `_Atomic` desugars are covered too, via a
`Node.checked_rmw_mirror` back-link `to_assign()` sets on the original deref
at parse time (before propagation has resolved anything) so the propagation
pass's attach walk can find and stamp the synthesized RMW store node once
its bounds are known.

`CHKNTZ` (#939) covers the pointee types `CHKNT` structurally cannot: a
struct/union or wide `_BitInt`/`_Decimal` assignment never puts its value in
a single register at all — `ND_ASSIGN`'s codegen routes these through a
dedicated memcpy branch (`src/codegen_expr.c`, ahead of the generic scalar-store
path `CHKNT` is emitted from) that stages a source address and a
destination address for `MCPY`. `CHKNTZ` is emitted in that branch, right
before the `MCPY`, scanning `checked_access_size` bytes at the *source*
address for any non-zero byte — the aggregate analogue of `CHKNT`'s
value-register comparison. Running before the copy (rather than scanning
the destination afterward) means the terminator slot is never actually
clobbered by a write that turns out to violate the invariant, matching
every other `CHK*` opcode's before-the-effect timing. `float`/`double`
pointees stay on `CHKNT` itself: their value is transferred out of the
`FReg` file into an integer register first (`FR2R`/`FR2R_F32`, the same
bit-pattern-transfer idiom used elsewhere to move a flat-double value into
an integer register), then compared to 0 exactly like an integer store —
`FR2R_F32`'s payload is the raw `float` bits, so `-0.0f` (non-zero bits,
even though it compares equal to `0.0f`) correctly traps. `long double`
pointees are excluded from both opcodes: its widened terminator slot is
16 bytes but the actual store is an 8-byte flat-double `FSTR`, so no
existing opcode inspects the full stored representation.
`checked_nt_pointee_supported()` (`src/parse_checked.c`) is the single gate deciding
which pointee types set `checked_nt_terminator` at all — both the
direct-access site (`compute_checked_bounds()`) and the propagation
attach site (`checked_prop_attach_scan()`) call it, so `CHKNT`/`CHKNTZ`
selection and propagation agree on exactly the same supported-pointee set.
`op_byte0_is_int_src()` (`src/optimize.c`) lists `CHKNTZ` alongside `CHKNT`
(all three operand bytes are pure sources), so it gets the same
copy-propagation and reordering-barrier treatment.

`CHKAB` (#944) is `CHKR`'s counterpart for the *opposite* trust direction —
Checked C's `_Assume_bounds_cast`. Where propagation only ever widens trust
into a previously-unchecked target, `CHKAB` *verifies* trust when assigning
into a target that is itself already declared checked: `src/parse_checked.c`'s
`verify_checked_assign_bounds()` rewrites `q = E;` (both `q` and `E`
declared-checked, `E` a direct source, not a #941-propagated one) into
`(temp = E's own bounds), (q = E)`, and codegen emits `CHKAB` twice after the
store — once against `q`'s own declared lower bound, once against its upper
bound — enforcing that `E`'s bounds imply `q`'s. `q`'s own bounds are
evaluated *after* the store (self-referencing on `q`'s own value, e.g.
`[q, q + m*sizeof(T))` for `count(m)`); the source's bounds are snapshotted
into temps *before* it, the inverse of `CHKR`'s own propagation-snapshot
ordering. See [SAFETY.md](SAFETY.md#checked-pointers) for the full writeup
of both features.

`CHKT3` is live (#651, extended to byte granularity by #653). It is emitted
by `emit_load_ex`/`emit_store_ex` (`src/codegen_emit.c`) right after `CHKP3`,
carrying the pointee's static `TypeKind` and size, and a 3-way mode:
`[rs:8|mode:8]` in the operand word, `[(size<<8)|expected_type:64]` as the
trailing immediate. `mode` is `CHKT3Mode` (`src/cccc.h`):

| Mode | Emitted by | Effect |
|---|---|---|
| `CHKT3_MODE_CHECK` (0) | a load | compares the shadow across `[ptr, ptr+size)` against `expected_type` |
| `CHKT3_MODE_STAMP` (1) | a store | establishes `expected_type` as the effective type for `[ptr, ptr+size)` |
| `CHKT3_MODE_CLEAR` (2) | a union member store | erases (rather than stamps) `[ptr, ptr+size)`, so a later access through a different union member doesn't false-positive |

Type tracking (#653) is a **byte-granular shadow** (`vm->type_shadow_pages`,
logically one byte per heap byte, indexed by `(char*)ptr - vm->heap_seg`,
physically a sparse page table of 64 KiB pages — see below), replacing the
earlier one-`type_kind`-per-allocation model — this is what lets a struct
member or array element be checked independently of the allocation's base
pointer or any other member/element. `TY_VOID` (0) means "no effective type
established", mirroring C11 §6.5p6: fresh heap bytes start there; a store
stamps the range it writes; a load checks the range it reads. A range whose
bytes carry more than one stamped type (e.g. straddling two prior stamps) is
treated as no-info rather than guessed at. `char`-typed accesses are always
legal (§6.5p7): a `char` load never checks, and a `char` store clears rather
than stamps. `CHKT3` resolves the containing allocation via the same
`heap_alloc_for_ptr`/`sorted_allocs_find` helper as `CHKB` and `CHKP3`
(#650's pattern) purely to find the allocation's liveness (`freed`) and
range — the type information itself lives in the shadow, not in
`AllocHeader`.

There are two shadow instances (`TypeShadowSeg`, `src/cccc.h`), one per
tracked segment: `vm->heap_shadow` (the original #653 scope) and
`vm->data_shadow`, covering globals (`static`/file-scope variables, #752).
`type_shadow_locate` (`src/ops.c`) resolves which segment (if either) a
given `[ptr, ptr+len)` range falls wholly inside; `op_CHKT3_fn` itself
still gates on `heap_alloc_for_ptr`/`sorted_allocs_find` exactly as before
for any address inside the heap segment — a heap-range address that isn't
inside a *live* allocation reports nothing at all here (`CHKP3`'s job) —
and skips that resolution entirely for an address outside the heap
segment, deferring to `type_shadow_locate` to decide whether it's a global
or an untracked address (e.g. the stack). Each segment's page *vector* is
lazily grown (`type_shadow_ensure`, `src/ops.c`) to cover its committed
size whenever `CCCC_TYPE_CHECKS` is set, so it stays in sync even if the
flag is toggled on mid-run by `#pragma cccc config(safety=N)`; individual
64 KiB pages are allocated only when a stamp first touches their range, and
freed back to `NULL` the moment a clear zeroes one in full
(`type_shadow_fill`, `src/ops.c`), so host memory for the shadow tracks the
*live* stamped footprint rather than either segment's total reservation —
a large allocate-then-free pattern on the heap reclaims its shadow pages
once the allocation is freed. A missing page reads back as all `TY_VOID`
("no info"), identical to an allocated all-zero page.

Every heap-mutating opcode keeps the heap shadow current: `MFRE` clears on
free; `MCPY` (backing struct/union assignment and the restrict
memcpy-loop lowering) propagates source shadow onto destination, mirroring
`memcpy` copying the effective type along with the bytes; `REALC` (always a
fresh bump allocation, never grown in place) carries the old block's shadow
to the new address before freeing the old one. (`MALC`/`CALC`/`MALCA` need
no explicit clear for a fresh allocation: with the page-chunked shadow, a
range no stamp has ever touched already reads back as `TY_VOID` at zero
host cost.) Guest `memcpy`/`memmove` route through shadow-aware host shims
(`cccc_shim_memcpy`/`cccc_shim_memmove`, `src/stdlib/string.c`) that call
`cc_type_shadow_copy` after the real libc call runs — this also covers a
copy between the heap and a global, since `type_shadow_copy` resolves src
and dst independently. Every *other* host function reachable through
`CALLF` might write heap or global bytes with no VM-level hook at all
(`fread`, `read`, `scanf`, any other FFI call) — `op_CALLF_fn` conservatively
clears shadow state reachable through an integer argument register before
such a call runs, so a write the VM can't observe can never leave a stale
stamp that later false-positives. The clear resolves either tracked segment
(`ffi_shadow_clear_extent`, `src/ops.c`): a heap address clears by its
allocation's extent (base pointer to `header->size`, via
`heap_alloc_for_ptr`, unaffected by which byte of the allocation the
pointer argument actually points at); a data-segment (global) address has
no allocation header to bound it, so it clears from the pointer to the end
of the emitted data segment (`vm->data_ptr`). `RETBUF` clears its handed-out
`return_buffer_pool` slot's shadow range before returning it: those slots
live in `data_seg` and rotate between every struct-returning call, so
without the clear a slot would carry a stale stamp from whichever struct
type was returned through it last.

`op_CALLF_fn`'s clear is classified per host function by name (#751,
`ffi_shadow_classify`/`ffi_shadow_rules`, `src/ops.c`), recovering coverage
the blanket clear used to destroy for functions whose write behavior is
statically known:

| `FfiShadowClass` | Effect | Examples |
|---|---|---|
| `FFI_SHADOW_HANDLED` | no clear — the shim already propagated | `memcpy`, `memmove`, `qsort` |
| `FFI_SHADOW_READONLY` | no clear at all — never writes through any pointer arg | `strlen`, `strcmp`, `memcmp`, `fwrite`, `bsearch`, ... |
| `FFI_SHADOW_BOUNDED` | clear narrowed to `[args[out_arg], args[out_arg]+len)`, where `len` comes from another argument (or a fixed size, for `strtol`/`strtod`'s `*endptr`), clamped against the remaining bytes in whichever segment resolves the pointer | `fread`, `snprintf`, `read`, `recv`, `strncpy`, ... |
| `FFI_SHADOW_DEFAULT` (unclassified) | whole-object clear (heap allocation or, for a global, pointer-to-end-of-data-segment) | everything else, including `printf`/`scanf` (`%n`) |

A `BOUNDED` entry only narrows the clear for its one designated argument;
every *other* pointer-shaped argument to that same call (e.g. `snprintf`'s
variadic `%s` arguments) still gets the default whole-object clear.
Since an unclassified name behaves exactly as before, adding or widening a
rule can only ever reduce clearing relative to today's behavior, never
introduce a false positive.

`qsort` (#769) is `HANDLED` by `wrap_qsort` (`src/stdlib/stdlib.c`) rather
than by a table entry, since narrowing its clear needs runtime information
(the actual pre/post shadow contents) a static rule can't express: `qsort`
only reorders whole `size`-byte elements, never rewrites their bytes, so if
every element's shadow pattern already matches element 0's before the host
`qsort()` call, that pattern is invariant under any permutation and the
shadow needs no clear at all — checked via
`cc_type_shadow_elements_uniform` (`src/ops.c`), unconditionally both
before the call and again immediately after. The post-check runs even when
the pre-check found a uniform range (e.g. an already-cleared, all-`TY_VOID`
range, itself trivially uniform): the comparator is guest code, reentered
through the callback trampoline, and its own loads/stores run through
ordinary CHKT3 checks, so a comparator that writes through its arguments
mid-sort stamps the shadow at that element's pre-move position, which the
post-check must still catch before `qsort` relocates the bytes elsewhere.
Either check finding a non-uniform range clears exactly
`[base, base+nmemb*size)` via `cc_type_shadow_clear_range` -- the range a
host `qsort()` call can touch, not the whole allocation. `bsearch`'s host
half writes through no argument at all, so it needs no runtime check and is
a plain `READONLY` table entry; its comparator is shadow-tracked the same
way `qsort`'s is.

Reusing a heap buffer as a different type — legal C — never false-positives,
since the next store simply re-stamps the effective type for the bytes it
touches. Union member access is exempted in both directions via
`vm->compiler.in_union_member_access` (set/cleared around `ND_MEMBER`
codegen when the immediate parent expression's type is `TY_UNION`): a union
load skips CHKT3 emission entirely; a union store emits `CHKT3_MODE_CLEAR`
instead of `CHKT3_MODE_STAMP`. Covers the heap and globals; stack
subobjects remain untracked (deferred — stack-slot reuse across frames
would need its own liveness bookkeeping, mirroring `frame_epochs`/
`stack_intervals`, to avoid reopening the false-positive class those exist
to prevent in the dangling-pointer detector).

### Stack Instrumentation Opcodes

| Opcode | Description |
|--------|-------------|
| `SCOPEIN` | Activate variables belonging to a lexical scope; records each variable's runtime liveness by actual address (bp+offset), not just its declaration record (#671) |
| `SCOPEOUT` | Deactivate variables (see #669 for why this no longer also aborts on tracked stack addresses — that check is detection-only, see `man/SAFETY.md`) |
| `CHKL` | Check variable liveness before access (use-after-scope / use-after-return), keyed by runtime address so cross-function offset collisions and recursive re-entry don't produce false positives (#671) |
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
| `CALLN` | Native-aware indirect call (dynamic symbol or FFI-token function pointer, falling back to a VM function).  Operands: `rs`, `meta` (bits 0-15 = nargs, bit 16 = returns_double, bit 17 = returns_float, bit 18 = is_variadic, bits 19-31 = fixed_param_count), `double_arg_mask` (2 words), `float_arg_mask` (2 words) — total 6 operand words. The variadic/fixed-param-count bits let the handler tell fixed flonum params (passed in `FREG_A0+`) apart from variadic-tail doubles (bit pattern in `REG_A0+`, matching the internal-call ABI so `va_arg` can read them after `ENT3` spills), and select the correct `libffi` prep (`ffi_prep_cif` vs `ffi_prep_cif_var`) for the callee |

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

When using `-E` (preprocessed output) or `-m`/`-c=generated` (serialized C
output), any queued libraries are re-emitted at the top of the output as
`#pragma cccc link("name")` — CCCC's own spelling, ignored as a harmless
unknown pragma by a plain downstream `cc` but parsed back into
`pragma_link_libs` if the output is ever re-fed to cccc itself, so the
requirement round-trips (#1149). `-c=native` emits neither spelling: the
library already reaches the host linker directly via the `-l` merge above,
so re-stating it as a pragma in the temporary source would be redundant.

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

The pool itself (`alloc_return_buffer_pool`, `src/codegen_func.c`) is allocated in the data segment at `CCCC_MAX_DATA_ALIGN` (64-byte) alignment (#1136), covering the widest by-value return this convention carries — a 512-bit vector (#722).

A registered FFI cfunc (`cc_register_cfunc`) must honor this convention on
both ends, not just the host C ABI's own: a struct/union argument arrives as
a pointer to the *caller's own* storage (#714/#1078 — no scratch copy is
made for a struct/union the way there is for a vector/decimal arg, see the
by-value ABI writeups above), and a struct/union return must go back as a
pointer, not the host ABI's in-register struct return. Registering a raw
host symbol whose own C ABI takes or returns an aggregate by value (rather
than a small `wrap_*` marshalling function) silently breaks this — a guest
argument's address gets read as if it were the struct's own first word, or
REG_A0 gets treated as a struct pointer when it's actually raw return-register
bytes. Found auditing #1087: `inet_ntoa(struct in_addr)` (by-value argument)
and `div`/`ldiv`/`lldiv` (by-value return) were both registered raw; fixed
with wrapper functions (`wrap_inet_ntoa`, `wrap_div`/`wrap_ldiv`/`wrap_lldiv`)
that marshal explicitly, `src/stdlib/posix_net.c`/`stdlib.c` (#1090).

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

## Decimal Floating-Point

`_Decimal32/64/128` (C23) use real IEEE-754-2008 decimal encoding via the
Intel BID library, opt-in with `make CCCC_HAS_DECIMAL=1`. The library is
never vendored: `tools/fetch_intel_bid.sh` downloads (or reuses an
already-present tarball), verifies its SHA3-256, and builds `libbid.a` into
a gitignored `build/intel-bid/` prefix.

```
tools/fetch_intel_bid.sh
make CCCC_HAS_DECIMAL=1 CCCC_BID_PREFIX=build/intel-bid
```

Without the flag, `_Decimal32/64/128` declarations, `sizeof`, `_Alignof`,
typedefs, and struct/array layout all work normally (they have their own
`TypeKind` values, `TY_DECIMAL32/64/128`, sized 4/8/16 bytes); only decimal
*literals and arithmetic* require the library, and are a clean compile
error without it. A guest program distinguishes the two configurations via
the `__STDC_IEC_60559_DFP__` predefine (also exposed as the legacy
`__STDC_DEC_FP__` spelling).

### Value representation

A decimal value is **address-based**, like a small struct or a wide
`_BitInt(N>64)` value — never routed through `fregs[]`/`vregs[]`. This is
what lets `_Decimal128` (16 bytes) work with no special case: every width
is "a pointer to a 4/8/16-byte BID buffer" carried in an ordinary integer
register. Local/temporary storage comes from the same per-function scratch
stack pool wide `_BitInt` uses (`alloc_decimal_temp`, rounding up to whole
64-bit words). Struct members, array elements, and function locals get
their storage sized directly from `ty->size` like any other type.

### Opcodes

All twelve decimal opcodes share one convention: **zero operand words,
fixed argument registers** (`REG_A0`–`REG_A3`), identical in shape to the
`WIDE_ADD`/`WIDE_SUB`/… family (#456) used for wide `_BitInt`. Arguments are
loaded into place with ordinary `MOV3`/`LEA3`/`LI3` immediately beforehand;
the opcode itself just performs the operation and is treated as fully
**opaque** by the optimizer (`op_implicit_abi_regs`, `src/optimize.c`) —
the same conservative treatment already used for `WIDE_*`/`CALLF`, which is
what makes this safe: no decimal op is ever RRRS-operand-decoded, so there
is no `op_byte0_is_int_src`-style classification to keep in sync.

`width` is `0` = `_Decimal32`, `1` = `_Decimal64`, `2` = `_Decimal128`.

| Opcode | Arguments | Effect |
|--------|-----------|--------|
| `DADD` | `A0`=dst addr, `A1`=a addr, `A2`=b addr, `A3`=width | `*dst = *a + *b` |
| `DSUB` | same | `*dst = *a - *b` |
| `DMUL` | same | `*dst = *a * *b` |
| `DDIV` | same | `*dst = *a / *b` |
| `DNEG` | `A0`=dst addr, `A1`=a addr, `A2`=width | `*dst = -*a` |
| `DCMP` | `A0`=a addr, `A1`=b addr, `A2`=width | `A0` result: `0`=EQ, `1`=LT, `2`=GT, `3`=UNORDERED |
| `DFROMI` | `A0`=dst addr, `A1`=int64 value, `A2`=width, `A3`=is_unsigned | `*dst = (decimal)value` |
| `DTOI` | `A0`=src addr, `A1`=width, `A2`=is_unsigned | `A0` = `(int64)*src`, truncating (C semantics) |
| `DFROMBITS` | `A0`=dst addr, `A1`=raw f32/f64 bits, `A2`=width, `A3`=src_is_f32 | `*dst = (decimal)binary_value` |
| `DTOBITS` | `A0`=src addr, `A1`=width, `A2`=dst_is_f32 | `A0` = raw f32/f64 bit pattern of `(binary)*src` |
| `DCVT` | `A0`=dst addr, `A1`=src addr, `A2`=dst_width, `A3`=src_width | `*dst = (decimal)*src` (decimal-to-decimal) |
| `DFMT` | `A0`=buf, `A1`=n, `A2`=val addr, `A3`=width | `A0` = bytes that would have been written (`snprintf` contract) |

`DFROMBITS`/`DTOBITS` move the binary float side as a raw bit pattern via
`FR2R`/`FR2R_F32` and `R2FR`/`R2FR_F32` (the existing float-register
bit-reinterpret opcodes) rather than through memory — no decimal opcode
ever touches an `FReg` directly. `DCMP`'s four-way result (rather than a
plain `-1/0/1`) is what lets `==`/`!=`/`<`/`<=` all be derived correctly
even when one operand is NaN; `>`/`>=` never reach codegen as distinct
opcodes — like binary float, they're normalized to a swapped `<`/`<=` at
parse time.

**Clobber contract (#838):** every `D*` opcode and every `WIDE_*` opcode
reads/writes only `REG_A0`–`REG_A5` and the fixed-A-register block's opaque
side effects (`emit_wide_op`, `src/codegen_emit.c`) — none of them touch a `T`
register, and codegen relies on this: the decimal and wide-`_BitInt` binop
branches stage one operand's address across the other operand's evaluation
in a `T` register, which would be silently clobbered if these opcodes reset
the temp-register allocator the way a real call does. This differs from
`AND`/`OR`/`XOR`/comparison wide-`_BitInt` ops and any decimal op reached
through a genuine FFI call path (`emit_wide_helper`'s `CALLF`), which *do*
clobber caller-saved `T` registers like any other call and must reset the
allocator accordingly. Before #838, `emit_wide_op` also called
`reset_temp_regs()` defensively; that was removed because no `op_*_fn` handler
for a `D*`/`WIDE_*` opcode (`src/ops.c`) ever reads or writes a `REG_T*` slot,
and the reset was unsound — it silently freed a live temp belonging to an
*enclosing* stack frame's still-in-progress expression, not just the current
one.

`__builtin_decimal_to_chars(buf, n, decimal_value)` lowers directly to `DFMT`
and always produces BID's canonical shortest-form string (no flags, width, or
precision — that's its whole contract).

### `printf`/`scanf` integration (#829)

`%Hf`/`%Df`/`%DDf` (`_Decimal32`/`_Decimal64`/`_Decimal128`) work with the
`f`/`F`/`e`/`E`/`g`/`G` conversions, including flags (`- + space # 0`), field
width, and precision — the same surface as a binary `double`. Two runtime
shim entry points do the work (`src/stdlib/decimal.c`, declared in
`src/internal.h`, both raw-byte-pointer like the arithmetic shim above):

- `cccc_dec_format_ex(buf, n, val, width, conv, flags, field_width, prec)` —
  printf side. Decomposes the value via `__bidNN_to_string` into a
  `(sign, digit-string, decimal exponent)` triple, rounds it to the requested
  precision with round-half-even (BID's own default rounding mode, so this
  stays consistent with `+`/`-`/`*`/`/`), and renders fixed/scientific/general
  form from that triple directly — never through a binary `double`
  intermediate, so it's exact for values a `long double` can't represent
  (verified for `_Decimal128`).
- `cccc_dec_from_string(width, dst, s)` — scanf side. Wraps
  `__bidNN_from_string`, the same entry point compile-time decimal literals
  use (`cccc_dec_encode_literal`), so parsing is exact rather than routing
  through `strtold`.

No native host libc implements decimal floating-point conversions (verified
against real glibc 2.39: `printf("%Df", ...)` prints the length modifier and
conversion character literally rather than formatting the argument), so a
`CCCC_HAS_DECIMAL=1` build always routes `printf`/`fprintf`/`sprintf`/
`snprintf` through the custom `stb_sprintf`-derived engine
(`src/stdlib/format_printf.c`) regardless of `CCCC_HAVE_NATIVE_PCT_B` — the
same reasoning that already makes the `scanf` family route through the custom
engine unconditionally (#728).

**Variadic ABI.** A decimal argument in the variadic tail of a call — the
`x` in `printf("%Df", x)` — is passed **by pointer** to a caller-frame
scratch copy (`gen_decimal_arg_ptr`, `src/codegen_emit.c`), mirroring the
vector-variadic convention (#721, see the "ABI" subsection below).
`_Decimal32/64/128` isn't
subject to default argument promotion (unlike `float`→`double`), which is
why three distinct length modifiers exist; the by-pointer convention keeps
all three widths distinct through a single 8-byte variadic slot regardless
of which one is used. The scratch copy is
always allocated at the full 16 bytes / 16-byte alignment, independent of
the argument's actual 4/8/16-byte width: `validate_format_call`'s
`%Hf`/`%Df`/`%DDf` type-checking only covers a literal format string and
only warns, so a width mismatch **between two decimal widths** (`%DDf`
reading a `_Decimal32`) must stay memory-safe — it reads
uninitialized-but-in-bounds bytes from a fully-sized object rather than
overrunning a narrower one. A mismatch **between a decimal and a
non-decimal argument** (`%Df` given a plain `double`) is a different,
unfixable case: it dereferences the `double`'s bit pattern as a pointer and
crashes, exactly the same real UB `%s` given a `double` already has today
with any pointer-expecting conversion — the by-pointer decimal ABI doesn't
introduce a new hazard class here, though it does make the *consequence* of
that specific mismatch a crash rather than garbage output (GCC's decimal
varargs are by-value, so the equivalent GCC mismatch just misreads bits).
Not something `-3`'s dangling-pointer detection can catch either: the
dereference happens host-side inside `cccc_dec_format_ex`/`__bidNN_to_string`
(via `va_arg(va, void *)` in `stb_sprintf.h`'s decimal short-circuit),
outside the VM's own instrumentation. `<stdarg.h>`'s `va_arg`
detects the by-pointer case via `__builtin_classify_type`, extended with a
second discriminant (`CCCC_DECIMAL_TYPE_CLASS = 98`, alongside vector's
`99`) folded into the same by-pointer `va_arg` arm.

### `<math.h>` transcendentals (#828)

`sqrt`/`exp`/`log`/`pow`/`sin`/`cos`/... for `_Decimal32/64/128` (`<decimal_math.h>`,
opt-in with `CCCC_HAS_DECIMAL=1`; see [COVERAGE.md](COVERAGE.md)'s C23 header
table for the full function list) add **no new opcodes**. Unlike the twelve
`D*` arithmetic opcodes above — which call directly into `src/stdlib/decimal.c`
from `src/ops.c` and are never FFI-registered — decimal maths is an ordinary
stdlib module (`src/stdlib/decimal_math.c`, registered like any other
`<header.h>` → `register_*_functions` mapping in `tools/stdlib.tsv`). A guest
call to e.g. `sqrtd64()` goes through the normal FFI call path into six
op-dispatch entry points, each taking raw byte addresses (same "no BID type in
a VM header" convention as the `cccc_dec_*` shim functions above) rather than
through a dedicated opcode.

### `strtod32`/`strtod64`/`strtod128` (#832)

Parsing a decimal from a *runtime* string (as opposed to a compile-time
literal) adds **no new opcode**, following the `<math.h>` precedent directly
above: `include/stdlib.h`'s `strtod32`/`strtod64`/`strtod128` are `static
inline` wrappers over a single `long long`-uniform FFI entry point,
`__cccc_dec_strtod` (`src/stdlib/stdlib.c`), which forwards to
`cccc_dec_strtod` (`src/stdlib/decimal.c`). `cccc_dec_strtod` scans the
longest valid numeric prefix itself — `__bidNN_from_string` gives no
`endptr` — sizing its own copy to the scanned length rather than a fixed
buffer, so an arbitrarily long coefficient never truncates. `*endptr` and the
no-conversion (`+0`, `endptr == s`) case match C's `strtod()` contract.

Decimal **return by value** from a guest `static inline` function is already
supported (the struct-ABI reuse described below) — this is exactly what
every `<decimal_math.h>` wrapper already does, so no decimal value ever
crosses the FFI boundary itself. `#830`'s restriction (decimal rejected as a
*fixed* FFI parameter or return) doesn't apply here for that reason.

### `<fenv.h>` rounding and exception flags (#832)

Phase 1 hard-wired every arithmetic/conversion entry point in
`src/stdlib/decimal.c` and `src/stdlib/decimal_math.c` to
`BID_ROUNDING_TO_NEAREST` and discarded BID's exception-flags out-parameter,
so `fesetround()` had no effect on decimal arithmetic and a decimal overflow/
underflow/inexact/invalid/divide-by-zero could never be observed via
`fetestexcept()`. Both are now real:

- **Rounding.** `cccc_dec_host_rounding()` (`decimal.c`) / `dm_host_rounding()`
  (`decimal_math.c`) map the host's *current* `fegetround()` mode to the
  matching `BID_ROUNDING_*` constant (`FE_TONEAREST`/`_DOWNWARD`/`_UPWARD`/
  `_TOWARDZERO` → `BID_ROUNDING_TO_NEAREST`/`_DOWN`/`_UP`/`_TO_ZERO`) — the
  same mapping `__cccc_flt_rounds` (`src/stdlib/fenv.c`) already used for
  `FLT_ROUNDS`. `BID_ROUNDING_TIES_AWAY` has no portable `<fenv.h>`
  equivalent and is unreachable (follow-up ticket).
- **Exception flags.** `cccc_dec_raise_flags()` / `dm_raise_flags()` map
  BID's `DEC_FE_INVALID`/`_DIVBYZERO`/`_OVERFLOW`/`_UNDERFLOW`/`_INEXACT`
  bits (verified numerically identical across platforms) to the matching
  host `FE_*` bits and call `feraiseexcept()`. `DEC_FE_UNNORMAL` (denormal)
  has no portable `FE_*` equivalent and is masked out.

Every entry point that can round or raise takes a trailing `env` parameter
(`CCCC_DEC_ENV_DYNAMIC` or `_STATIC`, `src/internal.h`):

- **`CCCC_DEC_ENV_DYNAMIC`** — the runtime policy above (host rounding mode,
  flags raised). Used by every `src/ops.c` `D*` opcode handler and by the
  runtime `strtod32/64/128`/scanf paths.
- **`CCCC_DEC_ENV_STATIC`** — always round-to-nearest, flags discarded. Used
  only by the compile-time constant folder (`src/parse_expr.c`'s `eval_decimal`,
  see below), which runs inside the *compiler* process and must never
  observe or perturb the host FP environment it happens to be in.

`cccc_dec_neg`/`cccc_dec_cmp` take no `env` (negation is exact; the quiet
comparisons cannot raise). `cccc_dec_to_int` keeps `__bidNN_to_int64_int`
(truncation *is* C cast semantics regardless of rounding mode) but still
raises flags. `cccc_dec_format`/`cccc_dec_format_ex` (printf) deliberately
stay flag-discarding and nearest-rounding — `printf` isn't required to raise
FP exceptions, and its precision rounding is independently specified as
round-half-even.

**Compile-time evaluation must never leak into the guest's runtime FP
state**, in either direction — the trap this design exists to close:

- A comptime macro (`[[cccc::comptime]]`) calling a decimal `<math.h>`
  function reaches `src/stdlib/decimal_math.c` directly (no `env`
  parameter there at all — it's FFI-only, always dynamic), so it depends on
  a save/restore barrier around the comptime `vm_eval()` call
  (`src/macros.c`'s `fenv_barrier_begin`/`_end`, wrapping both the
  expression-position macro-call site and the `__builtin_comptime_init`
  site) to avoid leaving the *compiler's* rounding mode or exception flags
  dirty after that macro runs.
- `eval_decimal` (`src/parse_expr.c`) — the folder used by `write_gvar_data` for
  a decimal-typed static/global initializer, and by `eval_double`/`eval2`'s
  `ND_CAST` arms for the reverse decimal→binary-float/decimal→int
  directions — wraps its own top-level entry the same way.

Both barriers save/restore only the **rounding mode**, not the whole
exception-flag state, and explicitly `feclearexcept()` before returning —
deliberately *not* round-tripping the flags through `fegetenv()`/`fesetenv()`.
This survives an independently-verified pre-existing condition: the
*compile* phase can already leave host FP exception flags dirty before any
decimal code runs at all (`tokenize.c`'s `convert_pp_number` scans every
floating/decimal literal's extent via a host `strtold()` call whose value is
discarded but whose side effect isn't — `strtold("1.1", NULL)` alone sets
`FE_UNDERFLOW` on at least one verified platform) — restoring a saved
environment via `fesetenv()` would silently reintroduce that dirty state.
The guest program's actual clean-start guarantee comes from a third,
higher-level fix: `cc_run()` (`src/vm.c`) resets the host FP environment
(`fesetround(FE_TONEAREST); feclearexcept(FE_ALL_EXCEPT);`) exactly once, as
the very first thing it does — before constructors, `main()`, or `atexit`
handlers run (each of which is its own separate `cc_run_at()` cycle). A
constructor's own `fesetround()`/`feraiseexcept()` calls still correctly
persist into `main()` and beyond, matching real linked-C behavior, since the
reset happens only once at the top, not before every `cc_run_at()` cycle —
while whatever the *compile* phase left dirty never reaches the guest at
all.

### Compile-time constant folding (#832)

`static _Decimal64 x = 1.1dd + 2.2dd;` used to be a hard diagnostic — only a
bare literal (or a cast of one) was accepted as a decimal static/global
initializer. `eval_decimal` (`src/parse_expr.c`) now folds `+ - * /`, unary `-`,
`?:`, `,`, a decimal-to-decimal cast, and casts to/from integer and binary
floating-point, mirroring the shape of the existing `eval_double`/`eval2`
integer/binary-float constant folders. Each recursive call derives its
working width from the *node's own* type (`dec_width_code`), never trusts a
caller-supplied width beyond the final store — `usual_arith_conv` is
expected to have inserted a cast wherever two different decimal widths
meet, so a width mismatch indicates a missing cast rather than something
safe to paper over. Folding a decimal-to-integer cast is what makes
`(int)1.5dd` in an array bound, case label, or similar integer constant
expression fold correctly (`(int)(1.1dd + 2.2dd)` also folds, a
GCC-compatible extension: strict C only permits a floating constant as a
cast's *immediate* operand, which `1.5dd` satisfies and the sum doesn't).

**Still deferred** (follow-up ticket): decimal comparisons directly in an
integer constant expression (`_Static_assert(1.1dd == 1.1dd, "")`) — that
needs decimal arms in `eval2`'s `ND_EQ`/`ND_NE`/`ND_LT`/`ND_LE`, which
folding static initializers didn't require.

### ABI

By-value decimal arguments and returns reuse the vector-by-value struct-ABI
machinery (#714): a caller-frame scratch-slot copy for arguments, `RETBUF`
+ copy for returns. `CALLT` (tail-call optimization) is excluded for any
call with a decimal return *or* a decimal argument, for the same frame-reuse
hazard reason vector returns/args are (#716) — the value's scratch slot
lives in the caller's or callee's own frame, so a tail call reusing that
frame would return or read through a dangling pointer.

A decimal value through the **variadic tail** of a call is passed by
pointer (#829, see above) — this covers `printf`/`scanf` and any other
variadic call whose receiving side reads it back via `va_arg`. A decimal
value as a **fixed** parameter or return through a **native FFI call**
remains rejected with a diagnostic: `libffi` has no decimal `ffi_type`, so
neither has a sound by-value marshalling convention yet (tracked as #830).

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
5. **Return-buffer pool**: the return-buffer pool is a fixed-size *rotating scratch*
   pool (`RETURN_BUFFER_POOL_SIZE` slots), not a per-module resource list — a host
   VM produced by normal codegen always starts with every slot already filled, and
   struct/union-returning calls in appended module text resolve their buffer purely
   at runtime from the *host's* pool (the `RETBUF` opcode). So a host VM with its
   own pool keeps it as-is and the module's entries are not merged. The module's
   pool is adopted wholesale only when the host has none of its own yet (e.g. a
   from-scratch VM built solely to stage a loaded module) — offsets and pool
   pointers are re-anchored by `data_shift` together with `return_buffer_size`,
   since a slot's size and its buffer pointers must always move together.
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

0. Before codegen, `cc_collect_link_symbols` (also in `src/bytecode.c`) pre-scans
   each `--link lib.c4a` path's exported symbol names into `vm->compiler.link_syms`.
   Codegen consults this set wherever it would otherwise resolve a bodiless callee to
   a registered FFI symbol — both a direct `CALL` (`ffi_index_for_callee`, the
   call-patch pass) and a function-pointer address-of inside a function body (the
   `func_addr_patches` pass) — so a name that is both an FFI symbol and defined in a
   linked library resolves to the library's definition instead of silently binding to
   the host function (#882). Scoped to what's knowable at this point: a standalone
   `-c` object with no matching `--link` path, a symbol supplied only later via a
   runtime `cc_load_module()` call, or a **file-scope** global initializer's
   address-of (`apply_global_relocations` has no relocation mechanism for an
   unresolved data-segment reference by name, unlike the text-segment case above),
   still resolve to FFI.
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
5. Linking runs once, immediately after codegen — before any terminal sink decides
   what to do with the result. So the fully linked program is what gets written to
   `-o <file>`, what `--testing`/`--disassemble` operate on, and what runs when there
   is no `-o` at all (the compiled program executes directly in-memory). Earlier
   versions only linked on the `-o` path; `--link` with no `-o` silently skipped
   linking and ran the unlinked image, whose deferred `CALL` sites were left at their
   unpatched placeholder operand of 0.

`--link` cannot be combined with `-c=native` (no bytecode linker step exists in the
native-backend handoff to the host C compiler) or with a prebuilt `.c4` file as input
(there is no fresh codegen output in that process for the pass to resolve relocations
against); both are rejected with a clean CLI error rather than silently ignoring the
flag. The prebuilt-`.c4`-input rejection covers every `.c4` input file, not just the
single-file run/`--testing`/`--disassemble` dispatch: the `--ngrams`/`--fusion-candidates`
static-analysis dispatch, which accepts and walks one or more prebuilt `.c4` files via
`cc_load_bytecode`, hits the same rejection when `--link` is also on the command line.

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

### Async Signal / Notification Delivery

Two safe points near the top of the dispatch loop poll for asynchronous events and can deliver them between *any* two bytecode instructions, not just at a call boundary: a pending real signal (`_cccc_pending`, set by an async-signal-safe host shim) and a pending `SIGEV_THREAD` notification (`_cccc_sigev_pending`, set by a host notification thread — `<aio.h>`/`<mqueue.h>`). Both jump into guest bytecode (a VM signal handler or a `sigev_notify_function`) the same way `VRAISE` does: push a return address, then set `vm->pc` to the handler.

Because delivery isn't pinned to a call boundary, the normal caller-saved calling-convention ABI doesn't protect the interrupted code's registers the way it does for a synchronous call. Before jumping to the handler, the dispatch loop snapshots the full register file (`regs`/`fregs`/`vregs`) into a `SigFrame` (`src/cccc.h`) — the same structure that already tracks handler-entry/return for `sa_mask`/`SA_NODEFER`/`SA_RESETHAND` enforcement — backed by a lazily-allocated save area (`vm->async_reg_saves`, one slot per `SigFrame`) so a program that never takes a signal pays nothing for it. `cccc_signal_poll_handler_returns`, called at the top of every dispatch iteration once any frame is active, restores the snapshot once the handler genuinely returns (`sp`/`pc` back exactly where the interruption happened) — a handler that `longjmp()`s out instead is left alone, since `SETJMP`/`LONGJMP` (see Non-Local Jumps below) already own `REG_A0` on that path. If every `SigFrame` slot is in use (`CCCC_SIG_FRAME_MAX`, currently 8), a new async delivery is deferred — left pending — rather than delivered without a place to save its snapshot.

`VRAISE` (synchronous `raise()`) does not use this machinery: it always runs at a call boundary, where the ordinary calling convention already protects the caller, so no snapshot is taken.

## Safety Integration

The VM does not rely on external sanitizer libraries.  Instead, the compiler injects safety opcodes at compile time and the interpreter implements the checks inline:

* **Bounds checks** — `CHKB` (pointer/subscript addition) and `CHKBN` (pointer subtraction, #982) at pointer *formation*, plus `CHKD` (#983) at the *dereference* site, before every array-subscript or pointer-dereference that the compiler can annotate with a size; resolves interior heap pointers via `vm->sorted_allocs`, not just exact base pointers. `CHKB`/`CHKBN` deliberately allow a pointer to land exactly one past its allocation's end (legal C to *form*); `CHKD` is what still traps if that pointer is actually dereferenced.
* **UAF detection** — `CHKP3` consults `AllocHeader` metadata (magic `0xDEADBEEF`, `freed` bit, generation counter); also resolves interior heap pointers via `vm->sorted_allocs`.
* **Uninitialised reads** — `CHKI` / `MARKI` maintain a per-address hash map of initialised stack slots.
* **Stack canaries** — `ENT3` writes a canary word; `LEV3` validates it before returning.
* **CFI** — A shadow stack mirrors the real stack; `CALL` pushes to both. `CALLT` leaves both the real and shadow return-address entries untouched — the tail call reuses the current frame but does not consume the return address it will eventually return through, so its shadow twin must stay in lockstep too. `LEV3` is what pops (and compares) one entry from each stack, whether it belongs to the frame that pushed it or a tail-called frame further down the chain.
* **Provenance tracking** — `MARKP` records `(origin, base, size)`; `CHKPA` rejects arithmetic that leaves the object.
* **Dangling pointers** — three layers in `CHKP3`. (1) A range check against `[vm->stack_seg, vm->sp)`; since the stack grows downward, an address in that range belongs to a frame that has already returned (#670). (2) Per-frame epoch liveness: `ENT3` assigns each activation a monotonic epoch (`vm->frame_epoch_counter`, tracked in the `vm->frame_epochs` push/pop stack and `vm->live_epochs` membership set); `LEA3` tags the resulting `&local` address with the current frame's epoch in `vm->stack_ptr_epochs` (the stack analogue of `ptr_tags` recording a heap allocation's creation generation) unless `LEA3_NO_RECORD` proves it never escapes (#676); `CHKP3` flags a dereference whose tagged epoch is no longer live, catching the case the range check alone misses — a dangling pointer passed *deeper* into another call and dereferenced there (#673). (3) Interior interval-stabbing: `STKTAG` retains `[base, base+size)` for an escaping array/struct local, tagged with the same epoch, in `vm->stack_intervals`; consulted only when layer 2's exact lookup misses, `CHKP3` resolves an interior address to the max-epoch containing interval (epoch order is recency order, so this is correct even when a live frame has reused a dead frame's address range) and flags it the same way (#675). `LEV3`, the tail-call unwind path, and `LONGJMP` (which can retire several epochs in one jump) all keep `frame_epochs`/`live_epochs` in sync with the real call stack; `stack_intervals` entries are retained rather than pruned on frame death — see `man/SAFETY.md` for what's caught. This same bookkeeping is a second, independent consumer for `DYNOBJSZ`'s stack-buffer sizing (#648) — see `STKTAG`/`DYNOBJSZ` above and `man/SAFETY.md`. Because a fresh top-level `cc_run_at` call means a fresh guest call stack, it resets `frame_epochs`/`live_epochs`/`stack_ptr_epochs`/`stack_intervals` to empty before running (without touching the monotonic `frame_epoch_counter`) — this also fixed a latent desync: the test framework's assertion-failure path does a host-level `longjmp` that abandons the guest call stack without running the aborted frames' `LEV3`, which previously left stale epochs to trip the *next* call's `LEV3` tripwire.

  **Lazy per-function activation.** `ENT3`'s epoch push is conditional, not universal: only STKTAG and a recorded LEA3 ever read the *current top* epoch, and both are emitted only for a local or parameter whose address escapes its creating frame — so a function whose body proves it has none of those contributes nothing to liveness and skips the push entirely. Codegen tracks, while generating one function's body, whether `emit_lea3_var` emitted `STKTAG` for an escaping aggregate (`ENT3_PUSH_EPOCH_AGG`, bit 31 of the float-mask half — needed in both `--dangling-detection` and dynobjsz-only mode) or a recorded `LEA3` for an escaping scalar (`ENT3_PUSH_EPOCH_SCALAR`, bit 31 of the f32-mask half — needed only under `--dangling-detection`, since a scalar's recorded `LEA3` write is itself gated on that flag), then patches the two bits into the already-emitted `ENT3` masks word once the body is done (mirroring the existing inlined-locals stack-size patch). `op_ENT3_fn` masks these bits back out before treating the rest as real float-param masks, then pushes only when the relevant bit (and mode) is set. Because `vm->frame_epochs.bps[]` is monotonic (deeper calls pushed later, at strictly smaller `bp`), `LEV3`/`CALLT` no longer assume the top entry belongs to the frame currently unwinding — they pop it only when `bps[count-1] == vm->bp`, a self-synchronizing check that is a no-op for a frame that never pushed. `LONGJMP`'s `frame_epoch_truncate_to` was already `bp`-driven and needs no change. The original `chain_depth == frame_epochs.count` tripwire (which assumed every frame pushes) is replaced by an always-on O(1) guard (`bps[count-1] >= vm->bp`) plus, under `#ifndef NDEBUG`, a walk asserting `frame_epochs` is an in-order subsequence of the live saved-`bp` chain.
* **Use-after-scope / use-after-return** — `CHKL`, keyed by a variable's actual runtime address (bp+offset) via `vm->stack_var_active`, populated by `SCOPEIN`/`SCOPEOUT` per activation (#671).

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
3. **Scalar local promotion** — Hot eligible integer, pointer, and floating-point locals are held in callee-saved VM registers at `--optimize=2` and above. Integer/pointer locals use `REG_S0`–`S3`; `float`/`double` locals use `FREG_S0`–`S3`. Locals captured by an Apple block literal or a GNU nested function (at any nesting depth) are excluded, even with no `&` in the source — a nested function reaches such a local directly through the static-link chain and its stack slot, and promoting it would let the enclosing function and the nested function disagree about its value.
4. **Fused indexed load/store** — Simple array and pointer accesses use `LDR_INDEX_*` / `STR_INDEX_*`, removing separate index multiply and address-add opcodes in hot loops.
5. **Automatic opcode fusion** — `--optimize=4` / `--fuse-ops` rewrites adjacent single-def/single-use arithmetic chains to fused opcodes such as `MULI3`, `MULADD3`, `MULADDI3`, `FMADD3`, `FMADD3_F32`, `FMSUB3`, `FMSUB3_F32`, `FNMSUB3`, and `FNMSUB3_F32`.
6. **Fused floating-point multiply-add/subtract** — `FMUL3+FADD3` chains fuse to `FMADD3`; `FMUL3+FSUB3` fuses to `FMSUB3` (minuend form) or `FNMSUB3` (accumulating-subtract form). Dead-FMOV3 elimination in copy-prop restores adjacency when float local promotion inserts a register-copy between the multiply and subtract.
7. **Tail-call optimisation** — `return f(args)` patterns that meet eligibility criteria emit `CALLT` instead of `CALL + LEV3`, reducing tail-recursive calls to O(1) stack depth (see [Tail-Call Optimisation](#tail-call-optimisation) above).

The dominant cost remains the interpreter itself (as opposed to compile time); see [TOOLING.md](TOOLING.md#benchmarks) for full numbers and [TOOLING.md](TOOLING.md#profiling) for analysis tooling.
