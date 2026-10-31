# VM

Contributor notes: architecture, encoding, execution model and performance.

> Reference documentation for the CCCC bytecode interpreter, instruction set, ABI, and execution model.

## Overview

CCCC's C frontend produces a portable, register-based bytecode that runs on a built-in interpreter (the **VM**).  The VM is the runtime that powers `[[cccc::comptime]]` execution and doubles as a self-contained, introspectable runtime for the memory-safety suite, the debugger, the profiler, and any program run without `-c=native`.  It is intentionally not a JIT-to-machine-code backend: every opcode is interpreted, which keeps the same binary that parses C also able to execute it (or to hand macro-expanded C off to `cc` / `clang` / `gcc` via `-c=native` — see the [README](../README.md)).  This design trades raw execution speed for portability and deep runtime instrumentation in a single self-contained binary. VM pointers are host pointers, so the VM is not a sandbox; comptime alone confines its memory accesses to VM memory (`COMPTIME_CONTAIN` in `src/ops.c`).

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

`r0`/`REG_ZERO` must never be used as an address or scratch register in codegen: writes to it are discarded, so a subsequent load through it always reads from address 0 rather than whatever value was "stored". This is distinct from `r0` legitimately being a load/store *destination* for a discarded-value result — the integer `op_LDR_*_fn` ops guard `rd != REG_ZERO` before writing back, so they simply skip the write; the float `op_FLDR_fn`/`op_FLDR_F32_fn` ops have no such guard and write `fregs[0]` unconditionally, which is harmless only because nothing treats `fregs[0]` as load-bearing. A codegen path that computes an address into `dest_reg` and then loads through that same register must first redirect any `REG_ZERO` destination to a real temporary (see the `gen_expr` guard at `src/codegen_expr.c` ahead of `ND_DEREF`/`ND_MEMBER`, added for the earlier work — a discarded float/double deref used to compute its address into `r0`, i.e. address 0, and segfault on the load).

The floating-point register file (`fregs[32]`) uses the **same indices** and stores a flat `double` per slot.  The frontend already emits type-specific opcodes (`FADD3` vs `FADD3_F32`, `FLDR` vs `FLDR_F32`), so the register itself carries no precision tag.  A `float` value is held as the `double` that results from rounding to float precision and then widening — which is exact — so any register can be read as a `double` with no per-access branch, and the `*_F32` opcodes round their result through `(float)` before the (exact) widening store.

