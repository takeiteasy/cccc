/*
 CCCC: Comprehensiev C Compensation Compiler

 Copyright (C) 2025 George Watson

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "./codegen_internal.h"

// ========== Emit Helpers ==========

void emit_word(VirtualMachine *vm, InstrWord word) {
    if (!vm || !vm->text_seg)
        error("codegen: text segment not initialized");
    if (vm->compiler.text_limit && vm->text_ptr + 1 >= vm->compiler.text_limit)
        error("internal: wrapper overflowed its reclaimed text");
    if (vm->text_ptr + 1 >= (Pc)(vm->text_committed / sizeof(InstrWord))) {
        if (vm_text_ensure_count(vm, vm->text_ptr + 2) != 0)
            error("codegen: text segment overflow (limit: %d instructions)",
                  vm->poolsize_max);
    }
    vm->text_seg[++vm->text_ptr] = word;
}

Pc emit_word_ptr(VirtualMachine *vm) {
    if (!vm || !vm->text_seg)
        error("codegen: text segment not initialized");
    if (vm->compiler.text_limit && vm->text_ptr + 1 >= vm->compiler.text_limit)
        error("internal: wrapper overflowed its reclaimed text");
    if (vm->text_ptr + 1 >= (Pc)(vm->text_committed / sizeof(InstrWord))) {
        if (vm_text_ensure_count(vm, vm->text_ptr + 2) != 0)
            error("codegen: text segment overflow (limit: %d instructions)",
                  vm->poolsize_max);
    }
    return ++vm->text_ptr;
}

Pc emit_i64(VirtualMachine *vm, long long val) {
    Pc loc            = emit_word_ptr(vm);
    vm->text_seg[loc] = cc_i64_lo(val);
    emit_word(vm, cc_i64_hi(val));
    return loc;
}

void check_data_capacity(VirtualMachine *vm, long long needed) {
    if (vm_data_ensure(vm, needed) != 0)
        error("codegen: data segment overflow (limit: %d bytes)",
              vm->poolsize_max);
}

// #1136: effective alignment of an object placed in the data segment / TLS
// template. An explicit _Alignas (Obj.align) overrides the type's own
// alignment (ty_align); floored at 8 so every object that was already
// (correctly) placed on an 8-byte boundary keeps that exact placement --
// this also guards against obj_align/ty_align == 0 (extern/incomplete/
// tentative decls can plausibly carry either as unset), which would
// otherwise collapse the round-up mask to 0 and clobber offset 0. Capped at
// CCCC_MAX_DATA_ALIGN (64), the widest alignment any *type* requests today
// (64-byte vectors, #722). An explicit _Alignas(N) with N > 64 is accepted
// by the parser but only gets 64-byte placement from this allocator, not
// the full N -- a documented limitation (see docs/vm.md), not a silent
// truncation bug: the object is still more correctly placed than the
// pre-#1136 hardcoded 8-byte rounding.
int cc_effective_align(int obj_align, int ty_align) {
    int a = obj_align > ty_align ? obj_align : ty_align;
    if (a < 8)
        a = 8;
    if (a > CCCC_MAX_DATA_ALIGN)
        a = CCCC_MAX_DATA_ALIGN;
    // A well-formed _Alignas is always a power of two (C11 6.7.5p6), but
    // parse_types.c's DK_ALIGNAS arm (src/parse_types.c) never validates
    // that -- a pre-existing parser gap, not introduced here, out of this
    // ticket's scope to fix. Round up to the next power of two defensively
    // rather than feeding a non-power-of-two mask into the round-up below:
    // an odd 'a' would compute a wrong mask and could under-align (or, via
    // sign/shift edge cases, corrupt) the very placement this function
    // exists to get right, silently turning a pre-existing parser gap into
    // a data-segment correctness bug.
    if (a & (a - 1)) {
        int p = 8;
        while (p < a)
            p <<= 1;
        a = p;
    }
    return a;
}

// Grow tls_template to hold at least `needed` bytes.
void check_tls_capacity(VirtualMachine *vm, size_t needed) {
    if (needed <= vm->tls_template_cap)
        return;
    size_t new_cap = vm->tls_template_cap ? vm->tls_template_cap * 2 : 256;
    while (new_cap < needed)
        new_cap *= 2;
    char *p = realloc(vm->tls_template, new_cap);
    if (!p)
        error("codegen: TLS template allocation failed");
    if (new_cap > vm->tls_template_cap)
        memset(p + vm->tls_template_cap, 0, new_cap - vm->tls_template_cap);
    vm->tls_template     = p;
    vm->tls_template_cap = new_cap;
}

void emit(VirtualMachine *vm, int instruction) {
    emit_word(vm, instruction);
}

void emit_with_arg(VirtualMachine *vm, int instruction, long long arg) {
    emit_word(vm, instruction);
    emit_i64(vm, arg);
}

// 3-register ops: [OP] [rd:8|rs1:8|rs2:8|unused:40]
void emit_rrr(VirtualMachine *vm, int op, int rd, int rs1, int rs2) {
    emit_word(vm, op);
    emit_word(vm, ENCODE_RRR(rd, rs1, rs2));
}

// 2-register ops: [OP] [rd:8|rs1:8|unused:48]
void emit_rr(VirtualMachine *vm, int op, int rd, int rs1) {
    emit_word(vm, op);
    emit_word(vm, ENCODE_RR(rd, rs1));
}

// 3-register + 8-bit "scale" ops: [OP] [rd:8|rs1:8|rs2:8|scale:8|unused:32].
// Used by the wide-vector opcodes (#722) to carry a runtime lane count/byte
// width alongside up to 3 register operands -- see the SIMD opcode block in
// cccc.h for which family uses which field.
void emit_rrrs(VirtualMachine *vm, int op, int rd, int rs1, int rs2,
               int scale) {
    emit_word(vm, op);
    emit_word(vm, ENCODE_RRRS(rd, rs1, rs2, scale));
}

// 2-register + 8-bit "scale" ops: rs2 is unused (0) -- the vector ops that
// only take one source register (VLDR/VSTR/VSPLAT/VNEG/VNOT/VCVT) still need
// the scale byte, so they reuse the RRRS encoding with rs2 left unread by
// the VM handler (mirrors how VEXTRACT_*/VINSERT_* already ignore rs2).
void emit_rrs(VirtualMachine *vm, int op, int rd, int rs1, int scale) {
    emit_word(vm, op);
    emit_word(vm, ENCODE_RRRS(rd, rs1, 0, scale));
}

// 1-register + immediate: [OP] [rd:8|unused:24] [imm:64]
Pc emit_ri(VirtualMachine *vm, int op, int rd, long long imm) {
    emit_word(vm, op);
    emit_word(vm, ENCODE_R(rd));
    return emit_i64(vm, imm);
}

// Register + register + immediate: [OP] [rd:8|rs:8|unused:16] [imm:64]
Pc emit_rri(VirtualMachine *vm, int op, int rd, int rs, long long imm) {
    emit_word(vm, op);
    emit_word(vm, ENCODE_RR(rd, rs));
    return emit_i64(vm, imm);
}

Pc emit_rrrs_i(VirtualMachine *vm, int op, int rd, int base, int index,
               int scale, long long offset) {
    emit_word(vm, op);
    emit_word(vm, ENCODE_RRRS(rd, base, index, scale));
    return emit_i64(vm, offset);
}

// Float 3-register ops
void emit_frrr(VirtualMachine *vm, int op, int rd, int rs1, int rs2) {
    emit_word(vm, op);
    emit_word(vm, ENCODE_RRR(rd, rs1, rs2));
}

// Float 2-register ops
void emit_frr(VirtualMachine *vm, int op, int rd, int rs1) {
    emit_word(vm, op);
    emit_word(vm, ENCODE_RR(rd, rs1));
}

// ========== Specific Emit Helpers ==========

// LI3: rd = immediate
Pc emit_li3(VirtualMachine *vm, int rd, long long imm) {
    return emit_ri(vm, LI3, rd, imm);
}

Pc emit_lda3(VirtualMachine *vm, int rd, long long offset) {
    return emit_ri(vm, LDA3, rd, offset);
}

Pc emit_ldtls3(VirtualMachine *vm, int rd, long long offset) {
    return emit_ri(vm, LDTLS3, rd, offset);
}

Pc emit_lta3(VirtualMachine *vm, int rd, long long offset) {
    return emit_ri(vm, LTA3, rd, offset);
}

// LEA3: rd = bp + offset. `skip_record` sets LEA3_NO_RECORD (#676), telling
// op_LEA3_fn to skip its vm->stack_ptr_epochs write for this address -- pass
// true only when the result is proven never to escape its creating frame
// (see docs/safety.md and the mark_addr_escapes pass in parse.c).
static Pc emit_lea3_ex(VirtualMachine *vm, int rd, long long offset,
                       bool skip_record, int align_log) {
    emit_word(vm, LEA3);
    emit_word(vm, ENCODE_R(rd) | (skip_record ? LEA3_NO_RECORD : 0) |
                      ((InstrWord)align_log << LEA3_ALIGN_SHIFT));
    return emit_i64(vm, offset);
}

// log2(align/8) for an alignment already normalised by cc_effective_align.
int align_log_of(int align) {
    int log = 0;
    for (int n = align / 8; n > 1; n >>= 1)
        log++;
    return log;
}

// True when a param's frame slot holds a pointer to the value rather than the
// value itself (see gen_addr's by-pointer param arm).
bool param_slot_holds_pointer(Type *ty) {
    return ty->kind == TY_STRUCT || ty->kind == TY_UNION ||
           ty->kind == TY_VECTOR || is_wide_bitint(ty) || is_decimal(ty);
}

// Alignment field for a user local's address (#1137); 0 when 8-byte slots
// already suffice. By-pointer params are excluded (their pointee is aligned
// via the prologue copy); a slot-resident over-aligned scalar param gets a
// padded slot that gen_function's prologue copies the spilled value into.
int local_align_log(Obj *var) {
    if (var->is_block_var || var->ty->kind == TY_VLA)
        return 0;
    if (var->is_param && param_slot_holds_pointer(var->ty))
        return 0;
    return align_log_of(cc_effective_align(var->align, var->ty->align));
}

// LEA3: rd = bp + offset. Default: recorded (safe) -- see emit_lea3_ex.
Pc emit_lea3(VirtualMachine *vm, int rd, long long offset) {
    return emit_lea3_ex(vm, rd, offset, false, 0);
}

