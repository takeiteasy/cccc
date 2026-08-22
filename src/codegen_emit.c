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
// the full N -- a documented limitation (see man/VM.md), not a silent
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
// (see man/SAFETY.md and the mark_addr_escapes pass in parse.c).
static Pc emit_lea3_ex(VirtualMachine *vm, int rd, long long offset,
                       bool skip_record) {
    emit_word(vm, LEA3);
    emit_word(vm, ENCODE_R(rd) | (skip_record ? LEA3_NO_RECORD : 0));
    return emit_i64(vm, offset);
}

// LEA3: rd = bp + offset. Default: recorded (safe) -- see emit_lea3_ex.
Pc emit_lea3(VirtualMachine *vm, int rd, long long offset) {
    return emit_lea3_ex(vm, rd, offset, false);
}

// STKTAG: tag [bp+offset, bp+offset+size) with the current frame's epoch,
// for interior dangling-pointer resolution (#675). See man/SAFETY.md.
Pc emit_stktag(VirtualMachine *vm, long long offset, long long size) {
    emit_word(vm, STKTAG);
    emit_word(vm, 0); // unused (no register operand)
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
    Pc   pc         = emit_lea3_ex(vm, rd, var->offset, !var->addr_escapes);
    if (escaping_agg || vector_agg) {
        emit_stktag(vm, var->offset, var->ty->size);
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
    return emit_lea3_ex(vm, rd, offset, true);
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
    restrict_cache_invalidate_all(vm);
    reset_temp_regs();
    return true;
}

// Emit a dedicated WIDE_* opcode (args already loaded into REG_A0..A5) for
// the hot wide-bitint ops (#456) — same args/clobber contract as
// emit_wide_helper's CALLF, just without the FFI marshalling overhead.
void emit_wide_op(VirtualMachine *vm, int op) {
    emit(vm, op);
    restrict_cache_invalidate_all(vm);
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
    int       bytes = arg->ty->size;
    long long off   = alloc_wide_bitint_temp(vm, bytes / 8);
    emit_lea3(vm, addr_reg, off); // address escapes to the callee -- record it
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
    long long dst_offset = node->ret_buffer
                               ? (long long)node->ret_buffer->offset
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
//
// Shared with restrict_cache_handle_deref's cache-hit path (#750), which
// re-derives rs_addr purely to run these checks -- forward-declared near
// CCCC_FUSION_UNSAFE_FLAGS since that caller sits earlier in the file.
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

void emit_load_ex(VirtualMachine *vm, Type *ty, int rd, int rs_addr,
                  bool dangling_check) {
    emit_load_safety_checks(vm, ty, rs_addr, dangling_check);
    if (ty->kind == TY_CHAR || ty->kind == TY_BOOL) {
        emit_rr(vm, LDR_B, rd, rs_addr);
        if (ty->is_unsigned || ty->kind == TY_BOOL)
            emit_rr(vm, ZX1, rd, rd);
    } else if (ty->kind == TY_SHORT) {
        emit_rr(vm, LDR_H, rd, rs_addr);
        if (ty->is_unsigned)
            emit_rr(vm, ZX2, rd, rd);
    } else if (ty->kind == TY_INT || (ty->kind == TY_ENUM && ty->size == 4)) {
        emit_rr(vm, LDR_W, rd, rs_addr);
        if (ty->is_unsigned)
            emit_rr(vm, ZX4, rd, rd);
    } else if (ty->kind == TY_ENUM) {
        if (ty->size == 1) {
            emit_rr(vm, LDR_B, rd, rs_addr);
            if (ty->is_unsigned)
                emit_rr(vm, ZX1, rd, rd);
        } else if (ty->size == 2) {
            emit_rr(vm, LDR_H, rd, rs_addr);
            if (ty->is_unsigned)
                emit_rr(vm, ZX2, rd, rd);
        } else {
            emit_rr(vm, LDR_D, rd, rs_addr);
        }
    } else if (ty->kind == TY_BITINT) {
        if (is_wide_bitint(ty)) {
            // Wide _BitInt: address-based. rd already holds the address
            // (rs_addr). Just move address if they differ; no value load
            // needed.
            if (rd != rs_addr)
                emit_mov3(vm, rd, rs_addr);
        } else if (ty->size == 1) {
            emit_rr(vm, LDR_B, rd, rs_addr);
            emit_rr(vm, ty->is_unsigned ? ZX1 : SX1, rd, rd);
        } else if (ty->size == 2) {
            emit_rr(vm, LDR_H, rd, rs_addr);
            emit_rr(vm, ty->is_unsigned ? ZX2 : SX2, rd, rd);
        } else if (ty->size == 4) {
            emit_rr(vm, LDR_W, rd, rs_addr);
            emit_rr(vm, ty->is_unsigned ? ZX4 : SX4, rd, rd);
        } else {
            emit_rr(vm, LDR_D, rd, rs_addr);
        }
        if (!is_wide_bitint(ty))
            emit_bitint_trunc(vm, ty, rd);
    } else if (is_decimal(ty)) {
        // _Decimal32/64/128 (#402): address-based, same as wide _BitInt.
        // rd already holds the address (rs_addr); no value load needed.
        if (rd != rs_addr)
            emit_mov3(vm, rd, rs_addr);
    } else if (is_flonum(ty)) {
        emit_rr(vm, ty->kind == TY_FLOAT ? FLDR_F32 : FLDR, rd, rs_addr);
    } else {
        emit_rr(vm, LDR_D, rd, rs_addr);
    }
}

void emit_load(VirtualMachine *vm, Type *ty, int rd, int rs_addr) {
    emit_load_ex(vm, ty, rd, rs_addr, true);
}

static Node *strip_index_casts(Node *node) {
    while (node && node->kind == ND_CAST)
        node = node->lhs;
    return node;
}

// Returns true if the expression subtree contains a function call.
// Used to guard indexed addressing: calls clobber all temp registers
// (caller-saved), so a base address held in r_base across a call in
// the index expression will be corrupted at runtime.
bool expr_has_call(Node *node) {
    if (!node)
        return false;
    if (node->kind == ND_FUNCALL)
        return true;
    return expr_has_call(node->lhs) || expr_has_call(node->rhs) ||
           expr_has_call(node->cond) || expr_has_call(node->then) ||
           expr_has_call(node->els) || expr_has_call(node->body);
}

bool is_index_scale(Node *node, Node **index, int *scale) {
    node = strip_index_casts(node);
    if (!node || node->kind != ND_MUL)
        return false;
    Node *lhs = strip_index_casts(node->lhs);
    Node *rhs = strip_index_casts(node->rhs);
    if (rhs && rhs->kind == ND_NUM && rhs->val > 0 && rhs->val <= 255) {
        *index = strip_index_casts(lhs);
        if (!*index)
            return false;
        *scale = (int)rhs->val;
        return true;
    }
    if (lhs && lhs->kind == ND_NUM && lhs->val > 0 && lhs->val <= 255) {
        *index = strip_index_casts(rhs);
        if (!*index)
            return false;
        *scale = (int)lhs->val;
        return true;
    }
    return false;
}

bool match_indexed_addr(VirtualMachine *vm, Node *addr, IndexedAddr *out) {
    addr = strip_index_casts(addr);
    if (!addr || addr->kind != ND_ADD || vm->compiler.opt_level < 2)
        return false;
    if (vm->flags & CCCC_FUSION_UNSAFE_FLAGS)
        return false;

    Node *lhs = strip_index_casts(addr->lhs);
    Node *rhs = strip_index_casts(addr->rhs);
    if (!lhs || !rhs)
        return false;
    Node *index = NULL;
    int   scale = 0;
    if (is_index_scale(rhs, &index, &scale)) {
        out->base   = lhs;
        out->index  = index;
        out->scale  = scale;
        out->offset = 0;
        return true;
    }
    if (is_index_scale(lhs, &index, &scale)) {
        out->base   = rhs;
        out->index  = index;
        out->scale  = scale;
        out->offset = 0;
        return true;
    }
    return false;
}

static int indexed_load_op(Type *ty) {
    if (ty->kind == TY_CHAR || ty->kind == TY_BOOL)
        return LDR_INDEX_B;
    if (ty->kind == TY_SHORT)
        return LDR_INDEX_H;
    if (ty->kind == TY_INT || (ty->kind == TY_ENUM && ty->size == 4))
        return LDR_INDEX_W;
    if (ty->kind == TY_ENUM) {
        if (ty->size == 1)
            return LDR_INDEX_B;
        if (ty->size == 2)
            return LDR_INDEX_H;
        return LDR_INDEX_D;
    }
    if (ty->kind == TY_BITINT && !is_wide_bitint(ty)) {
        if (ty->size == 1)
            return LDR_INDEX_B;
        if (ty->size == 2)
            return LDR_INDEX_H;
        if (ty->size == 4)
            return LDR_INDEX_W;
        return LDR_INDEX_D;
    }
    if (ty->kind == TY_FLOAT)
        return FLDR_INDEX_F32;
    if (ty->kind == TY_DOUBLE || ty->kind == TY_LDOUBLE)
        return FLDR_INDEX;
    return LDR_INDEX_D;
}

static int indexed_store_op(Type *ty) {
    if (ty->kind == TY_CHAR || ty->kind == TY_BOOL)
        return STR_INDEX_B;
    if (ty->kind == TY_SHORT)
        return STR_INDEX_H;
    if (ty->kind == TY_INT || (ty->kind == TY_ENUM && ty->size == 4))
        return STR_INDEX_W;
    if (ty->kind == TY_ENUM) {
        if (ty->size == 1)
            return STR_INDEX_B;
        if (ty->size == 2)
            return STR_INDEX_H;
        return STR_INDEX_D;
    }
    if (ty->kind == TY_BITINT && !is_wide_bitint(ty)) {
        if (ty->size == 1)
            return STR_INDEX_B;
        if (ty->size == 2)
            return STR_INDEX_H;
        if (ty->size == 4)
            return STR_INDEX_W;
        return STR_INDEX_D;
    }
    if (ty->kind == TY_FLOAT)
        return FSTR_INDEX_F32;
    if (ty->kind == TY_DOUBLE || ty->kind == TY_LDOUBLE)
        return FSTR_INDEX;
    return STR_INDEX_D;
}

// After computing an array index into `reg`, an *unsigned* index narrower than
// 64 bits may carry garbage in its high bits: intermediate unsigned arithmetic
// results are not truncated to the type width (e.g. the post-increment `n++`
// lowering `(unsigned)((n += 1) - 1)` evaluates `1 + 0xFFFFFFFF == 0x100000000`
// in a 64-bit register), and match_indexed_addr strips the widening cast that
// would otherwise zero-extend — i.e. truncate — the value before it is used as
// a byte offset. Re-apply that zero-extension here. Signed indices are
// sign-correct straight from their loads/arithmetic, so they need no fixup.
// (#581)
static void emit_index_normalize(VirtualMachine *vm, int reg, Type *ty) {
    if (!ty || !ty->is_unsigned || !is_integer(ty) || ty->kind == TY_BITINT)
        return;
    if (ty->size == 1)
        emit_rr(vm, ZX1, reg, reg);
    else if (ty->size == 2)
        emit_rr(vm, ZX2, reg, reg);
    else if (ty->size == 4)
        emit_rr(vm, ZX4, reg, reg);
    // size 8 (unsigned long / size_t): already full width.
}

bool emit_indexed_load_if_possible(VirtualMachine *vm, Node *node,
                                   int dest_reg) {
    if (!node || node->kind != ND_DEREF || !node->lhs ||
        node->ty->kind == TY_ARRAY || node->ty->kind == TY_STRUCT ||
        node->ty->kind == TY_UNION || node->ty->kind == TY_COMPLEX ||
        node->ty->kind == TY_VLA || // #971: address-based, same as TY_ARRAY
        is_wide_bitint(node->ty) || is_decimal(node->ty)) // #402: address-based
        return false;
    IndexedAddr idx = {};
    if (!match_indexed_addr(vm, node->lhs, &idx))
        return false;
    if (expr_has_call(idx.base) || expr_has_call(idx.index))
        return false;
    int r_base = alloc_temp_reg();
    gen_expr(vm, idx.base, r_base);
    mark_temp_reg_used(r_base);
    int r_index = alloc_temp_reg();
    gen_expr(vm, idx.index, r_index);
    emit_index_normalize(vm, r_index, idx.index->ty);
    emit_rrrs_i(vm, indexed_load_op(node->ty), dest_reg, r_base, r_index,
                idx.scale, idx.offset);
    if (!is_flonum(node->ty)) {
        if (node->ty->kind == TY_BOOL || node->ty->kind == TY_CHAR) {
            if (node->ty->is_unsigned || node->ty->kind == TY_BOOL)
                emit_rr(vm, ZX1, dest_reg, dest_reg);
        } else if (node->ty->kind == TY_SHORT) {
            if (node->ty->is_unsigned)
                emit_rr(vm, ZX2, dest_reg, dest_reg);
        } else if (node->ty->kind == TY_INT ||
                   (node->ty->kind == TY_ENUM && node->ty->size == 4)) {
            if (node->ty->is_unsigned)
                emit_rr(vm, ZX4, dest_reg, dest_reg);
        } else if (node->ty->kind == TY_BITINT) {
            if (node->ty->is_unsigned) {
                if (node->ty->size == 1)
                    emit_rr(vm, ZX1, dest_reg, dest_reg);
                else if (node->ty->size == 2)
                    emit_rr(vm, ZX2, dest_reg, dest_reg);
                else if (node->ty->size == 4)
                    emit_rr(vm, ZX4, dest_reg, dest_reg);
            } else {
                if (node->ty->size == 1)
                    emit_rr(vm, SX1, dest_reg, dest_reg);
                else if (node->ty->size == 2)
                    emit_rr(vm, SX2, dest_reg, dest_reg);
                else if (node->ty->size == 4)
                    emit_rr(vm, SX4, dest_reg, dest_reg);
            }
            emit_bitint_trunc(vm, node->ty, dest_reg);
        }
    }
    free_temp_reg(r_index);
    free_temp_reg(r_base);
    return true;
}

bool emit_indexed_store_if_possible(VirtualMachine *vm, Node *lhs, Type *ty,
                                    int value_reg) {
    if (!lhs || lhs->kind != ND_DEREF || !lhs->lhs || ty->kind == TY_STRUCT ||
        ty->kind == TY_UNION || ty->kind == TY_COMPLEX || is_wide_bitint(ty) ||
        is_decimal(ty)) // #402: address-based
        return false;
    IndexedAddr idx = {};
    if (!match_indexed_addr(vm, lhs->lhs, &idx))
        return false;
    if (expr_has_call(idx.base) || expr_has_call(idx.index))
        return false;
    int r_base = alloc_temp_reg();
    gen_expr(vm, idx.base, r_base);
    mark_temp_reg_used(r_base);
    int r_index = alloc_temp_reg();
    gen_expr(vm, idx.index, r_index);
    emit_index_normalize(vm, r_index, idx.index->ty);
    emit_rrrs_i(vm, indexed_store_op(ty), value_reg, r_base, r_index, idx.scale,
                idx.offset);
    free_temp_reg(r_index);
    free_temp_reg(r_base);
    return true;
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

void emit_normalize_promoted_scalar(VirtualMachine *vm, Type *ty, int reg) {
    if (!ty)
        return;
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
    int preg = promoted_local_reg(vm, var);
    if (preg < 0 || dest_reg == REG_ZERO)
        return;
    emit_mov3(vm, dest_reg, preg);
}

void emit_promoted_write(VirtualMachine *vm, Obj *var, int value_reg) {
    int idx = promoted_local_index(vm, var);
    if (idx < 0)
        return;
    int preg = vm->compiler.promoted_regs[idx];
    if (preg != value_reg)
        emit_mov3(vm, preg, value_reg);
    emit_normalize_promoted_scalar(vm, var->ty, preg);
    vm->compiler.promoted_dirty[idx] = true;
}

void emit_flush_promoted_locals(VirtualMachine *vm) {
    for (int i = 0; i < vm->compiler.promoted_count; i++) {
        if (!vm->compiler.promoted_dirty[i])
            continue;
        Obj *var = vm->compiler.promoted_locals[i];
        emit_local_store(vm, var->ty, vm->compiler.promoted_regs[i],
                         var->offset);
        vm->compiler.promoted_dirty[i] = false;
    }
}

void emit_save_promoted_registers(VirtualMachine *vm) {
    for (int i = 0; i < vm->compiler.promoted_count; i++)
        emit_local_store(vm, ty_long, vm->compiler.promoted_regs[i],
                         vm->compiler.promoted_save_offsets[i]);
}

void emit_restore_promoted_registers(VirtualMachine *vm) {
    for (int i = vm->compiler.promoted_count - 1; i >= 0; i--)
        emit_local_load(vm, ty_long, vm->compiler.promoted_regs[i],
                        vm->compiler.promoted_save_offsets[i]);
}

void emit_init_promoted_params(VirtualMachine *vm) {
    for (int i = 0; i < vm->compiler.promoted_count; i++) {
        Obj *var = vm->compiler.promoted_locals[i];
        if (var->is_param)
            emit_local_load(vm, var->ty, vm->compiler.promoted_regs[i],
                            var->offset);
    }
}

// ---- FP local promotion helpers (#461) ----

static int fp_promoted_local_index(VirtualMachine *vm, Obj *var) {
    for (int i = 0; i < vm->compiler.fp_promoted_count; i++)
        if (vm->compiler.fp_promoted_locals[i] == var)
            return i;
    return -1;
}

bool is_fp_promoted_local(VirtualMachine *vm, Obj *var) {
    return fp_promoted_local_index(vm, var) >= 0;
}

static int fp_promoted_local_reg(VirtualMachine *vm, Obj *var) {
    int idx = fp_promoted_local_index(vm, var);
    return idx >= 0 ? vm->compiler.fp_promoted_regs[idx] : -1;
}

void emit_fp_promoted_read(VirtualMachine *vm, Obj *var, int dest_reg) {
    int preg = fp_promoted_local_reg(vm, var);
    if (preg < 0 || dest_reg == REG_ZERO)
        return;
    emit_fmov3(vm, dest_reg, preg);
}

void emit_fp_promoted_write(VirtualMachine *vm, Obj *var, int value_reg) {
    int idx = fp_promoted_local_index(vm, var);
    if (idx < 0)
        return;
    int preg = vm->compiler.fp_promoted_regs[idx];
    if (preg != value_reg)
        emit_fmov3(vm, preg, value_reg);
    vm->compiler.fp_promoted_dirty[idx] = true;
}

void emit_flush_fp_promoted_locals(VirtualMachine *vm) {
    for (int i = 0; i < vm->compiler.fp_promoted_count; i++) {
        if (!vm->compiler.fp_promoted_dirty[i])
            continue;
        Obj *var = vm->compiler.fp_promoted_locals[i];
        emit_local_store(vm, var->ty, vm->compiler.fp_promoted_regs[i],
                         var->offset);
        vm->compiler.fp_promoted_dirty[i] = false;
    }
}

void emit_save_fp_promoted_registers(VirtualMachine *vm) {
    // Save as flat double — fregs[] holds doubles after detag (#460).
    for (int i = 0; i < vm->compiler.fp_promoted_count; i++)
        emit_ri(vm, FSTR_LOCAL, vm->compiler.fp_promoted_regs[i],
                vm->compiler.fp_promoted_save_offsets[i]);
}

void emit_restore_fp_promoted_registers(VirtualMachine *vm) {
    for (int i = vm->compiler.fp_promoted_count - 1; i >= 0; i--)
        emit_ri(vm, FLDR_LOCAL, vm->compiler.fp_promoted_regs[i],
                vm->compiler.fp_promoted_save_offsets[i]);
}

void emit_init_fp_promoted_params(VirtualMachine *vm) {
    for (int i = 0; i < vm->compiler.fp_promoted_count; i++) {
        Obj *var = vm->compiler.fp_promoted_locals[i];
        if (var->is_param)
            emit_local_load(vm, var->ty, vm->compiler.fp_promoted_regs[i],
                            var->offset);
    }
}

// Store operations based on type. dangling_check: see emit_load_ex above.
//
// vm->compiler.in_union_member_access (#653): a union member store must
// not stamp the accessed range with that member's type -- a later access
// through a *different* member of the same union is legal punning, not a
// bug -- so this emits CHKT3 in "clear" mode instead of "stamp" mode,
// erasing rather than establishing effective-type info for the range.
void emit_store_ex(VirtualMachine *vm, Type *ty, int rd_val, int rs_addr,
                   bool dangling_check) {
    if (dangling_check && (vm->flags & CCCC_POINTER_CHECKS))
        emit_rr(vm, CHKP3, rs_addr, 0);
    // #983: see emit_load_safety_checks' comment on dangling_check gating
    // CHKD the same way it gates CHKP3.
    if (dangling_check && (vm->flags & CCCC_BOUNDS_CHECKS))
        emit_rri(vm, CHKD, rs_addr, 0, (long long)ty->size);
    if (vm->flags & CCCC_TYPE_CHECKS) {
        int mode = vm->compiler.in_union_member_access ? CHKT3_MODE_CLEAR
                                                       : CHKT3_MODE_STAMP;
        emit_rri(vm, CHKT3, rs_addr, mode,
                 ((long long)ty->size << 8) | (long long)ty->kind);
    }
    if (ty->kind == TY_CHAR || ty->kind == TY_BOOL) {
        emit_rr(vm, STR_B, rd_val, rs_addr);
    } else if (ty->kind == TY_SHORT) {
        emit_rr(vm, STR_H, rd_val, rs_addr);
    } else if (ty->kind == TY_INT || (ty->kind == TY_ENUM && ty->size == 4)) {
        emit_rr(vm, STR_W, rd_val, rs_addr);
    } else if (ty->kind == TY_ENUM) {
        if (ty->size == 1)
            emit_rr(vm, STR_B, rd_val, rs_addr);
        else if (ty->size == 2)
            emit_rr(vm, STR_H, rd_val, rs_addr);
        else
            emit_rr(vm, STR_D, rd_val, rs_addr);
    } else if (ty->kind == TY_BITINT) {
        if (is_wide_bitint(ty)) {
            // rd_val holds the source address; rs_addr is the destination.
            emit_mov3(vm, REG_A0, rs_addr);
            emit_mov3(vm, REG_A1, rd_val);
            emit_li3(vm, REG_A2, ty->size);
            emit(vm, MCPY);
        } else if (ty->size == 1) {
            emit_rr(vm, STR_B, rd_val, rs_addr);
        } else if (ty->size == 2) {
            emit_rr(vm, STR_H, rd_val, rs_addr);
        } else if (ty->size == 4) {
            emit_rr(vm, STR_W, rd_val, rs_addr);
        } else {
            emit_rr(vm, STR_D, rd_val, rs_addr);
        }
    } else if (is_decimal(ty)) {
        // _Decimal32/64/128 (#402): rd_val holds the source address (like
        // wide _BitInt); rs_addr is the destination. Plain memcpy.
        emit_mov3(vm, REG_A0, rs_addr);
        emit_mov3(vm, REG_A1, rd_val);
        emit_li3(vm, REG_A2, ty->size);
        emit(vm, MCPY);
    } else if (is_flonum(ty)) {
        emit_rr(vm, ty->kind == TY_FLOAT ? FSTR_F32 : FSTR, rd_val, rs_addr);
    } else {
        emit_rr(vm, STR_D, rd_val, rs_addr);
    }
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
    int tmp   = alloc_temp_reg();
    emit_li3(vm, tmp, shift);
    emit_rrr(vm, SHL3, reg, reg, tmp);
    emit_rrr(vm, ty->is_unsigned ? USHR3 : SHR3, reg, reg, tmp);
    free_temp_reg(tmp);
}

// JZ3: if rs == 0, jump (returns patch location)
Pc emit_jz3(VirtualMachine *vm, int rs) {
    emit(vm, JZ3);
    emit_word(vm, ENCODE_R(rs));
    Pc patch            = emit_word_ptr(vm);
    vm->text_seg[patch] = 0;
    // Branch creates a control-flow split; invalidate restrict cache for the
    // fall-through path.
    restrict_cache_invalidate_all(vm);
    return patch;
}

// JNZ3: if rs != 0, jump (returns patch location)
Pc emit_jnz3(VirtualMachine *vm, int rs) {
    emit(vm, JNZ3);
    emit_word(vm, ENCODE_R(rs));
    Pc patch            = emit_word_ptr(vm);
    vm->text_seg[patch] = 0;
    // Branch creates a control-flow split; invalidate restrict cache for the
    // fall-through path.
    restrict_cache_invalidate_all(vm);
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
    for (Node *n = node->case_next; n; n = n->case_next) {
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

        long begin                    = n->begin;
        long end                      = n->end;
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