A third register file, `vregs[32]` (`VReg`, see [SIMD / Vector Operations](vm-opcodes.md#simd--vector-operations)), holds up to 512-bit SIMD state — GNU `vector_size` values and (future) autovectorized loop temporaries. It is a raw lane- and width-agnostic union (64 bytes); like `fregs`, the opcode determines the active lane view, and the instruction operand's "scale" byte carries the active width/lane count, not the register.

### Memory Segments

| Segment | Grows | Purpose |
|---------|-------|---------|
| **Text** | Upward | Bytecode instructions (`InstrWord`, 32-bit words) |
| **Data** | Upward | Global variables, string literals, static initialisers |
| **Stack** | Downward | Activation records, locals, spilled arguments |
| **Heap** | Upward | `malloc` / `free` when the VM heap allocator is active (the default; `-V`/`--no-vm-heap` routes it to the host allocator) |
| **TLS** | Upward | Per-thread copy of thread-local (`_Thread_local` / `__thread`) variables |

All segments are reserved upfront as large virtual ranges and committed in `poolsize` chunks (default 256 KiB elements, max 64 MiB elements).  This gives the VM stable base pointers while keeping resident memory modest.

A global (and TLS, see below) object is placed at its own **declared alignment**, not a hardcoded 8 bytes: an explicit `_Alignas(N)` overrides the type's own alignment, capped at `CCCC_MAX_DATA_ALIGN` (64 bytes — the widest alignment any type requests today, a 512-bit vector). `_Alignas(N)` with `N > 64` is accepted but only gets 64-byte placement, a known limitation ([#1902](https://todo.sr.ht/~takeiteasy/cccc/1902)). `cc_effective_align()` (`src/codegen_emit.c`) computes this for every data-segment/TLS-template allocation site. **Local** (stack-frame) variables get the same alignment and cap through a different mechanism — see [Function Frame](vm-opcodes.md#function-frame).

### Thread-Local Storage (TLS)

Variables declared `_Thread_local`, `__thread`, or `thread_local` are placed in a dedicated TLS segment:

1. **Template (`vm->tls_template`)** — built once by `gen()` alongside the data segment.  Each TLS variable is allocated a slot, rounded to its own declared alignment (see above), and its initialiser is written here.  The `LDTLS3` opcode emits the byte offset baked at compile time.
2. **Per-thread copy (`vm->current_tls_seg`)** — allocated by `posix_memalign` (aligned to `CCCC_MAX_DATA_ALIGN` — before that, a plain `malloc`, which only guarantees the platform's default `max_align_t`) for the main thread and for each spawned `pthread_t` or `thrd_t`.  The thread inherits an `memcpy` of the template at creation time (C11 §6.2.4p4 — static initialisation).  On context switch the VM updates `vm->current_tls_seg` to point to the calling thread's copy.
3. **Access (`LDTLS3 rd, imm24`)** — loads the effective address `vm->current_tls_seg + imm24` into `rd`.  Subsequent loads/stores through that pointer are ordinary data-segment accesses.  Since both the base and the offset are now alignment-correct, this address itself carries the TLS variable's full declared alignment.

TLS variable assignment (`vm->tls_template` write) happens in `gen()` at compile time; the per-thread copy is kept consistent via the pthreads context-switch wrappers in `src/stdlib/pthread.c`.

### Calling Convention

* **Integer arguments** are placed in `REG_A0` … `REG_A7`.
* **Floating-point arguments** are placed in `FREG_A0` … `FREG_A7`.
* **Return values** come back in `REG_A0` (integer) or `FREG_A0` (float).
* **Struct returns** use a rotating pool of return buffers allocated in the data segment; the `RETBUF` opcode yields the next buffer address.
* **Struct/union arguments** are passed by address: the caller passes a pointer to its own object in the integer arg register, and the callee's own prologue (`gen_function`, `src/codegen_func.c`) copies the pointee into a fresh frame-local scratch slot (`alloc_wide_bitint_temp`'s per-function pool, the same one vectors/decimals below reuse) and rebinds the parameter's slot to point at the copy instead — true by-value semantics: a write through the parameter never aliases the caller's argument (before this fix, nothing ever copied, so a write silently mutated the caller's object). This copy only exists in a CCCC-emitted callee's own prologue — an **FFI cfunc** (a real host C function registered via `cc_register_cfunc`/`cc_register_variadic_cfunc`) receives the same by-address struct argument with no prologue of its own to copy it, so any FFI wrapper that reads a guest struct argument through that pointer must snapshot it itself before mutating anything through it. The `v*`-family stdio/syslog FFI wrappers (`src/stdlib/format_printf.c`/`format_scanf.c`/`stdio.c`/`posix_lang.c`) learned this the hard way: each takes a guest `va_list` (a plain struct, `include/stdarg.h`) by this same by-address convention and used to extract arguments straight out of the caller's own struct in place, silently advancing the caller's own `va_list` on every host. Fixed with `CCCC_VA_LOCAL` (`src/stdlib/va_ffi_helper.h`), a snapshot-before-extract macro used at all twelve wrapper sites.
* **Spilled arguments** (more than 8) are pushed onto the stack before the call.
* `FREG_A0` … `FREG_A7` share raw register indices with `REG_A0` … `REG_A7`
  (both 10-17, see `src/internal.h`), so a float value and an integer address
  can never both be live "in" the same `A`-register at once. Codegen paths
  that compute a member/variable address and then load a flonum through it
  (e.g. `gen_expr`'s `ND_VAR`/`ND_MEMBER` cases in `src/codegen_expr.c`) must use a
  separate temp register for the address rather than reusing the destination
  register.

The `ENT3` opcode builds the stack frame, copies register arguments and stack-passed fixed arguments to their callee-local parameter slots, and optionally writes a stack canary. For variadic functions, `ENT3` still reserves and spills the first 8 argument slots so `va_arg` can consume any register-passed variadic tail; variadic arguments beyond those slots remain in the caller's stack area. `LEV3` restores `bp`, checks the canary, pops the return address, and resumes at the caller.

A nested function's synthesized `__static_link` parameter is always the first param (prepended at declaration, `src/parse_decl.c`), so under the ordinary layout it lives at `bp[-1]`; `--stack-canaries`/`-3` reserves `bp[-1]` for the canary word instead and shifts every param one slot lower, to `bp[-2]` (`canary_bias`, `assign_lvar_offsets`, `src/codegen_stmt.c`). A multi-hop static-link chase (a nested function reading a variable owned by an ancestor two or more levels up, `emit_static_chain_var_addr`/the sibling-call walk in `codegen_expr.c`) must apply that same canary-aware hop distance at *every* link, not just the first — `static_link_hop_bytes()` (`src/codegen_addr.c`) is the single source of truth both call sites share, after each was found to have independently hardcoded the no-canary distance and silently walked into the wrong slot (SIGSEGV) the moment a chain reached depth 2 under either flag.

### Tail-Call Optimisation

When codegen detects that a `return` statement's outermost expression is a direct call to a statically-known, in-VM, non-variadic, non-nested function whose return type is not a struct/union and whose argument count does not exceed 8, it emits `CALLT` instead of `CALL` followed by `LEV3`.

`CALLT` unwinds the current frame (`sp = bp`, restores saved `bp`) without consuming the return address already on the stack. The callee's subsequent `LEV3` therefore returns directly to the *original* caller, so deeply-recursive helpers and mutually-recursive function pairs execute in O(1) stack space regardless of recursion depth.

Tail-call elimination is **unconditional** — deep-recursion correctness
depends on it, so there is no flag to disable it. Eligibility is decided per
call site (`can_emit_tail_call`, `src/codegen_call.c`).

**Eligibility** (all must hold):

* The call is the sole, outermost expression of the `return` — `return f() + 1` is not a tail call.
* The callee is a directly-addressed in-VM function (not FFI, not a function pointer).
* The callee is not variadic and not a nested function (nested functions require a static-link argument in `REG_A0`).
* The callee does not return a struct, union, or a wide `_BitInt` (over 64 bits) — these returns are materialised through a frame-relative scratch buffer (`RETBUF` for struct/union, a compiler-synthesized scratch slot for wide `_BitInt`), which `CALLT`'s frame reuse would invalidate.
* The call uses 8 or fewer arguments (stack-spilled arguments would fall below the unwound frame).
* No argument carries the address of one of the caller's own locals or parameters — directly (`&x`), via pointer arithmetic on a frame-local base (`buf + i`), via array/struct/union decay, or via a local whose address is already known to escape the frame (a pointer variable holding `&x`, `&x` stored into a global, etc.). `CALLT` reuses the caller's frame, so a pointer into that frame would dangle the instant the callee's own prologue or body overwrites the slot. This check (`addr_escapes`-driven) carries a known accepted residual: a caller-frame address that reaches an argument only through a path `addr_escapes` does not model can still slip past. It was accepted when TCO was opt-in; with TCO now unconditional it is a property of every compile. The mitigations are in place.
* The `return` statement's implicit cast to the function's own return type must be representation-preserving with respect to the callee's return type — an identity conversion (e.g. `int → int`) or one that leaves the value's register representation unchanged (e.g. `long`/pointer/enum, all read the same 64-bit register). A genuine narrowing or rounding at the return site itself (`return (unsigned char) g(x);`, `return (float) g(x);`) disqualifies the call: `CALLT` hands the callee's raw return value straight to the *original* caller, with no later point at which the narrowing/rounding could be applied.

Mutually-recursive pairs (`A → B → A`) are handled correctly because `CALLT` leaves the caller's return address in place; `B`'s `CALLT` back to `A` reuses `B`'s frame, and so on.

**Interaction with `__builtin_return_address`**

Because `CALLT` unwinds the intermediate frame, `__builtin_return_address(0)` called from inside a tail-called function returns the *original caller's* return address — the frame for the tail-call site no longer exists on the stack.

Example:

```
// test_fn → tail_wrapper → ra_capture  (CALLT used for tail_wrapper → ra_capture)
static void *ra_capture(void) { return __builtin_return_address(0); }
static void *tail_wrapper(void) { return ra_capture(); }  // tail call → CALLT
```

Inside `ra_capture`:
* `__builtin_return_address(0)` → return address back into `test_fn` (the original caller); `tail_wrapper`'s frame has been unwound.
* `__builtin_return_address(1)` → `NULL` (sentinel: `test_fn` is outermost).
* `__builtin_pc_function_name(__builtin_return_address(0))` → `"test_fn"`, not `"tail_wrapper"`.

This is deterministic and by design: `CALLT` semantics guarantee that the callee's stack view is identical to what it would see if the tail-call site had never existed. When a call is *not* tail-call-eligible (e.g. its result is used after the call), the intermediate frame is present and `__builtin_return_address(0)` returns a PC inside it.

More generally, a level-`n` `__builtin_return_address` walk counts only the frames actually present on the stack — it has no way to know how many tail calls were elided along the way. Each `CALLT` in the chain being walked shifts every level below it down by one, so a lookup that would have returned a real return address without TCO can instead hit the outermost sentinel (`NULL`) early, or land on a different frame's return address than a naive frame count would suggest. `tests/test_builtin_return_address_callt.c` asserts this collapsed-frame behavior directly; `tests/test_builtin_return_address_notco.c` covers the same nonzero-level lookups through a helper chain shaped so its calls are *not* tail-call-eligible, for comparison.

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
  Used by the fused indexed load/store opcodes and the checked-access opcodes (`CHKR`, `CHKNT`, `CHKAB`, …).

* **RR** — `[opcode] [rd:8 | rs1:8 | unused:16]`  
  Used by moves, negations, loads, stores, and conversions.

* **RI** — `[opcode] [rd:8 | unused:24] [immediate:64]`  
  Used by `LI3`, `LDA3`, `LTA3`, `LEA3`, `ADDI3`, and branch targets.

* **R** — `[opcode] [rd:8 | unused:56]`  
  Used by single-register operations such as `PSH3`, `POP3`, `JZ3`, `JNZ3`.

The `OPS_X` macro in `src/cccc.h` declares every opcode together with its operand-word count, which is used by the bytecode serialiser, disassembler, and profiler.


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

`main()` is invoked with a synthetic return address of `0`.  `cc_run()` passes `argc` in `REG_A0` and `argv` in `REG_A1`.  When `LEV3` pops this sentinel, it sets `vm->pc = CCCC_INVALID_PC` and the dispatch loop returns `(int)vm->regs[REG_A0]` as the process exit code.

### Debugger Integration

When `CCCC_ENABLE_DEBUGGER` is set, every dispatch checks breakpoints, single-step flags, and step-over / step-out conditions before executing the opcode.  `BTRAP` can also force entry into the interactive REPL.

### Async Signal / Notification Delivery

Two safe points near the top of the dispatch loop poll for asynchronous events and can deliver them between *any* two bytecode instructions, not just at a call boundary: a pending real signal (`_cccc_pending`, set by an async-signal-safe host shim) and a pending `SIGEV_THREAD` notification (`_cccc_sigev_pending`, set by a host notification thread — `<aio.h>`/`<mqueue.h>`). Both jump into guest bytecode (a VM signal handler or a `sigev_notify_function`) the same way `VRAISE` does: push a return address, then set `vm->pc` to the handler.

Because delivery isn't pinned to a call boundary, the normal caller-saved calling-convention ABI doesn't protect the interrupted code's registers the way it does for a synchronous call. Before jumping to the handler, the dispatch loop snapshots the full register file (`regs`/`fregs`/`vregs`) into a `SigFrame` (`src/cccc.h`) — the same structure that already tracks handler-entry/return for `sa_mask`/`SA_NODEFER`/`SA_RESETHAND` enforcement — backed by a lazily-allocated save area (`vm->async_reg_saves`, one slot per `SigFrame`) so a program that never takes a signal pays nothing for it. `cccc_signal_poll_handler_returns`, called at the top of every dispatch iteration once any frame is active, restores the snapshot once the handler genuinely returns (`sp`/`pc` back exactly where the interruption happened) — a handler that `longjmp()`s out instead is left alone, since `SETJMP`/`LONGJMP` (see Non-Local Jumps below) already own `REG_A0` on that path. If every `SigFrame` slot is in use (`CCCC_SIG_FRAME_MAX`, currently 8), a new async delivery is deferred — left pending — rather than delivered without a place to save its snapshot.

`VRAISE` (synchronous `raise()`) does not use this machinery: it always runs at a call boundary, where the ordinary calling convention already protects the caller, so no snapshot is taken.

## Safety Integration

The VM does not rely on external sanitizer libraries.  Instead, the compiler injects safety opcodes at compile time and the interpreter implements the checks inline:

* **Bounds checks** — `CHKB` (pointer/subscript addition) and `CHKBN` (pointer subtraction) at pointer *formation*, plus `CHKD` at the *dereference* site, before every array-subscript or pointer-dereference that the compiler can annotate with a size; resolves interior heap pointers via `vm->sorted_allocs`, not just exact base pointers. `CHKB`/`CHKBN` deliberately allow a pointer to land exactly one past its allocation's end (legal C to *form*); `CHKD` is what still traps if that pointer is actually dereferenced.
* **UAF detection** — `CHKP3` consults `AllocHeader` metadata (magic `0xDEADBEEF`, `freed` bit, generation counter); also resolves interior heap pointers via `vm->sorted_allocs`.
* **Uninitialised reads** — `CHKI` / `MARKI` maintain a per-address hash map of initialised stack slots.
* **Stack canaries** — `ENT3` writes a canary word; `LEV3` validates it before returning.
* **CFI** — A shadow stack mirrors the real stack; `CALL` pushes to both. `CALLT` leaves both the real and shadow return-address entries untouched — the tail call reuses the current frame but does not consume the return address it will eventually return through, so its shadow twin must stay in lockstep too. `LEV3` is what pops (and compares) one entry from each stack, whether it belongs to the frame that pushed it or a tail-called frame further down the chain.
* **Provenance tracking** — `MARKP` records `(origin, base, size)`; `CHKPA` rejects arithmetic that leaves the object.
* **Dangling pointers** — three layers in `CHKP3`. (1) A range check against `[vm->stack_seg, vm->sp)`; since the stack grows downward, an address in that range belongs to a frame that has already returned. (2) Per-frame epoch liveness: `ENT3` assigns each activation a monotonic epoch (`vm->frame_epoch_counter`, tracked in the `vm->frame_epochs` push/pop stack and `vm->live_epochs` membership set); `LEA3` tags the resulting `&local` address with the current frame's epoch in `vm->stack_ptr_epochs` (the stack analogue of `ptr_tags` recording a heap allocation's creation generation) unless `LEA3_NO_RECORD` proves it never escapes; `CHKP3` flags a dereference whose tagged epoch is no longer live, catching the case the range check alone misses — a dangling pointer passed *deeper* into another call and dereferenced there. (3) Interior interval-stabbing: `STKTAG` retains `[base, base+size)` for an escaping array/struct local, tagged with the same epoch, in `vm->stack_intervals`; consulted only when layer 2's exact lookup misses, `CHKP3` resolves an interior address to the max-epoch containing interval (epoch order is recency order, so this is correct even when a live frame has reused a dead frame's address range) and flags it the same way. `LEV3`, the tail-call unwind path, and `LONGJMP` (which can retire several epochs in one jump) all keep `frame_epochs`/`live_epochs` in sync with the real call stack; `stack_intervals` entries are retained rather than pruned on frame death — see `docs/safety.md` for what's caught. This same bookkeeping is a second, independent consumer for `DYNOBJSZ`'s stack-buffer sizing — see `STKTAG`/`DYNOBJSZ` above and `docs/safety.md`. Because a fresh top-level `cc_run_at` call means a fresh guest call stack, it resets `frame_epochs`/`live_epochs`/`stack_ptr_epochs`/`stack_intervals` to empty before running (without touching the monotonic `frame_epoch_counter`) — this also fixed a latent desync: the test framework's assertion-failure path does a host-level `longjmp` that abandons the guest call stack without running the aborted frames' `LEV3`, which previously left stale epochs to trip the *next* call's `LEV3` tripwire.

  **Lazy per-function activation.** `ENT3`'s epoch push is conditional, not universal: only STKTAG and a recorded LEA3 ever read the *current top* epoch, and both are emitted only for a local or parameter whose address escapes its creating frame — so a function whose body proves it has none of those contributes nothing to liveness and skips the push entirely. Codegen tracks, while generating one function's body, whether `emit_lea3_var` emitted `STKTAG` for an escaping aggregate (`ENT3_PUSH_EPOCH_AGG`, bit 31 of the float-mask half — needed in both `--dangling-detection` and dynobjsz-only mode) or a recorded `LEA3` for an escaping scalar (`ENT3_PUSH_EPOCH_SCALAR`, bit 31 of the f32-mask half — needed only under `--dangling-detection`, since a scalar's recorded `LEA3` write is itself gated on that flag), then patches the two bits into the already-emitted `ENT3` masks word once the body is done (mirroring the existing inlined-locals stack-size patch). `op_ENT3_fn` masks these bits back out before treating the rest as real float-param masks, then pushes only when the relevant bit (and mode) is set. Because `vm->frame_epochs.bps[]` is monotonic (deeper calls pushed later, at strictly smaller `bp`), `LEV3`/`CALLT` no longer assume the top entry belongs to the frame currently unwinding — they pop it only when `bps[count-1] == vm->bp`, a self-synchronizing check that is a no-op for a frame that never pushed. `LONGJMP`'s `frame_epoch_truncate_to` was already `bp`-driven and needs no change. The original `chain_depth == frame_epochs.count` tripwire (which assumed every frame pushes) is replaced by an always-on O(1) guard (`bps[count-1] >= vm->bp`) plus, under `#ifndef NDEBUG`, a walk asserting `frame_epochs` is an in-order subsequence of the live saved-`bp` chain.
* **Use-after-scope / use-after-return** — `CHKL`, keyed by a variable's actual runtime address (bp+offset) via `vm->stack_var_active`, populated by `SCOPEIN`/`SCOPEOUT` per activation.

Because every check is guarded by a runtime flag, the same bytecode can run with full safety or with zero overhead simply by changing the active `CCCCFlags` mask.

## Opcode Profiling

The VM can collect dynamic execution statistics:

* **Per-opcode counts** — How many times each opcode was executed.
* **Bigram profile** — Transition frequencies `prev → cur` (stored in a flat `OP_COUNT × OP_COUNT` array).
* **Trigram profile** — Transition frequencies `prev2 → prev → cur` (heap-allocated `OP_COUNT³` array, enabled on demand).

Enable profiling with `--vm-profile` (text report to stderr). Combine it with `--json` to also write the same data as JSON to stdout.  The JSON schema includes `total_opcodes`, `total_bigrams`, per-opcode arrays, and per-bigram arrays with percentages.

Comptime execution (`execute_macro_fn_ex`, `__builtin_comptime_init`) is counted into a separate `VmProfileCounts` bucket while `vm->vm_profile_in_comptime` is set, and emitted as a nested `"comptime"` object with the same keys. Counts accumulate across all top-level `cc_run_at` calls (init entries, `main`, atexit entries); nothing resets them between calls. Under `-c=native`/`-c=generated`/`-m` the JSON goes to stderr, without the run keys.

## Performance Notes

The VM is the runtime for compile-time macro bodies and for VM-only workflows (the safety suite, the debugger, the profiler, quick iteration without a system compiler).  For production code, `-c=native` hands macro-expanded C to `cc` / `clang` / `gcc` and skips the VM entirely, so the interpreter cost only matters for the things that *run on it*.

**VM execution speed is an explicit non-goal.** There are no optimisation
levels or per-pass flags: the VM compiles C to bytecode and interprets it.
`-O<n>` optimises only under `-c=native`, where it is forwarded to the host
cc; in the VM it just defines `__OPTIMIZE__` (`-O1` and above). A fixed set of
always-on lowerings and clean-ups keeps the interpreter from doing needless
work:

1. **Inlined threaded dispatch** — Opcode logic lives at computed-goto labels; there is no function call per instruction.
2. **Fused local load/store** — The common `LEA3 + LDR/STR` pair for local variables is collapsed into a single `LDR_LOCAL_*` / `STR_LOCAL_*` opcode, saving one dispatch and one register-pressure hop per access.
3. **Fused indexed access and multiply-add** — `a[i]`, `a[i + 1]` and `s->arr[i]` become one `LDR_INDEX_*` / `STR_INDEX_*` opcode, and `x + a * b`, `a * K` and floating-point `c + a * b` become `MULADD3` / `MULI3` / `FMADD3`. The floating-point forms round twice, exactly like the multiply and add they replace. Both are skipped under any pointer, type, overflow, uninitialised-read or instrumentation check, whose checks they would bypass (see [Fused Indexed Load / Store](vm-opcodes.md#fused-indexed-load--store)).
4. **Loop locals in registers** — In a function with loops (including one closed by a backward `goto` and a computed-goto dispatcher), the scalar and floating-point locals and parameters used inside a loop (up to four of each kind, hottest first) live in the callee-saved registers `S0`–`S3` and `FS0`–`FS3` instead of their frame slot. An index, base or multiply-add operand that is a promoted local is read from its register directly, with no copy into a temporary, unless another operand of the same instruction may store to a variable first. A local whose address is taken, that a nested function captures, or that has a `cleanup` attribute stays in memory, as do all locals of a function that calls `setjmp`. Promotion is off under any flag that checks or instruments locals (the same set that disables [fused indexed access](vm-opcodes.md#fused-indexed-load--store)) and under an explicit `-g`; items 5 and 6 follow the same rule. The debugger that opens automatically on a crash can show a stale value for a promoted local. `setjmp` saves these registers in the `jmp_buf` and `longjmp` restores them.
5. **Immediate operands and constant folding** — `x + 5` and `x - 5` become `ADDI3`, `x << 3`, `x >> 3` and `x & 0xff` become `SHLI3`, `SHRI3` / `USHRI3` and `ANDI3`, a multiplication by a constant (any 64-bit value) becomes `MULI3`, and a comparison against a constant becomes one `SEQI3` ... `UGEI3` (see [Comparisons (Immediate)](vm-opcodes.md#comparisons-immediate)). An integer expression made only of constants is computed at compile time; a division by zero or a shift count outside the type's width is left to run. A discarded `i++` stores the new value without rebuilding the old one. `&&`, `||` and `!` in an `if`, loop or `?:` condition jump straight to the branch targets; no 0/1 is built and tested again. A `for` or `while` loop with a small scalar condition tests it once before the first iteration and again at the bottom, which branches back into the body, so an iteration runs one branch instead of a test and a `JMP`; a larger condition, or one with a statement expression, keeps the test at the top.
6. **Per-function clean-up** — After each function is generated, a pass over its bytecode removes register copies by forwarding their source, drops definitions nothing reads, writes an operation's result straight into the register a following `MOV3` would copy it to, and deletes a sign or zero extension of a value already in range (including one that only feeds a zero test, and the result of an `&` with a non-negative mask) and a jump to the next instruction. What a register is known to hold (the extensions it satisfies and an interval for its value) carries across branches, switch tables and computed gotos, across a call for promoted locals, and a direct call's result is known to fit its declared return type; a branch on a comparison narrows the intervals of the compared registers along each of its edges, so a loop index below its bound needs no extension after `i++`. An operation whose operands all hold a known value is replaced by its result when that fits in place (`x * 8 + 1` with `x` known becomes one `LI3`; `p += k` with `k` known becomes one `ADDI3`), `x + 0`, `x * 1` and `x & -1` become copies, and a register loaded with 0 is read as the zero register. Copies stay inside a basic block. It models the opcodes in `src/codegen_peep.c`; any other opcode reads every register and ends what the pass knows, so a new opcode is safe until it is listed.
7. **Tail-call elimination** — `return f(args)` patterns that meet eligibility criteria emit `CALLT` instead of `CALL + LEV3`, reducing tail-recursive calls to O(1) stack depth (see [Tail-Call Optimisation](#tail-call-optimisation) above). This one is not a speed knob: deep-recursion correctness depends on it, so it is unconditional.

The dominant cost is the interpreter itself.

## Binding host data

An `extern` global the program declares and uses but never defines can resolve to host storage:

```c
int counter = 3;
cc_register_cdata(&vm, "counter", &counter, sizeof counter);
void *slot = cc_register_cdata_owned(&vm, "limit", sizeof(int), &(int){10});
```

The guest reads and writes `counter` in place. An owned slot is VM-allocated and reset to its initial value at the start of each run. A definition in the program wins, and a declaration larger than the registered size is a compile error. The load honours `--disable-ffi`, `--ffi-allow` and `--ffi-deny` by the object's name, and comptime code needs a [capability class](comptime-policy.md) for it.

## Guide pages

| Page | Covers |
|---|---|
| [vm-opcodes.md](vm-opcodes.md) | Core opcodes |
| [vm-opcodes-safety.md](vm-opcodes-safety.md) | Memory management and safety opcodes |
| [vm-opcodes-misc.md](vm-opcodes-misc.md) | Non-local jumps, FFI and misc opcodes |
| [vm-decimal.md](vm-decimal.md) | Decimal floating-point |