// STKTAG: tag [bp+offset, bp+offset+size) with the current frame's epoch,
// for interior dangling-pointer resolution (#675). See docs/safety.md.
Pc emit_stktag(VirtualMachine *vm, long long offset, long long size,
               int align_log) {
    emit_word(vm, STKTAG);
    emit_word(vm, (InstrWord)align_log << LEA3_ALIGN_SHIFT); // align field only
    emit_i64(vm, offset);
    return emit_i64(vm, size);
}

// LEA3 for a local Obj's own base address, e.g. `&var`/array-or-struct
// base materialization -- skips recording iff #676's escape analysis
// proved `var`'s address never escapes its creating frame. When `var` is
// an escaping array/struct/union, also emits STKTAG (#675) so an interior
// pointer derived from this base at a runtime offset (e.g. &arr[i] for
// non-constant i, which never itself passes through LEA3 as a single
// recorded address) can still be resolved back to this base's epoch at
// CHKP3 time. Scalars need no interior resolution -- their one address is
// already covered exactly by stack_ptr_epochs.
//
// Also records, on vm->compiler, whether the *current function* needs its
// own frame epoch pushed (#703): STKTAG for an escaping aggregate, or a
// recorded LEA3 for an escaping scalar. gen_function patches these into the
// ENT3 masks word once the body is done. Deliberately keyed off what this
// function actually emits (params included, via the same emit_lea3_var
// path) rather than a fn->locals-only pre-scan, which would miss an
// escaping aggregate *parameter*.
Pc emit_lea3_var(VirtualMachine *vm, int rd, Obj *var) {
    bool escaping_agg = var->addr_escapes && (var->ty->kind == TY_ARRAY ||
                                              var->ty->kind == TY_STRUCT ||
                                              var->ty->kind == TY_UNION);
    // A TY_VECTOR local is always STKTAG'd, regardless of what escape
    // analysis (mark_addr_escapes, #676) proved (#727). Structs/arrays only
    // need STKTAG when addr_escapes is set because a *non*-escaping
    // struct/array is never read back through its own address -- member
    // access on a value that can't escape has no reason to re-derive an
    // lvalue. Vectors break that assumption: element access (`v[i]`) always
    // lowers through gen_addr()+VLDR (this file's #714/#722 comment: "a
    // vector local ... lives in a memory slot ... exactly like a small
    // struct"), even for a vector proven never to leave its frame. Without
    // STKTAG, only the LEA3-recorded base offset is coverable at all, and
    // for a non-escaping vector even the base isn't recorded (skip_record is
    // still true below, matching struct/array's own policy) -- so *every*
    // lane read of a non-escaping vector falls to stack_interval_stab.
    // STKTAG-ing the whole vector extent with the current (live) frame's
    // epoch on every access ensures stack_interval_stab's prefer-live
    // resolution always has this vector's own live range to prefer over any
    // dead sibling frame's STKTAG range that happens to physically overlap
    // it (e.g. a prior variadic call's own dead va_list).
    //
    // Deliberately does NOT also force skip_record=false (exact
    // stack_ptr_epochs recording) for a non-escaping vector: exact-recording
    // the base would tag that one absolute address with this frame's epoch,
    // and once this frame returns that tag goes stale -- unlike the STKTAG
    // interval, a stale *exact* tag has no prefer-live protection (layer 2
    // is a single last-write-wins hashmap slot, not a set of overlapping
    // candidates), so an unrelated dereference by any later sibling frame
    // that happens to reuse the same physical address (e.g. a stack-spilled
    // variadic argument slot, which is address-passed rather than declared
    // as a var) would spuriously collide with it. The STKTAG interval alone
    // -- which layer 3 resolves soundly via prefer-live -- is enough to
    // cover every offset of this vector, including offset 0.
    bool vector_agg = var->ty->kind == TY_VECTOR;
    int  align_log  = local_align_log(var);
    Pc   pc = emit_lea3_ex(vm, rd, var->offset, !var->addr_escapes, align_log);
    if (escaping_agg || vector_agg) {
        emit_stktag(vm, var->offset, var->ty->size, align_log);
        vm->compiler.frame_has_esc_agg = true;
    } else if (var->addr_escapes) {
        vm->compiler.frame_has_esc_scalar = true;
    }
    return pc;
}

// LEA3 for compiler-internal bookkeeping addresses (static links, block
// descriptors, closure captures, memcpy/cleanup scratch) that never
// correspond to a user-visible `&local` and are proven, by construction,
// never to escape their creating frame -- always skip recording (#676).
Pc emit_lea3_internal(VirtualMachine *vm, int rd, long long offset) {
    return emit_lea3_ex(vm, rd, offset, true, 0);
}

Pc emit_lea3_var_plain(VirtualMachine *vm, int rd, Obj *var) {
    return emit_lea3_ex(vm, rd, var->offset, false, local_align_log(var));
}

Pc emit_lea3_var_internal(VirtualMachine *vm, int rd, Obj *var) {
    return emit_lea3_ex(vm, rd, var->offset, true, local_align_log(var));
}

Pc emit_lea3_aligned(VirtualMachine *vm, int rd, long long offset,
                     int align_log, bool record) {
    return emit_lea3_ex(vm, rd, offset, !record, align_log);
}

// ADDI3: rd = rs + immediate
Pc emit_addi3(VirtualMachine *vm, int rd, int rs, long long imm) {
    return emit_rri(vm, ADDI3, rd, rs, imm);
}

// MOV3: rd = rs
void emit_mov3(VirtualMachine *vm, int rd, int rs) {
    emit_rrr(vm, MOV3, rd, rs, 0);
}

void emit_fmov3(VirtualMachine *vm, int rd, int rs) {
    emit_frr(vm, FMOV3, rd, rs);
}

void emit_fround_f32(VirtualMachine *vm, int rd, int rs) {
    emit_frr(vm, FROUND_F32, rd, rs);
}

int fop_for_type(Type *ty, int f64_op) {
    if (!ty || ty->kind != TY_FLOAT)
        return f64_op;
    switch (f64_op) {
        case FADD3:
            return FADD3_F32;
        case FSUB3:
            return FSUB3_F32;
        case FMUL3:
            return FMUL3_F32;
        case FDIV3:
            return FDIV3_F32;
        case FNEG3:
            return FNEG3_F32;
        case FEQ3:
            return FEQ3_F32;
        case FNE3:
            return FNE3_F32;
        case FLT3:
            return FLT3_F32;
        case FLE3:
            return FLE3_F32;
        case FGT3:
            return FGT3_F32;
        case FGE3:
            return FGE3_F32;
        case I2F3:
            return I2F3_F32;
        case F2I3:
            return F2I3_F32;
        case U2F3:
            return U2F3_F32;
        case F2U3:
            return F2U3_F32;
        case FR2R:
            return FR2R_F32;
        case R2FR:
            return R2FR_F32;
        default:
            return f64_op;
    }
}

// #780: an unsigned 64-bit integer type needs the dedicated U2F3/F2U3
// opcode pair, not I2F3/F2I3 -- the latter treat the register as a signed
// 64-bit value, which is wrong for both directions once the value's high
// bit is set (float->int saturates against the wrong range; int->float
// reads a negative value). Narrower unsigned types are unaffected: they're
// already zero-extended in the register (so I2F3 is correct) and F2I3's
// signed saturation covers their full range (so a same-width ZX/SX after
// F2I3 is correct too) -- only size-8 needs the new opcodes.
bool is_u64_int(Type *ty) {
    return ty && is_integer(ty) && ty->is_unsigned && ty->size == 8;
}

// FREG_A0..A7 alias REG_A0..A7 by raw register number (regs[] and fregs[] are
// separate storage, but share index numbers). Many gen_expr paths legitimately
// reuse their destination register *number* as an integer scratch while
// producing a float result -- deref/member address computation, int->float
// cast source, ternary condition -- so evaluating a float call argument
// directly into FREG_A0+i would let that integer scratch clobber a
// live REG_A0+i already holding a marshalled argument (e.g. printf's format
// pointer). Evaluate into a caller-saved temp-numbered float register instead:
// its aliased integer slot is a free temp, never a live argument register.
// Caller moves the result out (FR2R/emit_fmov3) and frees the temp. (#712)
int gen_flonum_arg_to_scratch(VirtualMachine *vm, Node *arg) {
    int r = alloc_temp_reg();
    gen_expr(vm, arg, r);
    return r;
}

// Load operations based on type

bool is_zero_size_aggregate(Type *ty) {
    return ty && ty->size == 0 &&
           (ty->kind == TY_STRUCT || ty->kind == TY_UNION);
}

// True for _BitInt(N) with N > 64 — multi-word, address-based storage.
bool is_wide_bitint(Type *ty) {
    return ty && ty->kind == TY_BITINT && ty->bit_width > 64;
}

// #994: true when a block capture of this type needs a multi-word MCPY
// copy into its descriptor slot rather than a single 8-byte load+store.
// Any aggregate kind is routed through MCPY even when size <= 8 -- an
// 8-byte LDR_D of a smaller struct/array sitting at the very end of an
// allocation can still trip CHKD/bounds, which a same-size MCPY does not
// (it copies exactly ty->size bytes). is_block_var/TY_VLA captures never
// reach this check: both always copy a fixed 8-byte pointer (the heap
// box, or the VLA's own placeholder pointer) regardless of the pointee's
// real size -- see the caller in gen_expr's ND_BLOCK_LITERAL case.
bool block_capture_needs_mcpy(Type *ty) {
    if (ty->size > 8)
        return true;
    switch (ty->kind) {
        case TY_STRUCT:
        case TY_UNION:
        case TY_ARRAY:
        case TY_COMPLEX:
        case TY_VECTOR:
            return true;
        default:
            return is_wide_bitint(ty) || is_decimal(ty);
    }
}

// Emit CALLF for a named wide-bitint helper with `nargs` integer args already
// loaded into REG_A0..REG_A{nargs-1}.  Returns false if function not found.
bool emit_wide_helper(VirtualMachine *vm, const char *name, int nargs) {
    int ffi_idx = find_ffi_function(vm, name);
    if (ffi_idx < 0) {
        // Should never happen if wide_bitint.c is compiled in.
        error("wide _BitInt runtime helper '%s' not registered", name);
        return false;
    }
    emit(vm, CALLF);
    emit_word(vm, ffi_idx);
    emit_word(vm, nargs);
    emit_i64(vm, 0); // double_arg_mask
    emit_i64(vm, 0); // float_arg_mask
    reset_temp_regs();
    return true;
}

// Emit a dedicated WIDE_* opcode (args already loaded into REG_A0..A5) for
// the hot wide-bitint ops (#456) — same args/clobber contract as
// emit_wide_helper's CALLF, just without the FFI marshalling overhead.
void emit_wide_op(VirtualMachine *vm, int op) {
    emit(vm, op);
}

