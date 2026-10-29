# Core Opcodes

Control flow, arithmetic, data movement, floating point and SIMD.

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

Local/parameter offsets (`assign_stack_offsets`, `src/codegen_stmt.c`) are word slots counted down from `bp`. `bp`'s absolute parity varies from call to call (the calling convention pins `bp[+1]` to the return address and `bp[+2..]` to stack-passed arguments), so no constant offset can be aligned. A local whose declared or natural alignment is `> 8` (an `_Alignas(16)+` local, `__int128`/wide `_BitInt`, a 16/32/64-byte vector, a struct with an over-aligned member) instead gets a **padded slot** of `size + (A − 8)` bytes, and its address is `align_up(bp + offset*8, A)`, computed wherever the address is formed. `A` is `cc_effective_align()`'s result, so the same 64-byte cap applies as for globals.

The rounding lives in `LEA3` and `STKTAG` (a 3-bit `log2(A/8)` field, see below), so ENT3, LEV3 and the frame layout are unchanged and dangling-pointer layers 2 and 3 record the exact aligned address the program holds. Codegen must never address an over-aligned local as plain `bp + offset`: use `emit_lea3_var`/`emit_lea3_var_internal`/`emit_lea3_var_plain` (`local_align_log(var)` gives the field), and `is_simple_local_scalar` excludes such locals from the fused `LDR_LOCAL`/`STR_LOCAL` opcodes, so `__int128` and a 16-byte `long double` local use `LEA3` + `LDR`/`STR`. Other places that follow the same rule: `emit_static_chain_var_addr` (nested-function access rounds up from the owner's `bp`), the debugger (`DebugSymbol.align_log`, `debugger_rewrite_locals`), and the scratch pool's `alloc_aligned_temp` (vector-argument temporaries and the by-value struct/union parameter copy). A slot-resident over-aligned scalar parameter (a 16-byte-aligned `long double` on Linux) keeps its positional `ENT3` spill slot; the prologue copies the value into a padded slot. The `CHKI`/`MARKI`/`CHKL`/`MARKR`/`MARKW`/`SCOPEIN` instrumentation keys on the raw slot, which is consistent with itself, so it is unchanged. Compiler-internal wide `_BitInt`/decimal result temporaries are not aligned: they are only reached by runtime helpers, never by user code.

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
| `ADDC` | Checked signed addition; traps when the result leaves the signed width in the width byte |
| `SUBC` | Checked signed subtraction (also unary `-` as `0 - x`); traps outside the width |
| `MULC` | Checked signed multiplication; traps outside the width |
| `DIVC` | Checked signed division; traps on divide-by-zero or `MIN / -1` of the width |
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

### Comparisons (Immediate)

Same format as `ADDI3`: `[op][rd|rs1][imm64]`. Codegen emits them when one side of a comparison is an integer constant, and not under a flag in `CCCC_FUSION_UNSAFE_FLAGS`. The `G` forms are the constant on the left: `5 < x` is `SGTI3 x, 5`.

| Opcode | Description |
|--------|-------------|
| `SEQI3` | `rd = (rs1 == imm)` |
| `SNEI3` | `rd = (rs1 != imm)` |
| `SLTI3` | `rd = (rs1 <  imm)`, signed |
| `SLEI3` | `rd = (rs1 <= imm)`, signed |
| `SGTI3` | `rd = (rs1 >  imm)`, signed |
| `SGEI3` | `rd = (rs1 >= imm)`, signed |
| `ULTI3` | `rd = (rs1 <  imm)`, unsigned 64-bit |
| `ULEI3` | `rd = (rs1 <= imm)`, unsigned 64-bit |
| `UGTI3` | `rd = (rs1 >  imm)`, unsigned 64-bit |
| `UGEI3` | `rd = (rs1 >= imm)`, unsigned 64-bit |

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

#### Wide _BitInt bitfields

A bitfield whose *declared type* is itself a wide `_BitInt` (`T f : W;`,
`T`'s width over 64 bits) is not accessed through either the ordinary
scalar-register shift/mask idiom (the value doesn't fit one register) or a
whole-container load/store CALLF (the *bit packing* within the struct is
compact regardless of container width — `struct { _BitInt(256) f : 193; }`
packs `f` into bits `[0,193)`, though its container spans 32 bytes; the
struct itself is 32 bytes, not just enough to cover those bits, since the layout fix
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
| `LDHA3` | 3 | `regs[rd] =` address of host data object `idx` (`cc_register_cdata`). The FFI flags and the comptime policy apply to the object's name |
| `LTA3` | 3 | `regs[rd] = text_seg + byte_offset` (stores raw offset) |
| `LEA3` | 3 | `regs[rd] = bp + offset` (local variable address). Format: `[LEA3][rd:8\|LEA3_NO_RECORD:1\|align:3\|unused:52][offset:i64]`; a non-zero `align` field (`log2(A/8)`, bits 9-11) rounds the address up to `8<<align` bytes before it is recorded (over-aligned locals). When `CCCC_DANGLING_DETECT` is active, tags the result with the current frame's liveness epoch in `vm->stack_ptr_epochs` — unless `LEA3_NO_RECORD` is set, in which case the tag is skipped. Codegen sets the flag only when a local's address is proven never to escape its creating frame (compiler-internal bookkeeping addresses, or a user local whose escape analysis found no call-argument/return/pointer-store use); see `docs/safety.md`. |
| `STKTAG` | 5 | Tags `[bp+offset, bp+offset+size)` with the current frame's liveness epoch in `vm->stack_intervals`, for interior dangling-pointer resolution and `DYNOBJSZ` stack-buffer sizing. Format: `[STKTAG][align field][offset:i64][size:i64]` (the first word carries the same `LEA3` align field, tagging `[align_up(bp+offset), +size)`). Emitted immediately after the `LEA3` base of an *escaping* array/struct local or parameter (never for scalars, which are already covered exactly by `stack_ptr_epochs`). No-op unless `stack_extents_enabled()` is true — `CCCC_DANGLING_DETECT`, or the program contains a `DYNOBJSZ` opcode (`vm->dynobjsz_present`, set by a one-time incremental scan of the text segment before the first `cc_run_at`) — *and* the current frame is one that pushed an epoch (see the lazy-activation note under "Dangling pointers" below; a function whose body emits `STKTAG` always pushes one). See `docs/safety.md`. |
| `RETADDR` | 3 | `regs[rd] = return address of the nth caller frame`. Walks the saved-bp chain `level` steps from `vm->bp`; each step loads `frame[0]` (saved old-bp). Bounds-checks each frame against the live stack (`vm->sp ≤ frame < vm->initial_sp`). On success, sets `regs[rd] = frame[+1]` (the `Pc` return address stored by `CALL`/`CALLI`). Returns `NULL` (0) past the outermost frame. Format: `[RETADDR][rd:8\|unused:56][level:i64]`. Lowered from `__builtin_return_address(n)`. The returned value (a `Pc`/`uint32_t` offset) can be passed to `__builtin_pc_function_name` or `__builtin_pc_source_location` for symbolization; see the [Source Map API](debugger-source-map.md#source-map-api). |
| `DYNOBJSZ` | 3 | `regs[rd] = runtime byte-size remaining at regs[rs]`. First tries the **heap** path: looks up the containing allocation in `vm->sorted_allocs` (binary search for the largest tracked base address ≤ `ptr`) for pointers into the VM heap (`heap_seg ≤ ptr < heap_end` — `malloc`/`calloc`/`realloc`/`reallocarray` require the VM heap (i.e. not `-V`/`--no-vm-heap`); `alloca`/VLA, which lower to the `ALCA` opcode, always qualify), validates the `AllocHeader` magic (`0xDEADBEEF`) and `freed` flag, and returns `requested_size - offset`. Handles both base pointers (offset 0) and interior pointers (`p + k`) uniformly. On a heap miss, tries the **stack** path: stabs `vm->stack_intervals` (`stack_interval_stab`, same max-epoch resolution `CHKP3` uses) for an escaping fixed-size stack array/struct/union tagged by `STKTAG`, and returns `hi - ptr` only if the matched interval's epoch is still in `vm->live_epochs` — a match whose epoch has retired (frame already returned) is dangling, not resolvable, and falls through. Returns the conservative fallback — `(size_t)-1` (type 0/1) or `0` (type 2/3) — if both paths miss (non-heap and non-stack-tracked, freed, out-of-bounds `requested_size` offset, or dangling). Format: `[DYNOBJSZ][rd:8\|rs:8\|unused:48][type:i64]`. Lowered from `__builtin_dynamic_object_size(ptr, type)` when the pointer's size cannot be resolved at compile time. Using this opcode at all sets `vm->dynobjsz_present`, activating `STKTAG`/epoch bookkeeping independently of `CCCC_DANGLING_DETECT` (see `STKTAG` above) — but only for the functions that actually need it: see the lazy per-function activation note under "Dangling pointers" below. |
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

### Fused Indexed Load / Store

`LDR_INDEX_{B,H,W,D}`, `STR_INDEX_{B,H,W,D}`, `FLDR_INDEX{,_F32}` and `FSTR_INDEX{,_F32}` access `base + index * scale + offset` in one instruction. They are `RRRS`-encoded (`[rd:8|base:8|index:8|scale:8]`) followed by a 64-bit byte offset, and check exactly what the matching plain `LDR_*`/`STR_*` opcode checks. A constant index uses `REG_ZERO` as the index register.

Codegen emits them for `*(base + index * k + c)` with `k` in 1..255 and a constant `c` (`a[i]`, `a[i + 1]`, `s->arr[i]`), unless the base or index contains a call. A constant added to or subtracted from a signed (or 64-bit) index, and the offset of an array member, fold into the offset field. They are not emitted when any of `CCCC_FUSION_UNSAFE_FLAGS` is set (pointer, bounds, UAF, dangling, type, uninitialised, overflow, stack-instrumentation, provenance, invalid-arithmetic and checked-pointer checks), because those checks are emitted around the plain access.

| Opcode | Description |
|--------|-------------|
| `LDR_INDEX_B` / `_H` / `_W` / `_D` | `regs[rd] = *(T*)(regs[base] + regs[index] * scale + offset)`, sign-extended |
| `STR_INDEX_B` / `_H` / `_W` / `_D` | `*(T*)(regs[base] + regs[index] * scale + offset) = regs[rd]` |
| `FLDR_INDEX` / `FLDR_INDEX_F32` | load a double / float into `fregs[rd]` |
| `FSTR_INDEX` / `FSTR_INDEX_F32` | store `fregs[rd]` as a double / float |

### Atomic Operations

Atomic load, store, and read-modify-write opcodes for concurrency. The access width is encoded in a 64-bit operand word (`(size << 1) | is_unsigned`). All atomic opcodes tag the accessed address in the shadow metadata for mixed-access detection.

| Opcode | Operands | Description |
|--------|----------|-------------|
| `ALDR` | 3 | `regs[rd] = *(T*)regs[rs]` — atomic load; `width_enc` in i64 |
| `ASTR` | 3 | `*(T*)regs[rs] = regs[rd]` — atomic store; `width_enc` in i64 |
| `AXCHG` | 2 | Atomic exchange: `old = *(T*)REG_A0; *(T*)REG_A0 = (T)REG_A1; REG_A0 = old`; `width_enc` in i64 |
| `ACAS` | 2 | Atomic compare-and-swap: if `*(T*)REG_A0 == *(T*)REG_A1` then `*(T*)REG_A0 = (T)REG_A2; REG_A0 = 1` else `*(T*)REG_A1 = *(T*)REG_A0; REG_A0 = 0`; `width_enc` in i64 |

`atomic_thread_fence`/`atomic_signal_fence` get no opcode at all — they lower to `ND_FENCE`, which just evaluates the `order` argument (for its side effects) and emits nothing. The GIL is held for the whole `vm_eval` and released only at explicit blocking cfunc points, never between bytecode instructions, so no cross-thread reordering of guest memory accesses is ever observable under the VM; guest signal handlers dispatch at safe points (`cccc_call_guest_callback`) rather than asynchronously, so `atomic_signal_fence` needs no barrier here either. `-c=native` has no GIL and gets a real `__atomic_thread_fence`/`__atomic_signal_fence` instead.

### Floating-Point Operations

All `F*` opcodes operate on `fregs[]`.  Comparisons write a boolean into an integer register.

The `FMADD3`/`FMSUB3`/`FNMSUB3` family replaces a multiply feeding an add or subtract of the same type. The product is rounded before the add, so the result is bit-identical to `FMUL3` followed by `FADD3`/`FSUB3`; it is not a single-rounding fused multiply-add (call `fma()` for that). The integer `MULADD3` forms wrap like the multiply and add they replace. Codegen emits all of them under the same flag condition as the indexed opcodes.

| Opcode | Description |
|--------|-------------|
| `FADD3` | `fregs[rd] = fregs[rs1] + fregs[rs2]` (f64) |
| `FSUB3` | `fregs[rd] = fregs[rs1] - fregs[rs2]` (f64) |
| `FMUL3` | `fregs[rd] = fregs[rs1] * fregs[rs2]` (f64) |
| `FDIV3` | `fregs[rd] = fregs[rs1] / fregs[rs2]` (f64; IEEE-754 semantics -- finite/0.0 is a correctly-signed infinity, 0.0/0.0 is NaN, neither is UB; traps only under the opt-in `--trap-fp-divzero`) |
| `FMOV3` | `fregs[rd] = fregs[rs1]` (f64) |
| `FNEG3` | `fregs[rd] = -fregs[rs1]` (f64) |
| `FMADD3` / `FMADD3_F32` | `fregs[rd] = fregs[rs1] + fregs[rs2] * fregs[rs3]` |
| `FMSUB3` / `FMSUB3_F32` | `fregs[rd] = fregs[rs2] * fregs[rs3] - fregs[rs1]` |
| `FNMSUB3` / `FNMSUB3_F32` | `fregs[rd] = fregs[rs1] - fregs[rs2] * fregs[rs3]` |
| `FEQ3` … `FGE3` | f64 comparisons |
| `FADD3_F32` … `FDIV3_F32` | f32 arithmetic |
| `FNEG3_F32` | f32 negation |
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

Vector opcodes operating on `vregs[]` (see [Register File](vm.md#register-file)). Introduced for GNU `__attribute__((vector_size(N)))` support (`coverage.md`, GNU Extensions) and reserved as the shared substrate for a future interpreter autovectorizer. **128-, 256-, and 512-bit vectors are supported** (16/32/64-byte total size) — e.g. `v4f32`/`v8f32`/`v16f32`, `v2f64`/`v4f64`/`v8f64`, and the corresponding integer lane layouts at each width; any other width is rejected with a diagnostic. As with `F*` vs `F*_F32`, the opcode — not the register — carries the active lane *type*; the instruction operand's "scale" byte carries the active *width* (`VLDR`/`VSTR`: byte count) or *lane count* (every other op), so one opcode per element family now serves all three widths (opcode names no longer carry an `X2`/`X4`/... lane-count suffix). `VLDR`/`VSTR` move the operand-carried byte count with a byte-copy (no alignment fault on unaligned addresses); arithmetic reads/writes the lane view named by the opcode, looping up to the operand-carried lane count.

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

**Operand encoding.** Every vector opcode is `RRRS`-encoded (`ENCODE_RRRS`/`DECODE_RRRS`, `internal.h`) — `[rd:8|rs1:8|rs2:8|scale:8]`. Two-register ops (`VLDR`/`VSTR`/`VSPLAT_*`/`VNEG_*`/`VNOT`/`VCVT_*`) leave `rs2` unread and use `scale` for the byte width (`VLDR`/`VSTR`) or lane count (everything else), the same way `VEXTRACT_*`/`VINSERT_*` already ignore `rs2` and use `scale` for the lane index. This keeps a vector value flowing through a **single** `vregs[]` index end to end (the same contract `gen_expr`'s `dest_reg` and the temp-register allocator use for every other value kind) — widening to 256/512-bit only grows the register and adds an operand field, it does not tile multiple registers per value.

**Per-lane integer division/modulo trapping (`VDIV_I*`/`VMOD_I*`).** Mirrors scalar `DIVC`'s trapping policy (not `DIV3`'s non-trapping `LLONG_MIN` return) — a zero divisor in *any* lane prints a `DIVISION BY ZERO` diagnostic naming the lane and aborts; `MIN/-1` overflow in any lane traps the same way for `VDIV_*` (`VMOD_*` returns `0` for that lane instead, matching `a % -1 == 0` for all `a`). This makes vector integer division/modulo deliberately stricter than default-build scalar `/`.

**Frontend lowering (codegen.c `gen_vector_expr`).** A vector local/global lives in a memory slot sized to its own byte width (16/32/64), exactly like a small struct — no frame-layout changes were needed beyond sizing the slot correctly (`assign_stack_offsets`/`var_stack_slots`). The *value* of a vector expression flows through a `vregs[]` index in `dest_reg`, mirroring how float-typed expressions flow through `fregs[]` even though float locals also live in memory: `VLDR`/`VSTR` move between the slot and a vreg around each operation, carrying the value's byte width in the operand. `v[i]` subscript is **not** lowered through `VEXTRACT`/`VINSERT` — it is intercepted at parse time (before array-decay pointer arithmetic, which would misinterpret the vector's `base` field) and rewritten to `*(elem_ty*)(&v + i)`, reusing ordinary scalar load/store and supporting a runtime-variable index for free. `VEXTRACT`/`VINSERT` remain reserved for the autovectorizer (horizontal reductions), though `__builtin_shuffle`'s lowering (both the constant- and runtime-mask forms) also reuses this same subscript machinery (see below) rather than `VEXTRACT`/`VINSERT` directly.

**`__builtin_shuffle` (constant-mask and runtime-mask forms).** CCCC accepts `__builtin_shuffle(v, {i0,...,iN-1})` (1-vector permute) or `__builtin_shuffle(v1, v2, {i0,...,iN-1})` (2-vector blend) with a bare, compile-time-constant brace list mask (CCCC's original constant-mask form, closer to clang's `__builtin_shufflevector` than the syntax below), as well as `__builtin_shuffle(v, mask)` / `__builtin_shuffle(v1, v2, mask)` with `mask` an ordinary integer-vector expression — a named local, function parameter, or runtime-computed value — matching upstream GCC's general vector-typed mask argument. A `(vTYPE){...}` compound literal mask is rejected as ambiguous with the two-vector form's second argument (both start with `(`); a bare `{` unambiguously marks a constant mask, so the parser distinguishes the forms with one token of lookahead there, and (since the 1-vector runtime form and the 2-vector form both start with a non-brace expression) by checking whether a further `,` follows the second parsed argument.

No new opcode for either form: the parser materializes the source vector(s) (and, for the runtime form, the mask) into hidden locals (so side effects run once), then builds a chain of per-lane scalar reads/writes through the same vector-subscript lvalue lowering `v[i]` uses — this per-lane approach is width-agnostic and works unchanged at 256/512-bit, and works with a runtime index exactly as it does with a constant one. The runtime mask must be an integer vector with the same lane count and element byte width as the shuffled vector (validated at parse time, rejected with a diagnostic otherwise). The two forms diverge on out-of-range indices: the constant form range-checks every index at compile time and rejects an out-of-range one outright, while the runtime form can't be checked until run time, so each index is taken modulo the lane count (1-vector) or twice the lane count (2-vector) before use — matching GCC's documented wraparound semantics.

**By-value function args/returns (widened).** Reuses the struct-by-value call ABI rather than a register (FReg/GReg) convention, since a vector doesn't fit an 8-byte arg slot. Argument: the caller materializes the vector's value (a `vregs[]` register) into a fresh frame scratch slot sized to the argument's own byte width via `VSTR`, then passes that slot's address in the integer arg register — a caller-side copy, unlike a struct-by-value arg's own copy, which happens in the *callee's* prologue instead; both end up giving true by-value semantics, just on opposite sides of the call. Return: the callee requests a buffer from the `RETBUF` rotating pool (1024 bytes, comfortably covering the 64-byte maximum), `VSTR`s the result into it, and returns the buffer address; the caller `VLDR`s out of it. The callee reads a vector parameter the same way a struct parameter is read — the param's stack slot holds a pointer to the value, not the value itself (`gen_addr`'s param-slot dereference). Not supported through the native FFI marshalling path (int/float-only slot classification), a variadic `...` parameter (register spilling for `va_arg` is int/float-only), or a GNU/Apple block invocation (no by-memory ABI for aggregates at all) — each rejected at compile time. Tail-call elimination (`can_emit_tail_call`) excludes any vector-returning call and any call with a vector argument, since `CALLT` reuses the caller's frame and both the RETBUF buffer and the argument scratch slot are frame-local.

### Type Conversion

| Opcode | Description |
|--------|-------------|
| `SX1` | Sign-extend 1 byte → 64 bits |
| `SX2` | Sign-extend 2 bytes → 64 bits |
| `SX4` | Sign-extend 4 bytes → 64 bits |
| `ZX1` | Zero-extend 1 byte → 64 bits |
| `ZX2` | Zero-extend 2 bytes → 64 bits |
| `ZX4` | Zero-extend 4 bytes → 64 bits |