// Allocate a fresh stack slot for a wide _BitInt intermediate result.
// Returns the bp-relative offset (negative).  words = number of 64-bit words.
// Wide _BitInt storage is written via a raw pointer (CALLF helper or a
// WIDE_* opcode), never through STR_LOCAL, so no MARKI/MARKW liveness mark
// is emitted for it. This is intentional: wide _BitInt is address-based like
// structs/unions, so the ND_VAR read-side instrumentation guard excludes it
// (mirroring TY_STRUCT/UNION), keeping both sides symmetric. Fixed in #457.
long long alloc_wide_bitint_temp(VirtualMachine *vm, int words) {
    vm->compiler.ent3_extra_stack += words;
    return -(long long)(vm->compiler.ent3_base_stack +
                        vm->compiler.ent3_extra_stack);
}

// Scratch slot whose address is rounded up to 8<<align_log (#1137): pads by
// the worst-case shift. Reach it with emit_lea3_aligned (+ emit_stktag) using
// the same align_log.
long long alloc_aligned_temp(VirtualMachine *vm, int bytes, int align_log) {
    int words = (bytes + 7) / 8 + ((8 << align_log) - 8) / 8;
    return alloc_wide_bitint_temp(vm, words);
}

// Allocate a fresh stack slot for a _Decimal32/64/128 intermediate result
// (#402). Reuses alloc_wide_bitint_temp's per-function scratch pool,
// rounding up to whole 64-bit words (4 bytes for _Decimal32 still costs a
// full word -- same tradeoff wide _BitInt already makes for narrow widths).
// Same address-based rationale applies: written via DADD/DSUB/... or the
// BID shim, never through STR_LOCAL, so no MARKI/MARKW mark is emitted and
// the ND_VAR read-side uninit guard must exclude decimal (see is_decimal
// checks alongside is_wide_bitint below).
long long alloc_decimal_temp(VirtualMachine *vm, int bytes) {
    return alloc_wide_bitint_temp(vm, (bytes + 7) / 8);
}

// Materialize a vector-typed argument into a fresh frame scratch slot sized
// to the argument's own width and load its address into addr_reg, ready to
// pass like a struct-by-value arg (#714). Unlike a struct arg -- whose
// address is the caller's own addressable storage -- a vector expression is
// a value living in a vregs[] register (gen_vector_expr), so it must be
// copied out to memory before its address can be handed to the callee.
// Reuses alloc_wide_bitint_temp's per-function scratch allocator (same ENT3
// extra-stack pool as wide _BitInt temporaries), sized to arg->ty->size/8
// words -- 2/4/8 words for the 16/32/64-byte vectors #722 supports.
void gen_vector_arg_ptr(VirtualMachine *vm, Node *arg, int addr_reg) {
    int v = alloc_temp_reg();
    gen_expr(vm, arg, v); // vector value -> vregs[v]
    mark_temp_reg_used(v);
    int       bytes     = arg->ty->size;
    int       align_log = align_log_of(cc_effective_align(0, arg->ty->align));
    long long off       = alloc_aligned_temp(vm, bytes, align_log);
    // address escapes to the callee -- record it
    emit_lea3_aligned(vm, addr_reg, off, align_log, true);
    if (vm->flags & CCCC_POINTER_CHECKS)
        emit_rr(vm, CHKP3, addr_reg, 0);
    emit_rrs(vm, VSTR, v, addr_reg, bytes);
    free_temp_reg(v);
}

// Pass a _Decimal32/64/128 variadic argument by pointer to a caller-frame
// scratch copy, mirroring gen_vector_arg_ptr just above (#714/#721) --
// exactly one 8-byte slot regardless of the value's 4/8/16 byte width, which
// <stdarg.h>'s va_arg dereferences via __builtin_classify_type. A FIXED
// decimal param doesn't need this: gen_expr already yields the address of
// the source value directly (decimal is address-based throughout codegen --
// see alloc_decimal_temp's comment), and the callee only ever reads back
// exactly the declared width. A variadic reader, though, picks its load
// width from the %Hf/%Df/%DDf modifier in a printf-style format string,
// which the compiler cannot generally correlate with the argument's actual
// width (validate_format_call only catches a literal-format-string mismatch,
// and only warns). So this always allocates and copies the full 16 bytes,
// independent of arg->ty->size: a width mismatch then reads uninitialized-
// but-in-bounds bytes from a fully-sized scratch object, never past it.
void gen_decimal_arg_ptr(VirtualMachine *vm, Node *arg, int addr_reg) {
    int r_src = alloc_temp_reg();
    gen_expr(vm, arg, r_src);       // decimal value -> address of source
    mark_temp_reg_used(r_src);
    long long off =
        alloc_decimal_temp(vm, 16); // always full width, not arg->ty->size
    emit_lea3(vm, addr_reg, off); // address escapes to the callee -- record it
    if (vm->flags & CCCC_POINTER_CHECKS) {
        emit_rr(vm, CHKP3, addr_reg, 0);
        emit_rr(vm, CHKP3, r_src, 0);
    }
    // Raw word-at-a-time copy of arg->ty->size bytes (4, 8, or 16), NOT
    // MCPY: MCPY hard-codes REG_A0/A1/A2, which this function runs
    // interleaved with the call's own argument-register evaluation loop and
    // would clobber earlier-placed argument registers.
    int r_tmp = alloc_temp_reg();
    int r_s   = alloc_temp_reg();
    int r_d   = alloc_temp_reg();
    emit_mov3(vm, r_s, r_src);
    emit_mov3(vm, r_d, addr_reg);
    if (arg->ty->size == 4) {
        emit_rr(vm, LDR_W, r_tmp, r_s);
        emit_rr(vm, STR_W, r_tmp, r_d);
    } else {
        int words = arg->ty->size / 8;
        for (int i = 0; i < words; i++) {
            emit_rr(vm, LDR_D, r_tmp, r_s);
            emit_rr(vm, STR_D, r_tmp, r_d);
            if (i + 1 < words) {
                emit_addi3(vm, r_s, r_s, 8);
                emit_addi3(vm, r_d, r_d, 8);
            }
        }
    }
    free_temp_reg(r_d);
    free_temp_reg(r_s);
    free_temp_reg(r_tmp);
    free_temp_reg(r_src);
}

void gen_zero_size_arg(VirtualMachine *vm, Node *arg, int dest_reg) {
    gen_expr(vm, arg, REG_ZERO);
    emit_li3(vm, dest_reg, 0);
}

// Unary negate (-x) or bitwise-complement (~x) of a wide _BitInt, via its
// runtime helper(dst, a, words, width). The operand is address-based (like
// the binary wide ops), so gen_expr yields the source address; the result is
// written into a fresh stack temp whose address is returned in dest_reg.
void gen_wide_bitint_unary(VirtualMachine *vm, Node *node, int dest_reg,
                           const char *helper) {
    Type *ty    = node->ty;
    int   words = ty->size / 8;
    int   r_src = alloc_temp_reg();
    gen_expr(vm, node->lhs, r_src); // wide operand → address
    long long dst_offset = node_misc(node)->ret_buffer
                               ? (long long)node_misc(node)->ret_buffer->offset
                               : alloc_wide_bitint_temp(vm, words);
    emit_lea3(vm, REG_A0, dst_offset);
    emit_mov3(vm, REG_A1, r_src);
    emit_li3(vm, REG_A2, words);
    emit_li3(vm, REG_A3, ty->bit_width);
    if (vm->flags & CCCC_POINTER_CHECKS) {
        emit_rr(vm, CHKP3, REG_A0, 0);
        emit_rr(vm, CHKP3, REG_A1, 0);
    }
    emit_wide_helper(vm, helper, 4);
    emit_lea3(vm, dest_reg, dst_offset);
    free_temp_reg(r_src);
}

// Generate `node` as a scalar 0/1 truth value in dest_reg for boolean contexts
// (if/while/for/?: conditions, &&, ||, casts to _Bool). For ordinary scalars
// the value the branch ops test is already correct; wide _BitInt operands are
// address-based, so OR-reduce their words to a single 0/1 via the runtime.
// _Decimal32/64/128 (#402) is likewise address-based -- test != 0 via DCMP
// against a zero literal of the same width (C truthiness: nonzero is true,
// including -0, which DCMP's quiet_equal already treats as == 0).
void gen_cond_expr(VirtualMachine *vm, Node *node, int dest_reg) {
    // TODO: a float/double operand is left in a float register that the branch
    // ops do not test (#1899).
    gen_expr(vm, node, dest_reg);
    if (is_wide_bitint(node->ty)) {
        emit_mov3(vm, REG_A0, dest_reg); // address of the wide value
        emit_li3(vm, REG_A1, node->ty->size / 8);
        emit_wide_helper(vm, "__cccc_bitint_nonzero", 2);
        emit_mov3(vm, dest_reg, REG_A0);
    } else if (is_decimal(node->ty)) {
        int           w             = dec_width_code(node->ty);
        unsigned char zero_bits[16] = {0};
        long long     offset        = vm->data_ptr - vm->data_seg;
        offset                      = (offset + (node->ty->align - 1)) &
                                      ~(long long)(node->ty->align - 1);
        vm->data_ptr                = vm->data_seg + offset;
        check_data_capacity(vm, offset + node->ty->size);
        if (!cccc_dec_encode_literal("0", w, zero_bits))
            error_tok(vm, node->tok,
                      "_Decimal requires a build with CCCC_HAS_DECIMAL=1");
        memcpy(vm->data_ptr, zero_bits, (size_t)node->ty->size);
        vm->data_ptr += node->ty->size;

        int r_zero    = alloc_temp_reg();
        emit_lda3(vm, r_zero, offset);
        emit_mov3(vm, REG_A0, dest_reg);
        emit_mov3(vm, REG_A1, r_zero);
        emit_li3(vm, REG_A2, w);
        emit_wide_op(vm, DCMP); // A0 = 0=EQ/1=LT/2=GT/3=UNORDERED
        int tmp = alloc_temp_reg();
        emit_li3(vm, tmp, 0);
        emit_rrr(vm, SNE3, dest_reg, REG_A0, tmp); // nonzero iff code != EQ(0)
        free_temp_reg(tmp);
        free_temp_reg(r_zero);
    }
}

// dangling_check gates only the CHKP3 (pointer/dangling) emission -- CHKT3
// (type checks, an orthogonal feature) is untouched. Pass false only when the
// address is known at compile time to be a bp-relative local-frame address
// (see addr_is_local_frame, #740), never for an address that could hold an
// arbitrary/stale pointer value.
//
// vm->compiler.in_union_member_access (#653) additionally gates CHKT3 here:
// a union member load must never be checked against the shadow -- legal
// union punning (write one member, read another) would otherwise
// false-positive -- so no CHKT3 is emitted at all for a union load.
//
// #983: CHKD (dereference-time bounds check) is gated the same way as
// CHKP3 -- dangling_check false means rs_addr is a compile-time-known
// bp-relative frame address, which is never resolvable via
// heap_alloc_for_ptr (it isn't a heap allocation at all), so CHKD would
// always no-op there anyway; skipping the emission avoids the dead check.
void emit_load_safety_checks(VirtualMachine *vm, Type *ty, int rs_addr,
                             bool dangling_check) {
    if (dangling_check && (vm->flags & CCCC_POINTER_CHECKS))
        emit_rr(vm, CHKP3, rs_addr, 0);
    if (dangling_check && (vm->flags & CCCC_BOUNDS_CHECKS))
        emit_rri(vm, CHKD, rs_addr, 0, (long long)ty->size);
    if ((vm->flags & CCCC_TYPE_CHECKS) && !vm->compiler.in_union_member_access)
        emit_rri(vm, CHKT3, rs_addr, CHKT3_MODE_CHECK,
                 ((long long)ty->size << 8) | (long long)ty->kind);
}

// The fused indexed form of a plain load/store opcode.
static int indexed_form(int op) {
    switch (op) {
        case LDR_B:
            return LDR_INDEX_B;
        case LDR_H:
            return LDR_INDEX_H;
        case LDR_W:
            return LDR_INDEX_W;
        case LDR_D:
            return LDR_INDEX_D;
        case STR_B:
            return STR_INDEX_B;
        case STR_H:
            return STR_INDEX_H;
        case STR_W:
            return STR_INDEX_W;
        case STR_D:
            return STR_INDEX_D;
        case FLDR:
            return FLDR_INDEX;
        case FLDR_F32:
            return FLDR_INDEX_F32;
        case FSTR:
            return FSTR_INDEX;
        default:
            return FSTR_INDEX_F32;
    }
}

// Emits `op rd, [rs_addr]`, or its fused indexed form when `ix` is given.
static void emit_mem_rr(VirtualMachine *vm, int op, int rd, int rs_addr,
                        const IndexedRegs *ix) {
    if (ix)
        emit_rrrs_i(vm, indexed_form(op), rd, ix->base, ix->index, ix->scale,
                    ix->offset);
    else
        emit_rr(vm, op, rd, rs_addr);
}

static void emit_load_impl(VirtualMachine *vm, Type *ty, int rd, int rs_addr,
                           bool dangling_check, const IndexedRegs *ix) {
    if (!ix)
        emit_load_safety_checks(vm, ty, rs_addr, dangling_check);
    if (ty->kind == TY_CHAR || ty->kind == TY_BOOL) {
        emit_mem_rr(vm, LDR_B, rd, rs_addr, ix);
        if (ty->is_unsigned || ty->kind == TY_BOOL)
            emit_rr(vm, ZX1, rd, rd);
    } else if (ty->kind == TY_SHORT) {
        emit_mem_rr(vm, LDR_H, rd, rs_addr, ix);
        if (ty->is_unsigned)
            emit_rr(vm, ZX2, rd, rd);
    } else if (ty->kind == TY_INT || (ty->kind == TY_ENUM && ty->size == 4)) {
        emit_mem_rr(vm, LDR_W, rd, rs_addr, ix);
        if (ty->is_unsigned)
            emit_rr(vm, ZX4, rd, rd);
    } else if (ty->kind == TY_ENUM) {
        if (ty->size == 1) {
            emit_mem_rr(vm, LDR_B, rd, rs_addr, ix);
            if (ty->is_unsigned)
                emit_rr(vm, ZX1, rd, rd);
        } else if (ty->size == 2) {
            emit_mem_rr(vm, LDR_H, rd, rs_addr, ix);
            if (ty->is_unsigned)
                emit_rr(vm, ZX2, rd, rd);
        } else {
            emit_mem_rr(vm, LDR_D, rd, rs_addr, ix);
        }
    } else if (ty->kind == TY_BITINT) {
        if (is_wide_bitint(ty)) {
            // Wide _BitInt: address-based. rd already holds the address
            // (rs_addr). Just move address if they differ; no value load
            // needed.
            if (rd != rs_addr)
                emit_mov3(vm, rd, rs_addr);
        } else if (ty->size == 1) {
            emit_mem_rr(vm, LDR_B, rd, rs_addr, ix);
            emit_rr(vm, ty->is_unsigned ? ZX1 : SX1, rd, rd);
        } else if (ty->size == 2) {
            emit_mem_rr(vm, LDR_H, rd, rs_addr, ix);
            emit_rr(vm, ty->is_unsigned ? ZX2 : SX2, rd, rd);
        } else if (ty->size == 4) {
            emit_mem_rr(vm, LDR_W, rd, rs_addr, ix);
            emit_rr(vm, ty->is_unsigned ? ZX4 : SX4, rd, rd);
        } else {
            emit_mem_rr(vm, LDR_D, rd, rs_addr, ix);
        }
        if (!is_wide_bitint(ty))
            emit_bitint_trunc(vm, ty, rd);
    } else if (is_decimal(ty)) {
        // _Decimal32/64/128 (#402): address-based, same as wide _BitInt.
        // rd already holds the address (rs_addr); no value load needed.
        if (rd != rs_addr)
            emit_mov3(vm, rd, rs_addr);
    } else if (is_flonum(ty)) {
        emit_mem_rr(vm, ty->kind == TY_FLOAT ? FLDR_F32 : FLDR, rd, rs_addr,
                    ix);
    } else {
        emit_mem_rr(vm, LDR_D, rd, rs_addr, ix);
    }
}

void emit_load_ex(VirtualMachine *vm, Type *ty, int rd, int rs_addr,
                  bool dangling_check) {
    emit_load_impl(vm, ty, rd, rs_addr, dangling_check, NULL);
}

void emit_load_indexed(VirtualMachine *vm, Type *ty, int rd,
                       const IndexedRegs *ix) {
    emit_load_impl(vm, ty, rd, -1, false, ix);
}

void emit_load(VirtualMachine *vm, Type *ty, int rd, int rs_addr) {
    emit_load_ex(vm, ty, rd, rs_addr, true);
}

// Fused load from bp-relative local slot — replaces LEA3+LDR
void emit_local_load(VirtualMachine *vm, Type *ty, int rd, long long offset) {
    if (ty->kind == TY_CHAR || ty->kind == TY_BOOL) {
        emit_ri(vm, LDR_LOCAL_B, rd, offset);
        if (ty->is_unsigned || ty->kind == TY_BOOL)
            emit_rr(vm, ZX1, rd, rd);
    } else if (ty->kind == TY_SHORT) {
        emit_ri(vm, LDR_LOCAL_H, rd, offset);
        if (ty->is_unsigned)
            emit_rr(vm, ZX2, rd, rd);
    } else if (ty->kind == TY_INT || (ty->kind == TY_ENUM && ty->size == 4)) {
        emit_ri(vm, LDR_LOCAL_W, rd, offset);
        if (ty->is_unsigned)
            emit_rr(vm, ZX4, rd, rd);
    } else if (ty->kind == TY_ENUM) {
        if (ty->size == 1) {
            emit_ri(vm, LDR_LOCAL_B, rd, offset);
            if (ty->is_unsigned)
                emit_rr(vm, ZX1, rd, rd);
        } else if (ty->size == 2) {
            emit_ri(vm, LDR_LOCAL_H, rd, offset);
            if (ty->is_unsigned)
                emit_rr(vm, ZX2, rd, rd);
        } else {
            emit_ri(vm, LDR_LOCAL_D, rd, offset);
        }
    } else if (ty->kind == TY_BITINT) {
        if (ty->size == 1) {
            emit_ri(vm, LDR_LOCAL_B, rd, offset);
            emit_rr(vm, ty->is_unsigned ? ZX1 : SX1, rd, rd);
        } else if (ty->size == 2) {
            emit_ri(vm, LDR_LOCAL_H, rd, offset);
            emit_rr(vm, ty->is_unsigned ? ZX2 : SX2, rd, rd);
        } else if (ty->size == 4) {
            emit_ri(vm, LDR_LOCAL_W, rd, offset);
            emit_rr(vm, ty->is_unsigned ? ZX4 : SX4, rd, rd);
        } else {
            emit_ri(vm, LDR_LOCAL_D, rd, offset);
        }
        emit_bitint_trunc(vm, ty, rd);
    } else if (ty->kind == TY_FLOAT) {
        emit_ri(vm, FLDR_LOCAL_F32, rd, offset);
    } else if (ty->kind == TY_DOUBLE || ty->kind == TY_LDOUBLE) {
        emit_ri(vm, FLDR_LOCAL, rd, offset);
    } else {
        emit_ri(vm, LDR_LOCAL_D, rd, offset);
    }
}

// Fused store to bp-relative local slot — replaces LEA3+STR
void emit_local_store(VirtualMachine *vm, Type *ty, int rd_val,
                      long long offset) {
    if (ty->kind == TY_CHAR || ty->kind == TY_BOOL) {
        emit_ri(vm, STR_LOCAL_B, rd_val, offset);
    } else if (ty->kind == TY_SHORT) {
        emit_ri(vm, STR_LOCAL_H, rd_val, offset);
    } else if (ty->kind == TY_INT || (ty->kind == TY_ENUM && ty->size == 4)) {
        emit_ri(vm, STR_LOCAL_W, rd_val, offset);
    } else if (ty->kind == TY_ENUM) {
        if (ty->size == 1)
            emit_ri(vm, STR_LOCAL_B, rd_val, offset);
        else if (ty->size == 2)
            emit_ri(vm, STR_LOCAL_H, rd_val, offset);
        else
            emit_ri(vm, STR_LOCAL_D, rd_val, offset);
    } else if (ty->kind == TY_BITINT) {
        if (ty->size == 1)
            emit_ri(vm, STR_LOCAL_B, rd_val, offset);
        else if (ty->size == 2)
            emit_ri(vm, STR_LOCAL_H, rd_val, offset);
        else if (ty->size == 4)
            emit_ri(vm, STR_LOCAL_W, rd_val, offset);
        else
            emit_ri(vm, STR_LOCAL_D, rd_val, offset);
    } else if (ty->kind == TY_FLOAT) {
        emit_ri(vm, FSTR_LOCAL_F32, rd_val, offset);
    } else if (ty->kind == TY_DOUBLE || ty->kind == TY_LDOUBLE) {
        emit_ri(vm, FSTR_LOCAL, rd_val, offset);
    } else {
        emit_ri(vm, STR_LOCAL_D, rd_val, offset);
    }
}

// Store operations based on type. dangling_check: see emit_load_ex above.
//
// vm->compiler.in_union_member_access (#653): a union member store must
// not stamp the accessed range with that member's type -- a later access
// through a *different* member of the same union is legal punning, not a
// bug -- so this emits CHKT3 in "clear" mode instead of "stamp" mode,
// erasing rather than establishing effective-type info for the range.
static void emit_store_impl(VirtualMachine *vm, Type *ty, int rd_val,
                            int rs_addr, bool dangling_check,
                            const IndexedRegs *ix) {
    if (!ix && dangling_check && (vm->flags & CCCC_POINTER_CHECKS))
        emit_rr(vm, CHKP3, rs_addr, 0);
    // #983: see emit_load_safety_checks' comment on dangling_check gating
    // CHKD the same way it gates CHKP3.
    if (!ix && dangling_check && (vm->flags & CCCC_BOUNDS_CHECKS))
        emit_rri(vm, CHKD, rs_addr, 0, (long long)ty->size);
    if (!ix && (vm->flags & CCCC_TYPE_CHECKS)) {
        int mode = vm->compiler.in_union_member_access ? CHKT3_MODE_CLEAR
                                                       : CHKT3_MODE_STAMP;
        emit_rri(vm, CHKT3, rs_addr, mode,
                 ((long long)ty->size << 8) | (long long)ty->kind);
    }
    if (ty->kind == TY_CHAR || ty->kind == TY_BOOL) {
        emit_mem_rr(vm, STR_B, rd_val, rs_addr, ix);
    } else if (ty->kind == TY_SHORT) {
        emit_mem_rr(vm, STR_H, rd_val, rs_addr, ix);
    } else if (ty->kind == TY_INT || (ty->kind == TY_ENUM && ty->size == 4)) {
        emit_mem_rr(vm, STR_W, rd_val, rs_addr, ix);
    } else if (ty->kind == TY_ENUM) {
        if (ty->size == 1)
            emit_mem_rr(vm, STR_B, rd_val, rs_addr, ix);
        else if (ty->size == 2)
            emit_mem_rr(vm, STR_H, rd_val, rs_addr, ix);
        else
            emit_mem_rr(vm, STR_D, rd_val, rs_addr, ix);
    } else if (ty->kind == TY_BITINT) {
        if (is_wide_bitint(ty)) {
            // rd_val holds the source address; rs_addr is the destination.
            emit_mov3(vm, REG_A0, rs_addr);
            emit_mov3(vm, REG_A1, rd_val);
            emit_li3(vm, REG_A2, ty->size);
            emit(vm, MCPY);
        } else if (ty->size == 1) {
            emit_mem_rr(vm, STR_B, rd_val, rs_addr, ix);
        } else if (ty->size == 2) {
            emit_mem_rr(vm, STR_H, rd_val, rs_addr, ix);
        } else if (ty->size == 4) {
            emit_mem_rr(vm, STR_W, rd_val, rs_addr, ix);
        } else {
            emit_mem_rr(vm, STR_D, rd_val, rs_addr, ix);
        }
    } else if (is_decimal(ty)) {
        // _Decimal32/64/128 (#402): rd_val holds the source address (like
        // wide _BitInt); rs_addr is the destination. Plain memcpy.
        emit_mov3(vm, REG_A0, rs_addr);
        emit_mov3(vm, REG_A1, rd_val);
        emit_li3(vm, REG_A2, ty->size);
        emit(vm, MCPY);
    } else if (is_flonum(ty)) {
        emit_mem_rr(vm, ty->kind == TY_FLOAT ? FSTR_F32 : FSTR, rd_val, rs_addr,
                    ix);
    } else {
        emit_mem_rr(vm, STR_D, rd_val, rs_addr, ix);
    }
}

void emit_store_ex(VirtualMachine *vm, Type *ty, int rd_val, int rs_addr,
                   bool dangling_check) {
    emit_store_impl(vm, ty, rd_val, rs_addr, dangling_check, NULL);
}

void emit_store_indexed(VirtualMachine *vm, Type *ty, int rd_val,
                        const IndexedRegs *ix) {
    emit_store_impl(vm, ty, rd_val, -1, false, ix);
}

void emit_store(VirtualMachine *vm, Type *ty, int rd_val, int rs_addr) {
    emit_store_ex(vm, ty, rd_val, rs_addr, true);
}

// Truncate register to _BitInt(N) value semantics via shift pair.
// Uses 64-bit shifts: SHL by (64-N) then SHR by (64-N) to mask to N bits.
// For N==64 the shift is 0 and ops are no-ops.
void emit_bitint_trunc(VirtualMachine *vm, Type *ty, int reg) {
    if (ty->bit_width >= 64)
        return;
    int shift = 64 - ty->bit_width;
    emit_rri(vm, SHLI3, reg, reg, shift);
    emit_rri(vm, ty->is_unsigned ? USHRI3 : SHRI3, reg, reg, shift);
}

// Wrap a 32-bit int/enum arithmetic result to its width, so the 64-bit
// register always holds the value the type denotes.
// TODO: one extra opcode per 32-bit op; skip it when the consumer narrows
// anyway (#1787).
void emit_int32_wrap(VirtualMachine *vm, Type *ty, int reg) {
    if (ty->size == 4 && (ty->kind == TY_INT || ty->kind == TY_ENUM))
        emit_rr(vm, ty->is_unsigned ? ZX4 : SX4, reg, reg);
}

// Signed bit width a checked arithmetic opcode (ADDC etc.) tests for `ty`.
int checked_width(Type *ty) {
    return ty->kind == TY_BITINT ? ty->bit_width : ty->size * 8;
}

// JZ3: if rs == 0, jump (returns patch location)
Pc emit_jz3(VirtualMachine *vm, int rs) {
    emit(vm, JZ3);
    emit_word(vm, ENCODE_R(rs));
    Pc patch            = emit_word_ptr(vm);
    vm->text_seg[patch] = 0;
    return patch;
}

// JNZ3: if rs != 0, jump (returns patch location)
Pc emit_jnz3(VirtualMachine *vm, int rs) {
    emit(vm, JNZ3);
    emit_word(vm, ENCODE_R(rs));
    Pc patch            = emit_word_ptr(vm);
    vm->text_seg[patch] = 0;
    return patch;
}

static void add_patch_to_list(PatchList *list, Pc patch) {
    if (list->len == list->cap) {
        int new_cap = list->cap ? list->cap * 2 : 16;
        Pc *items   = realloc(list->items, sizeof(Pc) * new_cap);
        if (!items)
            error("out of memory");
        list->items = items;
        list->cap   = new_cap;
    }
    list->items[list->len++] = patch;
}

static void add_case_patch(SwitchCasePatch *entry, Pc patch) {
    if (entry->num_patches == entry->cap_patches) {
        int new_cap = entry->cap_patches ? entry->cap_patches * 2 : 2;
        Pc *patches = realloc(entry->patches, sizeof(Pc) * new_cap);
        if (!patches)
            error("out of memory");
        entry->patches     = patches;
        entry->cap_patches = new_cap;
    }
    entry->patches[entry->num_patches++] = patch;
}

SwitchCasePatch *find_switch_case(SwitchCasePatch *cases, int num_cases,
                                  Node *node) {
    for (int i = 0; i < num_cases; i++)
        if (cases[i].node == node)
            return &cases[i];
    return NULL;
}

static int compare_switch_cases(const void *a, const void *b) {
    const SwitchCasePatch *ca = a;
    const SwitchCasePatch *cb = b;
    if (ca->begin < cb->begin)
        return -1;
    if (ca->begin > cb->begin)
        return 1;
    return 0;
}

SwitchCasePatch *collect_switch_cases(Node *node, int *num_cases,
                                      long *min_case, long *max_case,
                                      long *covered_values) {
    int              cap   = 16;
    SwitchCasePatch *cases = calloc(cap, sizeof(SwitchCasePatch));
    if (!cases)
        error("out of memory");

    *num_cases      = 0;
    *covered_values = 0;
    for (Node *n = node_ctl(node)->case_next; n; n = node_ctl(n)->case_next) {
        if (*num_cases == cap) {
            cap *= 2;
            SwitchCasePatch *new_cases =
                realloc(cases, sizeof(SwitchCasePatch) * cap);
            if (!new_cases)
                error("out of memory");
            memset(new_cases + *num_cases, 0,
                   sizeof(SwitchCasePatch) * (cap - *num_cases));
            cases = new_cases;
        }

        long begin                    = node_ctl(n)->begin;
        long end                      = node_ctl(n)->end;
        cases[*num_cases].node        = n;
        cases[*num_cases].begin       = begin;
        cases[*num_cases].end         = end;
        cases[*num_cases].table_entry = CCCC_INVALID_PC;

        if (*num_cases == 0 || begin < *min_case)
            *min_case = begin;
        if (*num_cases == 0 || end > *max_case)
            *max_case = end;
        *covered_values += end - begin + 1;
        (*num_cases)++;
    }

    qsort(cases, *num_cases, sizeof(SwitchCasePatch), compare_switch_cases);
    return cases;
}

void free_switch_cases(SwitchCasePatch *cases, int num_cases) {
    if (!cases)
        return;
    for (int i = 0; i < num_cases; i++)
        free(cases[i].patches);
    free(cases);
}

void emit_sparse_switch_tree(VirtualMachine *vm, SwitchCasePatch *cases, int lo,
                             int hi, int r_val, int r_cmp,
                             PatchList *fail_patches) {
    if (lo > hi) {
        emit(vm, JMP);
        add_patch_to_list(fail_patches, emit_word_ptr(vm));
        return;
    }

    int mid = lo + (hi - lo) / 2;
    emit_li3(vm, r_cmp, cases[mid].begin);
    emit_rrr(vm, SLT3, r_cmp, r_val, r_cmp);
    Pc left_patch = emit_jnz3(vm, r_cmp);

    emit_li3(vm, r_cmp, cases[mid].end);
    emit_rrr(vm, SGT3, r_cmp, r_val, r_cmp);
    add_case_patch(&cases[mid], emit_jz3(vm, r_cmp));

    emit_sparse_switch_tree(vm, cases, mid + 1, hi, r_val, r_cmp, fail_patches);
    vm->text_seg[left_patch] = vm->text_ptr + 1;
    emit_sparse_switch_tree(vm, cases, lo, mid - 1, r_val, r_cmp, fail_patches);
}

// PSH3: push register value onto stack
void emit_psh3(VirtualMachine *vm, int rs) {
    emit(vm, PSH3);
    emit_word(vm, ENCODE_R(rs));
}

// POP3: pop stack value into register
void emit_pop3(VirtualMachine *vm, int rd) {
    emit(vm, POP3);
    emit_word(vm, ENCODE_R(rd));
}

// ========== Fused indexed access and multiply-add ==========

typedef struct {
    Node     *base;
    bool      base_is_addr; // base is a struct whose array member is indexed
    Node     *index;
    int       scale;
    long long offset;
} IndexedAddr;

// Drops the cast layers that turn the pieces of a pointer sum into pointers.
static Node *strip_ptr_casts(Node *node) {
    while (node && node->kind == ND_CAST && node->ty && node->ty->base)
        node = node->lhs;
    return node;
}

// Peels casts that leave the value in its register unchanged: same-type casts
// the parser wraps around operands, and (for a pointer sum) the casts that
// turn its pieces into pointers.
static Node *peel_noop_casts(Node *node, bool ptr_sum) {
    while (node && node->kind == ND_CAST && node->lhs && node->ty &&
           node->lhs->ty &&
           (ptr_sum
                ? node->ty->base != NULL
                : (!node->ty->base && node->ty->kind == node->lhs->ty->kind &&
                   node->ty->size == node->lhs->ty->size &&
                   node->ty->is_unsigned == node->lhs->ty->is_unsigned)))
        node = node->lhs;
    return node;
}

// Small non-negative (or signed) integer constant behind widening casts only,
// so a narrowing cast such as (char)300 is never read as 300.
static bool const_operand(Node *node, long long *value) {
    while (node && node->kind == ND_CAST && is_integer(node->ty) && node->lhs &&
           is_integer(node->lhs->ty) && node->ty->size >= node->lhs->ty->size)
        node = node->lhs;
    if (!node || node->kind != ND_NUM || !is_integer(node->ty))
        return false;
    if (node->val < 0 ? node->ty->is_unsigned || node->val < INT_MIN
                      : node->val > INT_MAX)
        return false;
    *value = node->val;
    return true;
}

static bool int_constant(Node *node, long long *value);

static bool indexed_type_ok(Type *ty) {
    if (!ty || is_wide_bitint(ty) || is_decimal(ty))
        return false;
    switch (ty->kind) {
        case TY_ARRAY:
        case TY_VLA:
        case TY_STRUCT:
        case TY_UNION:
        case TY_COMPLEX:
        case TY_FUNC:
        case TY_VECTOR:
        case TY_VOID:
            return false;
        default:
            return true;
    }
}

// Folds a constant `idx + c` / `idx - c` into the offset. Wrapping unsigned
// narrower than 64 bits is left alone: (long)(u + 1) is not (long)u + 1.
static Node *fold_index_constant(Node *index, int scale, long long *offset) {
    Node *sum = index;
    while (sum && sum->kind == ND_CAST && is_integer(sum->ty) && sum->lhs &&
           is_integer(sum->lhs->ty) && sum->ty->size >= sum->lhs->ty->size)
        sum = sum->lhs;
    if (!sum || (sum->kind != ND_ADD && sum->kind != ND_SUB) ||
        !is_integer(sum->ty) || sum->ty->kind == TY_BITINT ||
        (sum->ty->is_unsigned && sum->ty->size < 8))
        return index;
    long long c;
    if (const_operand(sum->rhs, &c)) {
        *offset += (sum->kind == ND_ADD ? c : -c) * scale;
        return sum->lhs;
    }
    if (sum->kind == ND_ADD && const_operand(sum->lhs, &c)) {
        *offset += c * scale;
        return sum->rhs;
    }
    return index;
}

static bool match_indexed_addr(VirtualMachine *vm, Node *deref,
                               IndexedAddr *out) {
    if (!deref || deref->kind != ND_DEREF || !indexed_type_ok(deref->ty) ||
        (vm->flags & CCCC_FUSION_UNSAFE_FLAGS))
        return false;
    Node *add = strip_ptr_casts(deref->lhs);
    if (!add || add->kind != ND_ADD)
        return false;
    Node *l = strip_ptr_casts(add->lhs);
    Node *r = strip_ptr_casts(add->rhs);
    if (!l || !r || !l->ty || !r->ty)
        return false;
    Node *base, *scaled;
    if (l->ty->base && is_integer(r->ty)) {
        base   = l;
        scaled = r;
    } else if (r->ty->base && is_integer(l->ty)) {
        base   = r;
        scaled = l;
    } else {
        return false;
    }

    Node     *index  = scaled;
    int       scale  = 1;
    long long offset = 0, k;
    if (scaled->kind == ND_MUL) {
        if (const_operand(scaled->rhs, &k) && k >= 1 && k <= 255) {
            index = scaled->lhs;
            scale = (int)k;
        } else if (const_operand(scaled->lhs, &k) && k >= 1 && k <= 255) {
            index = scaled->rhs;
            scale = (int)k;
        }
    }
    index = fold_index_constant(index, scale, &offset);
    if (const_operand(index, &k)) {
        offset += k * scale;
        index   = NULL;
    }

    bool base_is_addr = false;
    if (base->kind == ND_MEMBER && base->ty->kind == TY_ARRAY &&
        !base->member->is_bitfield) {
        offset       += base->member->offset;
        base          = base->lhs;
        base_is_addr  = true;
    }
    if (contains_funcall(base) || (index && contains_funcall(index)))
        return false;
    *out = (IndexedAddr){base, base_is_addr, index, scale, offset};
    return true;
}

bool gen_indexed_regs(VirtualMachine *vm, Node *deref, IndexedRegs *out) {
    IndexedAddr a;
    if (temp_regs_free() <= TEMP_REG_SPILL_THRESHOLD + 2 ||
        !match_indexed_addr(vm, deref, &a))
        return false;
    // A promoted base or index is read from its register: the access only
    // reads it, and it is already extended.
    int r_base = -1;
    if (!a.base_is_addr && !(a.index && may_store_to_variable(a.index)))
        r_base = promoted_operand_reg(vm, a.base);
    if (r_base < 0) {
        r_base = alloc_temp_reg();
        if (a.base_is_addr)
            gen_addr(vm, a.base, r_base);
        else
            gen_expr(vm, a.base, r_base);
        mark_temp_reg_used(r_base);
    }
    int r_index = REG_ZERO;
    if (a.index && (r_index = promoted_operand_reg(vm, a.index)) >= 0) {
        // nothing to evaluate
    } else if (a.index) {
        r_index = alloc_temp_reg();
        gen_expr(vm, a.index, r_index);
        // An unsigned index narrower than 64 bits can carry garbage in its
        // high bits (a post-increment lowers to `(n += 1) - 1`); signed ones
        // are sign-correct as loaded.
        Type *ity = a.index->ty;
        if (ity && ity->is_unsigned && is_integer(ity) &&
            ity->kind != TY_BITINT && ity->size < 8)
            emit_rr(vm,
                    ity->size == 1   ? ZX1
                    : ity->size == 2 ? ZX2
                                     : ZX4,
                    r_index, r_index);
        mark_temp_reg_used(r_index);
    }
    *out = (IndexedRegs){r_base, r_index, a.scale, a.offset};
    return true;
}

void free_indexed_regs(const IndexedRegs *ix) {
    if (ix->index != REG_ZERO)
        free_temp_reg(ix->index);
    free_temp_reg(ix->base);
}

// `add`'s operand `operand` as a multiply whose rounding/wrapping the fused
// opcode reproduces: 64-bit for pointer sums, same-width integers otherwise
// (the add's own wrap then covers the multiply's).
static Node *fusable_mul(Node *add, Node *operand) {
    bool  ptr_sum = add->ty->base != NULL;
    Node *mul     = peel_noop_casts(operand, ptr_sum);
    if (!mul || mul->kind != ND_MUL || !mul->ty || !is_integer(mul->ty) ||
        mul->ty->kind == TY_BITINT)
        return NULL;
    if (ptr_sum ? mul->ty->size != 8 : mul->ty->size != add->ty->size)
        return NULL;
    return mul;
}

static void emit_rrrr_op(VirtualMachine *vm, int op, int rd, int rs1, int rs2,
                         int rs3) {
    emit_word(vm, op);
    emit_word(vm, ENCODE_RRRR(rd, rs1, rs2, rs3));
}

// Evaluates `first` into dest_reg and `second`, `third` (optional) into fresh
// temps, in that order, which the callers arrange to match source order.
static bool gen_muladd_int(VirtualMachine *vm, Node *add, int dest_reg) {
    if (!add->ty->base && (!is_integer(add->ty) || add->ty->kind == TY_BITINT ||
                           add->ty->size > 8))
        return false;
    Node *mul_r = fusable_mul(add, add->rhs);
    Node *mul_l = mul_r ? NULL : fusable_mul(add, add->lhs);
    Node *mul   = mul_r ? mul_r : mul_l;
    if (!mul)
        return false;
    Node     *addend = mul_r ? add->lhs : add->rhs;
    Node     *x, *y = NULL;
    long long imm;
    // Pointer arithmetic keeps the small-constant rule.
    bool (*constant)(Node *, long long *) =
        add->ty->base ? const_operand : int_constant;
    if (constant(mul->rhs, &imm)) {
        x = mul->lhs;
    } else if (constant(mul->lhs, &imm)) {
        x = mul->rhs;
    } else {
        x = mul->lhs;
        y = mul->rhs;
    }
    if (add->ty->base) {
        addend = strip_ptr_casts(addend);
        x      = strip_ptr_casts(x);
    }

    // Promoted operands are read from their registers unless an operand may
    // store to a variable before the instruction reads it.
    bool direct = !may_store_to_variable(addend) && !may_store_to_variable(x) &&
                  !may_store_to_variable(y);
    int  p_add  = direct ? promoted_operand_reg(vm, addend) : -1;
    int  p_x    = direct ? promoted_operand_reg(vm, x) : -1;
    int  p_y    = direct && y ? promoted_operand_reg(vm, y) : -1;

    int r_x = -1, r_y = -1, r_add = dest_reg;
    if (mul_r) {
        if (p_add >= 0) {
            r_add = p_add;
        } else {
            gen_expr(vm, addend, dest_reg);
            mark_temp_reg_used(dest_reg);
        }
    }
    if (p_x >= 0) {
        r_x = p_x;
    } else {
        r_x = mul_r ? alloc_temp_reg() : dest_reg;
        gen_expr(vm, x, r_x);
        mark_temp_reg_used(r_x);
    }
    if (y) {
        if (p_y >= 0) {
            r_y = p_y;
        } else {
            r_y = alloc_temp_reg();
            gen_expr(vm, y, r_y);
            mark_temp_reg_used(r_y);
        }
    }
    if (!mul_r) {
        if (p_add >= 0) {
            r_add = p_add;
        } else {
            r_add = alloc_temp_reg();
            gen_expr(vm, addend, r_add);
            mark_temp_reg_used(r_add);
        }
    }
    if (y)
        emit_rrrr_op(vm, MULADD3, dest_reg, r_add, r_x, r_y);
    else
        emit_rrrs_i(vm, MULADDI3, dest_reg, r_add, r_x, 0, imm);
    if (!add->ty->base)
        emit_int32_wrap(vm, add->ty, dest_reg);
    if (!mul_r)
        free_temp_reg(r_add);
    if (y)
        free_temp_reg(r_y);
    if (mul_r)
        free_temp_reg(r_x);
    return true;
}

static bool gen_mul_imm(VirtualMachine *vm, Node *mul, int dest_reg) {
    if (!is_integer(mul->ty) || mul->ty->kind == TY_BITINT || mul->ty->base ||
        mul->ty->size > 8)
        return false;
    long long imm;
    Node     *x;
    if (int_constant(mul->rhs, &imm))
        x = mul->lhs;
    else if (int_constant(mul->lhs, &imm))
        x = mul->rhs;
    else
        return false;
    gen_expr(vm, x, dest_reg);
    emit_rri(vm, MULI3, dest_reg, dest_reg, imm);
    emit_int32_wrap(vm, mul->ty, dest_reg);
    return true;
}

static bool gen_muladd_float(VirtualMachine *vm, Node *node, int dest_reg) {
    Type *ty = node->ty;
    if (!ty || (ty->kind != TY_DOUBLE && ty->kind != TY_FLOAT))
        return false;
    bool f32 = ty->kind == TY_FLOAT;
    // The multiply on the right takes priority: it is the one source order
    // evaluates last, so the addend stays the first operand.
    Node *rhs        = peel_noop_casts(node->rhs, false);
    bool  mul_is_rhs = rhs->kind == ND_MUL;
    Node *mul        = mul_is_rhs ? rhs : peel_noop_casts(node->lhs, false);
    Node *addend     = mul_is_rhs ? node->lhs : node->rhs;
    if (mul->kind != ND_MUL || !mul->ty || mul->ty->kind != ty->kind ||
        !addend->ty || addend->ty->kind != ty->kind)
        return false;
    int op;
    if (node->kind == ND_ADD)
        op = f32 ? FMADD3_F32 : FMADD3;
    else if (mul_is_rhs)
        op = f32 ? FNMSUB3_F32 : FNMSUB3; // c - a*b
    else
        op = f32 ? FMSUB3_F32 : FMSUB3;   // a*b - c

    bool direct = !may_store_to_variable(addend) &&
                  !may_store_to_variable(mul->lhs) &&
                  !may_store_to_variable(mul->rhs);
    int  p_add  = direct ? promoted_operand_reg(vm, addend) : -1;
    int  p_a    = direct ? promoted_operand_reg(vm, mul->lhs) : -1;
    int  p_b    = direct ? promoted_operand_reg(vm, mul->rhs) : -1;

    int r_add = dest_reg, r_a, r_b;
    if (mul_is_rhs) {
        if (p_add >= 0) {
            r_add = p_add;
        } else {
            gen_expr(vm, addend, dest_reg);
            mark_temp_reg_used(dest_reg);
        }
        r_a = p_a >= 0 ? p_a : alloc_temp_reg();
    } else {
        r_a = p_a >= 0 ? p_a : dest_reg;
    }
    if (p_a < 0) {
        gen_expr(vm, mul->lhs, r_a);
        mark_temp_reg_used(r_a);
    }
    if (p_b >= 0) {
        r_b = p_b;
    } else {
        r_b = alloc_temp_reg();
        gen_expr(vm, mul->rhs, r_b);
        mark_temp_reg_used(r_b);
    }
    if (!mul_is_rhs) {
        if (p_add >= 0) {
            r_add = p_add;
        } else {
            r_add = alloc_temp_reg();
            gen_expr(vm, addend, r_add);
            mark_temp_reg_used(r_add);
        }
    }
    emit_rrrr_op(vm, op, dest_reg, r_add, r_a, r_b);
    if (!mul_is_rhs)
        free_temp_reg(r_add);
    free_temp_reg(r_b);
    if (mul_is_rhs)
        free_temp_reg(r_a);
    return true;
}

// Value of an integer constant (a literal behind any casts) in its own type,
// wrapped as each cast would. Narrowing and sign changes are applied, so
// (unsigned)-1 is 4294967295.
static long long wrap_int_to(Type *ty, long long v) {
    if (ty->kind == TY_BOOL)
        return v != 0;
    switch (ty->size) {
        case 1:
            return ty->is_unsigned ? (long long)(uint8_t)v : (int8_t)v;
        case 2:
            return ty->is_unsigned ? (long long)(uint16_t)v : (int16_t)v;
        case 4:
            return ty->is_unsigned ? (long long)(uint32_t)v : (int32_t)v;
        default:
            return v;
    }
}

static bool int_constant(Node *node, long long *value) {
    if (!node || !node->ty || !is_integer(node->ty) ||
        node->ty->kind == TY_BITINT || node->ty->size > 8)
        return false;
    if (node->kind == ND_NUM) {
        *value = wrap_int_to(node->ty, node->val);
        return true;
    }
    long long inner;
    if (node->kind != ND_CAST || !int_constant(node->lhs, &inner))
        return false;
    *value = wrap_int_to(node->ty, inner);
    return true;
}

// The value of an integer expression made only of constants, computed as the
// VM would. Anything that could trap or has no defined result (a zero divisor,
// an out-of-range shift count) is left to run.
static bool fold_int(Node *node, long long *value) {
    if (!node || !node->ty || !is_integer(node->ty) ||
        node->ty->kind == TY_BITINT || node->ty->size > 8)
        return false;
    long long a, b;
    if (node->kind == ND_NUM)
        return int_constant(node, value);
    if (node->kind == ND_CAST) {
        if (!fold_int(node->lhs, &a))
            return false;
        *value = wrap_int_to(node->ty, a);
        return true;
    }
    if (node->kind == ND_NEG || node->kind == ND_BITNOT ||
        node->kind == ND_NOT) {
        if (!fold_int(node->lhs, &a))
            return false;
        unsigned long long ua = (unsigned long long)a;
        *value = wrap_int_to(node->ty,
                             node->kind == ND_NEG      ? (long long)(0 - ua)
                             : node->kind == ND_BITNOT ? (long long)~ua
                                                       : (long long)(a == 0));
        return true;
    }
    if (!node->lhs || !node->rhs || !fold_int(node->lhs, &a) ||
        !fold_int(node->rhs, &b))
        return false;
    unsigned long long ua = (unsigned long long)a, ub = (unsigned long long)b;
    bool               uns   = node->ty->is_unsigned;
    int                width = node->ty->size * 8;
    long long          r;
    switch (node->kind) {
        case ND_ADD:
            r = (long long)(ua + ub);
            break;
        case ND_SUB:
            r = (long long)(ua - ub);
            break;
        case ND_MUL:
            r = (long long)(ua * ub);
            break;
        case ND_BITAND:
            r = a & b;
            break;
        case ND_BITOR:
            r = a | b;
            break;
        case ND_BITXOR:
            r = a ^ b;
            break;
        case ND_DIV:
        case ND_MOD:
            if (b == 0 || (!uns && b == -1))
                return false;
            if (uns)
                r = (long long)(node->kind == ND_DIV ? ua / ub : ua % ub);
            else
                r = node->kind == ND_DIV ? a / b : a % b;
            break;
        case ND_SHL:
        case ND_SHR:
            if (b < 0 || b >= width)
                return false;
            if (node->kind == ND_SHL)
                r = (long long)(ua << b);
            else
                r = uns ? (long long)(ua >> b) : a >> b;
            break;
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE: {
            Type *lt  = node->lhs->ty;
            bool  u64 = lt->is_unsigned && lt->size == 8;
            bool  lt_ = u64 ? ua < ub : a < b;
            bool  eq  = a == b;
            *value    = node->kind == ND_EQ   ? eq
                        : node->kind == ND_NE ? !eq
                        : node->kind == ND_LT ? lt_
                                              : (lt_ || eq);
            return true;
        }
        default:
            return false;
    }
    *value = wrap_int_to(node->ty, r);
    return true;
}

bool gen_folded_constant(VirtualMachine *vm, Node *node, int dest_reg) {
    switch (node->kind) {
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
        case ND_MOD:
        case ND_BITAND:
        case ND_BITOR:
        case ND_BITXOR:
        case ND_SHL:
        case ND_SHR:
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
        case ND_NEG:
        case ND_NOT:
        case ND_BITNOT:
        case ND_CAST:
            break;
        default:
            return false;
    }
    long long v;
    if (!cg_opt_enabled(vm) || !fold_int(node, &v))
        return false;
    if (dest_reg != REG_ZERO)
        emit_li3(vm, dest_reg, v);
    return true;
}

void patch_list_resolve(VirtualMachine *vm, PatchList *list, Pc target) {
    for (int i = 0; i < list->len; i++)
        vm->text_seg[list->items[i]] = target;
    free(list->items);
    *list = (PatchList){0};
}

void gen_branch(VirtualMachine *vm, Node *cond, bool when, int scratch,
                PatchList *to) {
    long long v;
    switch (cond->kind) {
        case ND_LOGAND:
        case ND_LOGOR: {
            // `a && b` is false as soon as either side is; `a || b` is true as
            // soon as either side is.
            bool short_when = cond->kind == ND_LOGOR;
            if (when == short_when) {
                gen_branch(vm, cond->lhs, when, scratch, to);
                gen_branch(vm, cond->rhs, when, scratch, to);
            } else {
                PatchList skip = {0};
                gen_branch(vm, cond->lhs, short_when, scratch, &skip);
                gen_branch(vm, cond->rhs, when, scratch, to);
                patch_list_resolve(vm, &skip, vm->text_ptr + 1);
            }
            return;
        }
        case ND_NOT:
            if (!is_wide_bitint(cond->lhs->ty) && !is_decimal(cond->lhs->ty)) {
                gen_branch(vm, cond->lhs, !when, scratch, to);
                return;
            }
            break;
        case ND_COMMA:
            gen_expr(vm, cond->lhs, REG_ZERO);
            gen_branch(vm, cond->rhs, when, scratch, to);
            return;
        default:
            break;
    }
    if (cg_opt_enabled(vm) && fold_int(cond, &v)) {
        if ((v != 0) == when) {
            emit(vm, JMP);
            add_patch_to_list(to, emit_word_ptr(vm));
        }
        return;
    }
    gen_cond_expr(vm, cond, scratch);
    add_patch_to_list(to,
                      when ? emit_jnz3(vm, scratch) : emit_jz3(vm, scratch));
}

static bool plain_int_operands(Node *node) {
    Type *l = node->lhs->ty, *r = node->rhs->ty;
    return l && r && is_integer(l) && is_integer(r) && l->kind != TY_BITINT &&
           r->kind != TY_BITINT && l->size <= 8 && r->size <= 8;
}

// `x + K`, `K + x`, `x - K` as one ADDI3, and a compare against a constant as
// one *I3 opcode, instead of loading the constant into a register first.
bool gen_imm_operand_op(VirtualMachine *vm, Node *node, int dest_reg) {
    if (dest_reg == REG_ZERO || !cg_opt_enabled(vm) || !node->ty ||
        !plain_int_operands(node))
        return false;
    long long k;
    Node     *x;
    bool      const_on_left;
    if (int_constant(node->rhs, &k)) {
        x             = node->lhs;
        const_on_left = false;
    } else if (int_constant(node->lhs, &k)) {
        x             = node->rhs;
        const_on_left = true;
    } else {
        return false;
    }
    if (int_constant(x, &(long long){0}))
        return false;

    int op;
    switch (node->kind) {
        case ND_ADD:
        case ND_SUB:
            if (node->ty->base || !is_integer(node->ty) ||
                node->ty->kind == TY_BITINT || node->ty->size > 8 ||
                (node->kind == ND_SUB && const_on_left))
                return false;
            gen_expr(vm, x, dest_reg);
            emit_rri(vm, ADDI3, dest_reg, dest_reg,
                     node->kind == ND_SUB
                         ? (long long)(0 - (unsigned long long)k)
                         : k);
            emit_int32_wrap(vm, node->ty, dest_reg);
            return true;
        case ND_SHL:
        case ND_SHR:
            if (const_on_left || k < 0 || k >= node->ty->size * 8)
                return false;
            gen_expr(vm, x, dest_reg);
            emit_rri(vm,
                     node->kind == ND_SHL    ? SHLI3
                     : node->ty->is_unsigned ? USHRI3
                                             : SHRI3,
                     dest_reg, dest_reg, k);
            if (node->kind == ND_SHL)
                emit_int32_wrap(vm, node->ty, dest_reg);
            return true;
        case ND_BITAND:
            op = ANDI3;
            break;
        case ND_EQ:
            op = SEQI3;
            break;
        case ND_NE:
            op = SNEI3;
            break;
        case ND_LT:
        case ND_LE: {
            Type *ct  = node->lhs->ty;
            bool  u64 = ct->is_unsigned && ct->size == 8;
            bool  lt  = node->kind == ND_LT;
            if (const_on_left)
                op = lt ? (u64 ? UGTI3 : SGTI3) : (u64 ? UGEI3 : SGEI3);
            else
                op = lt ? (u64 ? ULTI3 : SLTI3) : (u64 ? ULEI3 : SLEI3);
            break;
        }
        default:
            return false;
    }
    gen_expr(vm, x, dest_reg);
    emit_rri(vm, op, dest_reg, dest_reg, k);
    return true;
}

bool gen_fused_muladd(VirtualMachine *vm, Node *node, int dest_reg) {
    if (dest_reg == REG_ZERO || (vm->flags & CCCC_FUSION_UNSAFE_FLAGS) ||
        temp_regs_free() <= TEMP_REG_SPILL_THRESHOLD + 3 || !node->ty ||
        contains_funcall(node->lhs) || contains_funcall(node->rhs))
        return false;
    if (is_flonum(node->ty))
        return (node->kind == ND_ADD || node->kind == ND_SUB) &&
               gen_muladd_float(vm, node, dest_reg);
    if (node->kind == ND_ADD)
        return gen_muladd_int(vm, node, dest_reg);
    if (node->kind == ND_MUL)
        return gen_mul_imm(vm, node, dest_reg);
    return false;
}

// ========== Promoted locals ==========

// Widens `reg` to the width and signedness of `ty`, as a load from the slot
// would.
static void emit_normalize_promoted_scalar(VirtualMachine *vm, Type *ty,
                                           int reg) {
    if (ty->kind == TY_BOOL || ty->kind == TY_CHAR) {
        emit_rr(vm, ty->is_unsigned || ty->kind == TY_BOOL ? ZX1 : SX1, reg,
                reg);
    } else if (ty->kind == TY_SHORT) {
        emit_rr(vm, ty->is_unsigned ? ZX2 : SX2, reg, reg);
    } else if (ty->kind == TY_INT || (ty->kind == TY_ENUM && ty->size == 4)) {
        emit_rr(vm, ty->is_unsigned ? ZX4 : SX4, reg, reg);
    } else if (ty->kind == TY_BITINT) {
        if (ty->size == 1)
            emit_rr(vm, ty->is_unsigned ? ZX1 : SX1, reg, reg);
        else if (ty->size == 2)
            emit_rr(vm, ty->is_unsigned ? ZX2 : SX2, reg, reg);
        else if (ty->size == 4)
            emit_rr(vm, ty->is_unsigned ? ZX4 : SX4, reg, reg);
        emit_bitint_trunc(vm, ty, reg);
    }
}

void emit_promoted_read(VirtualMachine *vm, Obj *var, int dest_reg) {
    if (dest_reg == REG_ZERO)
        return;
    int preg = promoted_local_reg(vm, var);
    if (preg >= 0) {
        emit_mov3(vm, dest_reg, preg);
        return;
    }
    emit_fmov3(vm, dest_reg, fp_promoted_local_reg(vm, var));
}

// A cast that leaves a promoted register's value as it is: a representation
// no-op, or a widening of an integer the register already holds extended.
static bool cast_keeps_promoted_value(Type *to, Type *from) {
    if (cast_is_repr_noop(to, from))
        return true;
    if (!is_integer(to) || !is_integer(from) || to->kind == TY_BOOL ||
        to->kind == TY_BITINT || from->kind == TY_BITINT || from->size > 8)
        return false;
    return to->size == 8 ||
           (from->size < to->size && (from->is_unsigned || !to->is_unsigned));
}

// The promoted register to read `n` from in an instruction that only reads
// its operand, or -1 when `n` has to be evaluated into a temporary.
int promoted_operand_reg(VirtualMachine *vm, Node *n) {
    while (n && n->kind == ND_CAST && n->lhs && n->ty && n->lhs->ty &&
           cast_keeps_promoted_value(n->ty, n->lhs->ty))
        n = n->lhs;
    if (!n || n->kind != ND_VAR || !n->var)
        return -1;
    int reg = promoted_local_reg(vm, n->var);
    return reg >= 0 ? reg : fp_promoted_local_reg(vm, n->var);
}

// Whether evaluating `n` may store to a variable. Conservative: statement
// expressions, asm and atomics count, and so does a tree too large to scan.
static bool may_store_within(Node *n, int *budget) {
    for (; n; n = n->next) {
        if (--*budget < 0)
            return true;
        switch (n->kind) {
            case ND_ASSIGN:
            case ND_STMT_EXPR:
            case ND_ASM:
            case ND_CAS:
            case ND_EXCH:
            case ND_ASTORE:
                return true;
            default:
                break;
        }
        if (n->body || node_ctl(n)->init || node_ctl(n)->inc ||
            may_store_within(n->lhs, budget) ||
            may_store_within(n->rhs, budget) ||
            may_store_within(node_ctl(n)->cond, budget) ||
            may_store_within(node_ctl(n)->then, budget) ||
            may_store_within(node_ctl(n)->els, budget) ||
            may_store_within(n->args, budget))
            return true;
    }
    return false;
}

bool may_store_to_variable(Node *n) {
    int budget = 32;
    return may_store_within(n, &budget);
}

void emit_promoted_write(VirtualMachine *vm, Obj *var, int value_reg) {
    int preg = promoted_local_reg(vm, var);
    if (preg >= 0) {
        if (preg != value_reg)
            emit_mov3(vm, preg, value_reg);
        emit_normalize_promoted_scalar(vm, var->ty, preg);
        return;
    }
    preg = fp_promoted_local_reg(vm, var);
    if (var->ty->kind == TY_FLOAT)
        emit_fround_f32(vm, preg, value_reg);
    else if (preg != value_reg)
        emit_fmov3(vm, preg, value_reg);
}

// Prologue: save the callee-saved registers this function uses, then load the
// promoted parameters from their ENT3 spill slots.
void emit_save_promoted_registers(VirtualMachine *vm) {
    Compiler *cc = &vm->compiler;
    for (int i = 0; i < cc->promoted_count; i++)
        emit_local_store(vm, ty_long, cc->promoted_regs[i],
                         cc->promoted_save_offsets[i]);
    for (int i = 0; i < cc->fp_promoted_count; i++)
        emit_ri(vm, FSTR_LOCAL, cc->fp_promoted_regs[i],
                cc->fp_promoted_save_offsets[i]);
    for (int i = 0; i < cc->promoted_count; i++)
        if (cc->promoted_locals[i]->is_param)
            emit_local_load(vm, cc->promoted_locals[i]->ty,
                            cc->promoted_regs[i],
                            cc->promoted_locals[i]->offset);
    for (int i = 0; i < cc->fp_promoted_count; i++)
        if (cc->fp_promoted_locals[i]->is_param)
            emit_local_load(vm, cc->fp_promoted_locals[i]->ty,
                            cc->fp_promoted_regs[i],
                            cc->fp_promoted_locals[i]->offset);
}

void emit_restore_promoted_registers(VirtualMachine *vm) {
    Compiler *cc = &vm->compiler;
    for (int i = cc->fp_promoted_count - 1; i >= 0; i--)
        emit_ri(vm, FLDR_LOCAL, cc->fp_promoted_regs[i],
                cc->fp_promoted_save_offsets[i]);
    for (int i = cc->promoted_count - 1; i >= 0; i--)
        emit_local_load(vm, ty_long, cc->promoted_regs[i],
                        cc->promoted_save_offsets[i]);
}
