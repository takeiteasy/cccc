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

// ========== GNU vector_size Expressions (tracker #72) ==========
//
// A vector local/global lives in a memory slot aligned/sized to its own
// width (16/32/64 bytes), exactly like a small struct (gen_addr() already
// handles this generically -- no frame-layout changes needed). The *value*
// of a vector expression, however, flows through a vreg index in dest_reg,
// mirroring how FReg-typed expressions flow through fregs[] even though
// float locals also live in memory: VLDR/VSTR move between the slot and a
// vreg around each operation, carrying the value's byte width in the
// instruction operand.
//
// tracker #715 adds: bitwise &|^~ (integer lanes), integer lane / and %
// (per-lane trap on zero divisor / MIN-over-(-1)), comparisons ==/!=/</<=
// (GCC per-lane all-ones/all-zero SIGNED mask; >/>= are parsed as swapped
// </<=), GNU vector ?: select, and __builtin_convertvector.
//
// tracker #722 widens the substrate from 128-bit only to 128/256/512-bit
// (16/32/64-byte vectors), carrying the runtime lane count / byte width in
// the instruction operand instead of baking a fixed count into each opcode.
//
// Still deliberately out of scope (see follow-up tickets): __builtin_shuffle
// with a non-constant (runtime) index mask (the constant-mask form is
// supported, lowered without a new opcode).

typedef enum { VLANE_F64, VLANE_F32, VLANE_I64, VLANE_I32, VLANE_I16, VLANE_I8 } VecLaneFamily;

static VecLaneFamily vector_lane_family(Type *elem) {
    if (is_flonum(elem))
        return elem->kind == TY_FLOAT ? VLANE_F32 : VLANE_F64;
    switch (elem->size) {
    case 1:  return VLANE_I8;
    case 2:  return VLANE_I16;
    case 4:  return VLANE_I32;
    default: return VLANE_I64;
    }
}

static int vector_binop_for(NodeKind kind, VecLaneFamily fam) {
    static const int table[6][4] = {
        // ADD          SUB          MUL          DIV (int lanes: the
        //                                             trapping VDIV_I* also
        //                                             used by vector_modop_for)
        { VADD_F64,  VSUB_F64,  VMUL_F64,  VDIV_F64 },   // VLANE_F64
        { VADD_F32,  VSUB_F32,  VMUL_F32,  VDIV_F32 },   // VLANE_F32
        { VADD_I64,  VSUB_I64,  VMUL_I64,  VDIV_I64 },   // VLANE_I64
        { VADD_I32,  VSUB_I32,  VMUL_I32,  VDIV_I32 },   // VLANE_I32
        { VADD_I16,  VSUB_I16,  VMUL_I16,  VDIV_I16 },   // VLANE_I16
        { VADD_I8,   VSUB_I8,   VMUL_I8,   VDIV_I8  },   // VLANE_I8
    };
    int col = kind == ND_ADD ? 0 : kind == ND_SUB ? 1 : kind == ND_MUL ? 2 : 3;
    int op = table[fam][col];
    if (op < 0)
        error("codegen: unsupported vector binary op (should have been "
              "rejected in add_type)");
    return op;
}

// Integer-lane-only modulo (tracker #715); add_type rejects float-lane %.
static int vector_modop_for(VecLaneFamily fam) {
    switch (fam) {
    case VLANE_I64: return VMOD_I64;
    case VLANE_I32: return VMOD_I32;
    case VLANE_I16: return VMOD_I16;
    case VLANE_I8:  return VMOD_I8;
    default:
        error("codegen: unsupported vector modulo (should have been "
              "rejected in add_type)");
        return -1;
    }
}

// Bitwise (tracker #715): width-agnostic -- no per-lane-family variants
// needed (VM handler loops over the operand-carried word count). add_type
// rejects float-lane &|^~.
static int vector_bitop_for(NodeKind kind) {
    switch (kind) {
    case ND_BITAND: return VAND;
    case ND_BITOR:  return VOR;
    case ND_BITXOR: return VXOR;
    default:
        error("codegen: unsupported vector bitwise op");
        return -1;
    }
}

static int vector_negop_for(VecLaneFamily fam) {
    static const int table[6] = { VNEG_F64, VNEG_F32, VNEG_I64,
                                   VNEG_I32, VNEG_I16, VNEG_I8 };
    return table[fam];
}

static int vector_splatop_for(VecLaneFamily fam) {
    static const int table[6] = { VSPLAT_F64, VSPLAT_F32, VSPLAT_I64,
                                   VSPLAT_I32, VSPLAT_I16, VSPLAT_I8 };
    return table[fam];
}

// Comparisons (tracker #715): GCC semantics -- per-lane all-ones/all-zero
// SIGNED mask. `is_uns` selects the unsigned-view VCLTU/VCLEU variants for
// ordered comparisons on integer lanes (EQ/NE are sign-independent; float
// lanes are always signed-ordered, `is_uns` is ignored for VLANE_F64/F32).
static int vector_cmpop_for(NodeKind kind, VecLaneFamily fam, bool is_uns) {
    static const int table[6][4] = {
        // EQ         NE         LT (signed)   LE (signed)
        { VCEQ_F64, VCNE_F64, VCLT_F64, VCLE_F64 },
        { VCEQ_F32, VCNE_F32, VCLT_F32, VCLE_F32 },
        { VCEQ_I64, VCNE_I64, VCLT_I64, VCLE_I64 },
        { VCEQ_I32, VCNE_I32, VCLT_I32, VCLE_I32 },
        { VCEQ_I16, VCNE_I16, VCLT_I16, VCLE_I16 },
        { VCEQ_I8,  VCNE_I8,  VCLT_I8,  VCLE_I8  },
    };
    static const int unsigned_table[6][2] = {
        // LT (unsigned)   LE (unsigned) -- only meaningful for int families
        { -1, -1 }, { -1, -1 },
        { VCLTU_I64, VCLEU_I64 },
        { VCLTU_I32, VCLEU_I32 },
        { VCLTU_I16, VCLEU_I16 },
        { VCLTU_I8,  VCLEU_I8  },
    };
    int col = kind == ND_EQ ? 0 : kind == ND_NE ? 1 : kind == ND_LT ? 2 : 3;
    if (is_uns && col >= 2) {
        int op = unsigned_table[fam][col - 2];
        if (op >= 0)
            return op;
    }
    return table[fam][col];
}

// Select (tracker #715): VSEL_{8,16,32,64} by lane byte width.
static int vector_selop_for(int lane_bytes) {
    switch (lane_bytes) {
    case 1: return VSEL_8;
    case 2: return VSEL_16;
    case 4: return VSEL_32;
    default: return VSEL_64;
    }
}


static void gen_vector_expr(VirtualMachine *vm, Node *node, int dest_reg) {
    switch (node->kind) {
    case ND_VAR:
    case ND_DEREF:
    case ND_MEMBER: {
        // Vector lvalue read: address, then a full-width load.
        int r_addr = alloc_temp_reg();
        gen_addr(vm, node, r_addr);
        // #983: VLDR bypasses emit_load_ex entirely, so it needs its own
        // CHKD, same local-frame exemption as emit_load_ex's dangling_check.
        if ((vm->flags & CCCC_BOUNDS_CHECKS) && !addr_is_local_frame(vm, node))
            emit_rri(vm, CHKD, r_addr, 0, (long long)node->ty->size);
        emit_rrs(vm, VLDR, dest_reg, r_addr, node->ty->size);
        free_temp_reg(r_addr);
        return;
    }
    case ND_ASSIGN: {
        int r_val = dest_reg == REG_ZERO ? alloc_temp_reg() : dest_reg;
        gen_expr(vm, node->rhs, r_val);
        mark_temp_reg_used(r_val);
        int r_addr = alloc_temp_reg();
        gen_addr(vm, node->lhs, r_addr);
        // #983: VSTR bypasses emit_store_ex entirely, same reasoning as the
        // VLDR case above.
        if ((vm->flags & CCCC_BOUNDS_CHECKS) && !addr_is_local_frame(vm, node->lhs))
            emit_rri(vm, CHKD, r_addr, 0, (long long)node->ty->size);
        emit_rrs(vm, VSTR, r_val, r_addr, node->ty->size);
        free_temp_reg(r_addr);
        if (dest_reg == REG_ZERO)
            free_temp_reg(r_val);
        return;
    }
    case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD: {
        // usual_arith_conv() already cast both operands to this node's
        // (vector) type, so both sides are vector-valued here.
        VecLaneFamily fam = vector_lane_family(node->ty->base);
        int r_lhs = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_lhs);
        mark_temp_reg_used(r_lhs);
        int r_rhs = alloc_temp_reg();
        gen_expr(vm, node->rhs, r_rhs);
        int op = node->kind == ND_MOD ? vector_modop_for(fam)
                                       : vector_binop_for(node->kind, fam);
        emit_rrrs(vm, op, dest_reg, r_lhs, r_rhs, node->ty->vec_len);
        free_temp_reg(r_rhs);
        free_temp_reg(r_lhs);
        return;
    }
    case ND_BITAND: case ND_BITOR: case ND_BITXOR: {
        int r_lhs = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_lhs);
        mark_temp_reg_used(r_lhs);
        int r_rhs = alloc_temp_reg();
        gen_expr(vm, node->rhs, r_rhs);
        emit_rrrs(vm, vector_bitop_for(node->kind), dest_reg, r_lhs, r_rhs,
                  node->ty->size / 8);
        free_temp_reg(r_rhs);
        free_temp_reg(r_lhs);
        return;
    }
    case ND_BITNOT: {
        int r_src = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_src);
        emit_rrs(vm, VNOT, dest_reg, r_src, node->ty->size / 8);
        free_temp_reg(r_src);
        return;
    }
    case ND_EQ: case ND_NE: case ND_LT: case ND_LE: {
        // node->ty is the signed-integer mask type (vector_mask_type);
        // node->lhs->ty is the still-intact operand vector type, which is
        // what determines the lane family/signedness to compare.
        VecLaneFamily fam = vector_lane_family(node->lhs->ty->base);
        bool is_uns = node->lhs->ty->base->is_unsigned;
        int r_lhs = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_lhs);
        mark_temp_reg_used(r_lhs);
        int r_rhs = alloc_temp_reg();
        gen_expr(vm, node->rhs, r_rhs);
        emit_rrrs(vm, vector_cmpop_for(node->kind, fam, is_uns), dest_reg,
                  r_lhs, r_rhs, node->lhs->ty->vec_len);
        free_temp_reg(r_rhs);
        free_temp_reg(r_lhs);
        return;
    }
    case ND_COND: {
        if (!is_vector(node->cond->ty)) {
            // Ordinary C ternary with vector arms (tracker #715): the
            // condition is a plain scalar, so the whole vector value is
            // selected by a runtime branch -- standard C, not the GNU
            // per-lane extension below. Identical shape to the generic
            // (scalar/struct) ND_COND codegen elsewhere in this switch.
            int r_cond = (dest_reg == REG_ZERO) ? alloc_temp_reg() : dest_reg;
            mark_temp_reg_used(r_cond);
            gen_cond_expr(vm, node->cond, r_cond);
            Pc jz_else = emit_jz3(vm, r_cond);
            if (r_cond != dest_reg) free_temp_reg(r_cond);

            gen_expr(vm, node->then, dest_reg);
            emit(vm, JMP);
            Pc jmp_end = emit_word_ptr(vm);

            vm->text_seg[jz_else] = vm->text_ptr + 1;
            gen_expr(vm, node->els, dest_reg);
            vm->text_seg[jmp_end] = vm->text_ptr + 1;
            return;
        }
        // GNU per-lane vector ?: select (tracker #715): rd is pre-loaded
        // with the else-arm, then VSEL_w overwrites only the lanes where
        // cond is nonzero (see op_VSEL_*_fn in ops.c -- a read-modify-write
        // on rd, like VINSERT_*, safe under the optimizer's fully-opaque
        // treatment of vector opcodes).
        int r_dest = dest_reg == REG_ZERO ? alloc_temp_reg() : dest_reg;
        gen_expr(vm, node->els, r_dest);
        mark_temp_reg_used(r_dest);
        int r_cond = alloc_temp_reg();
        gen_expr(vm, node->cond, r_cond);
        mark_temp_reg_used(r_cond);
        int r_then = alloc_temp_reg();
        gen_expr(vm, node->then, r_then);
        emit_rrrs(vm, vector_selop_for(node->ty->base->size), r_dest, r_cond,
                  r_then, node->ty->vec_len);
        free_temp_reg(r_then);
        free_temp_reg(r_cond);
        if (dest_reg == REG_ZERO)
            free_temp_reg(r_dest);
        return;
    }
    case ND_NEG: {
        VecLaneFamily fam = vector_lane_family(node->ty->base);
        int r_src = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_src);
        emit_rrs(vm, vector_negop_for(fam), dest_reg, r_src, node->ty->vec_len);
        free_temp_reg(r_src);
        return;
    }
    case ND_CAST: {
        if (is_vector(node->lhs->ty)) {
            // Identity cast (usual_arith_conv casts a same-type vector
            // operand to the common type, which is itself). No conversion
            // needed -- just materialize the source value into dest_reg.
            gen_expr(vm, node->lhs, dest_reg);
            return;
        }
        // Scalar -> vector: broadcast to every lane (the `vec + scalar`
        // broadcast semantics; also reachable via a bare scalar assigned to
        // a vector variable).
        VecLaneFamily fam = vector_lane_family(node->ty->base);
        int r_scalar = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_scalar);
        emit_rrs(vm, vector_splatop_for(fam), dest_reg, r_scalar, node->ty->vec_len);
        free_temp_reg(r_scalar);
        return;
    }
    case ND_CONVERTVECTOR: {
        // __builtin_convertvector (tracker #715): cross-lane-family element
        // conversion, restricted at parse time to int32<->float32 and
        // int64<->float64 pairs (see the builtin's parse.c handler).
        VecLaneFamily src_fam = vector_lane_family(node->lhs->ty->base);
        VecLaneFamily dst_fam = vector_lane_family(node->ty->base);
        int op;
        if (src_fam == VLANE_F32 && dst_fam == VLANE_I32) op = VCVT_I32_F32;
        else if (src_fam == VLANE_I32 && dst_fam == VLANE_F32) op = VCVT_F32_I32;
        else if (src_fam == VLANE_F64 && dst_fam == VLANE_I64) op = VCVT_I64_F64;
        else if (src_fam == VLANE_I64 && dst_fam == VLANE_F64) op = VCVT_F64_I64;
        else {
            error_tok(vm, node->tok,
                      "codegen: unsupported vector conversion (should have "
                      "been rejected by __builtin_convertvector's parser)");
            op = -1;
        }
        int r_src = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_src);
        emit_rrs(vm, op, dest_reg, r_src, node->ty->vec_len);
        free_temp_reg(r_src);
        return;
    }
    case ND_COMMA:
        // A vector-typed comma reaches here from lvar_initializer's
        // ND_MEMZERO pre-zero (`ND_COMMA(memzero, rhs)`, tracker #713's
        // brace-init/copy-init path) as well as ordinary comma expressions
        // whose result is a vector.
        gen_expr(vm, node->lhs, REG_ZERO); // Discard result
        gen_expr(vm, node->rhs, dest_reg);
        return;
    default:
        error_tok(vm, node->tok, "unsupported vector expression");
    }
}

// ========== Expression Generation ==========

// Generate code for expression, result in dest_reg (integer) or dest_freg
// (float)
void gen_expr(VirtualMachine *vm, Node *node, int dest_reg) {
    if (!node) {
        error("codegen: null expression node");
    }

    // A vector-returning ND_FUNCALL falls through to the main switch below
    // instead of gen_vector_expr (#714): the value comes back via the
    // RETBUF+VSTR return-buffer convention (see ND_RETURN), handled in
    // ND_FUNCALL's result tail, not as a plain vector expression.
    // ND_BLOCK_CALL (Apple Blocks) is deliberately excluded from this bypass
    // -- its ABI (below, `case ND_BLOCK_CALL`) has no RETBUF/pointer-
    // arg machinery for aggregates at all, so a vector-typed block call still
    // falls to gen_vector_expr's default case below and reports a clean
    // "unsupported vector expression" error rather than silently
    // mis-marshalling through REG_A0.
    if (is_vector(node->ty) && node->kind != ND_FUNCALL) {
        gen_vector_expr(vm, node, dest_reg);
        return;
    }

    if (is_complex(node->ty)) {
        int imag_reg = (dest_reg == FREG_A5) ? FREG_A4 : FREG_A5;
        gen_complex_expr(vm, node, dest_reg == REG_ZERO ? FREG_A0 : dest_reg,
                         imag_reg);
        return;
    }

    // #916: REG_ZERO (a discarded-value expression -- `*p;`, `(void)*p`,
    // a comma-operator LHS) is hardwired to 0 and discards writes. The
    // ND_DEREF/ND_MEMBER cases below use dest_reg as scratch for the address
    // before loading through it, so a REG_ZERO dest silently turns every such
    // load into a read from address 0. Integer loads only look harmless
    // because op_LDR_*_fn skips the load when rd == REG_ZERO; FLDR/FLDR_F32
    // have no such guard and segfault. Routing through a real temp also
    // makes the discarded-deref safety checks meaningful again -- they were
    // being run against address 0 (see the matching note in
    // restrict_cache_handle_deref above).
    if (dest_reg == REG_ZERO &&
        (node->kind == ND_DEREF || node->kind == ND_MEMBER)) {
        int tmp = alloc_temp_reg();
        gen_expr(vm, node, tmp);
        free_temp_reg(tmp);
        return;
    }

    switch (node->kind) {
    case ND_NULL_EXPR:
        return;

    case ND_UNREACHABLE:
        emit(vm, BTRAP);
        return;

    case ND_DECIMAL_TO_CHARS: {
        // __builtin_decimal_to_chars(buf, n, decimal_val) (#402): lowers
        // directly to DFMT. buf(A0), n(A1), val(A2)=address, width(A3).
        //
        // node->cond (the decimal value) is evaluated FIRST, before buf/n:
        // any decimal subexpression (e.g. `x - y`) emits an opaque DADD/
        // DSUB/... op, and emit_wide_op's reset_temp_regs() call marks
        // every temp register "free" again for the *next* allocation --
        // exactly like is_wide_bitint_helper_op's callers already have to
        // account for. Evaluating it last, after buf/n were already parked
        // in T-registers, let a later gen_expr()-internal alloc_temp_reg()
        // legally reclaim one of those "freed" T-registers mid-evaluation
        // and silently clobber buf/n's value (caught empirically: buf's
        // address arrived 48 bytes off at runtime). Evaluating the risky
        // operand first sidesteps the whole hazard rather than requiring
        // a push/pop spill for buf/n.
        int r_val = alloc_temp_reg();
        gen_expr(vm, node->cond, r_val); // decimal operand -> its address
        mark_temp_reg_used(r_val);
        int r_buf = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_buf);
        mark_temp_reg_used(r_buf);
        int r_n = alloc_temp_reg();
        gen_expr(vm, node->rhs, r_n);
        mark_temp_reg_used(r_n);

        emit_mov3(vm, REG_A0, r_buf);
        emit_mov3(vm, REG_A1, r_n);
        emit_mov3(vm, REG_A2, r_val);
        emit_li3(vm, REG_A3, dec_width_code(node->cond->ty));
        if (vm->flags & CCCC_POINTER_CHECKS) {
            emit_rr(vm, CHKP3, REG_A0, 0);
            emit_rr(vm, CHKP3, REG_A2, 0);
        }
        emit_wide_op(vm, DFMT);
        if (dest_reg != REG_ZERO)
            emit_mov3(vm, dest_reg, REG_A0);
        free_temp_reg(r_n);
        free_temp_reg(r_buf);
        free_temp_reg(r_val);
        return;
    }

    case ND_NUM:
        if (is_flonum(node->ty)) {
            long long offset = vm->data_ptr - vm->data_seg;
            offset = (offset + 7) & ~7; // Align
            vm->data_ptr = vm->data_seg + offset;
            long long lit_size = (node->ty->kind == TY_FLOAT) ? sizeof(float) : sizeof(double);
            check_data_capacity(vm, offset + lit_size);
            if (node->ty->kind == TY_FLOAT) {
                *(float *)vm->data_ptr = (float)node->fval;
                vm->data_ptr += sizeof(float);
            } else {
                *(double *)vm->data_ptr = node->fval;
                vm->data_ptr += sizeof(double);
            }

            int temp = alloc_temp_reg();
            emit_lda3(vm, temp, offset);
            emit_rr(vm, node->ty->kind == TY_FLOAT ? FLDR_F32 : FLDR, dest_reg, temp);
            free_temp_reg(temp);
        } else if (is_decimal(node->ty)) {
            // _Decimal32/64/128 literal (#402): encoded to BID bits at
            // COMPILE time (unlike the wide-_BitInt literal below, which
            // resolves at runtime) -- there is no runtime global-init path
            // for a static/global decimal initializer to hook into, so the
            // bytes must already be correct in the data segment. A decimal
            // value is address-based (never loaded into an FReg/int reg),
            // so dest_reg receives the literal's address, not a loaded value.
            if (!node->dec_digits)
                error_tok(vm, node->tok,
                          "internal error: _Decimal literal missing digit text");

            unsigned char bits[16];
            if (!cccc_dec_encode_literal(node->dec_digits, dec_width_code(node->ty), bits))
                error_tok(vm, node->tok,
                          "_Decimal literals require a build with CCCC_HAS_DECIMAL=1 "
                          "(run tools/fetch_intel_bid.sh, then `make CCCC_HAS_DECIMAL=1`)");

            long long offset = vm->data_ptr - vm->data_seg;
            offset = (offset + (node->ty->align - 1)) & ~(long long)(node->ty->align - 1);
            vm->data_ptr = vm->data_seg + offset;
            check_data_capacity(vm, offset + node->ty->size);
            memcpy(vm->data_ptr, bits, (size_t)node->ty->size);
            vm->data_ptr += node->ty->size;

            emit_lda3(vm, dest_reg, offset);
        } else if (is_wide_bitint(node->ty)) {
            // wb/uwb literal wider than 64 bits: materialize at runtime via
            // __cccc_bitint_from_str, reading the full-precision digit text
            // (captured by the tokenizer) from the data segment.
            if (!node->wide_digits)
                error_tok(vm, node->tok,
                          "internal error: wide _BitInt literal missing digit text");

            size_t digit_len = strlen(node->wide_digits);
            long long offset = vm->data_ptr - vm->data_seg;
            offset = (offset + 7) & ~7; // Align
            vm->data_ptr = vm->data_seg + offset;
            check_data_capacity(vm, offset + (long long)digit_len + 1);
            memcpy(vm->data_ptr, node->wide_digits, digit_len + 1);
            vm->data_ptr += digit_len + 1;

            int str_addr = alloc_temp_reg();
            emit_lda3(vm, str_addr, offset);

            int words = node->ty->size / 8;
            long long dst_offset = alloc_wide_bitint_temp(vm, words);

            emit_lea3(vm, REG_A0, dst_offset);
            emit_mov3(vm, REG_A1, str_addr);
            emit_li3(vm, REG_A2, node->wide_base);
            emit_li3(vm, REG_A3, words);
            emit_li3(vm, REG_A4, node->ty->bit_width);
            emit_wide_helper(vm, "__cccc_bitint_from_str", 5);
            free_temp_reg(str_addr);

            emit_lea3(vm, dest_reg, dst_offset);
        } else {
            emit_li3(vm, dest_reg, node->val);
        }
        return;

    case ND_COMPLEX:
        if (node->val == 1 || node->val == 2) {
            int imag_reg = (dest_reg == FREG_A7) ? FREG_A6 : FREG_A7;
            gen_complex_expr(vm, node->lhs, dest_reg, imag_reg);
            if (node->val == 2)
                emit_fmov3(vm, dest_reg, imag_reg);
            if (node->ty->kind == TY_FLOAT)
                emit_fround_f32(vm, dest_reg, dest_reg);
            return;
        }
        error_tok(vm, node->tok, "unsupported non-complex projection");

    case ND_VAR:
        if (node->var->is_function) {
            // Function name used as value - function-to-pointer decay
            // Emit LTA3 with placeholder, patch later
            Pc addr_loc = emit_lta3(vm, dest_reg, 0); // Placeholder

            // Record patch location for later resolution
            PATCH_GROW(vm, func_addr_patches, num_func_addr_patches, func_addr_patches_cap);
            vm->compiler.func_addr_patches[vm->compiler.num_func_addr_patches]
                .location = addr_loc;
            vm->compiler.func_addr_patches[vm->compiler.num_func_addr_patches]
                .function = node->var;
            vm->compiler.num_func_addr_patches++;
        } else {
            // Stack instrumentation for scalar locals (not arrays/structs/
            // wide _BitInt). Wide _BitInt is address-based like structs, so
            // writes go through a raw pointer (WIDE_* opcode or CALLF helper),
            // never STR_LOCAL — meaning MARKI/MARKW are never emitted for those
            // slots. Excluding wide _BitInt from the read-side checks too keeps
            // the two sides symmetric and avoids the false "uninitialized
            // variable read" trap under CCCC_UNINIT_DETECTION (-2/-3) that
            // ticket #457 reported. (Consistent with structs/unions/arrays,
            // which are already exempt here.)
            if (node->var->is_local && !node->var->is_param &&
                node->var->ty && node->var->ty->kind != TY_ARRAY &&
                node->var->ty->kind != TY_STRUCT &&
                node->var->ty->kind != TY_UNION &&
                !is_wide_bitint(node->var->ty) &&
                !is_decimal(node->var->ty)) { // #402: address-based, same exemption
                if (vm->flags & CCCC_STACK_INSTR)
                    emit_chkl(vm, node->var->offset);
                if (vm->flags & CCCC_UNINIT_DETECTION)
                    emit_chki(vm, node->var->offset);
                if (vm->flags & CCCC_STACK_INSTR)
                    emit_markr(vm, node->var->offset);
            }
            if (is_promoted_local(vm, node->var)) {
                emit_promoted_read(vm, node->var, dest_reg);
                return;
            }
            if (is_fp_promoted_local(vm, node->var)) {
                emit_fp_promoted_read(vm, node->var, dest_reg);
                return;
            }
            // Fused local load: skip the LEA3+LDR two-step for simple locals
            if (is_simple_local_scalar(vm, node)) {
                emit_local_load(vm, node->ty, dest_reg, node->var->offset);
            } else if (is_flonum(node->ty)) {
                // For float types, FREG_A0-A7 have the same raw numbers as
                // REG_A0-A7. Use a temp register to avoid clobbering int regs.
                int r_addr = alloc_temp_reg();
                gen_addr(vm, node, r_addr);
                emit_load_ex(vm, node->ty, dest_reg, r_addr, !addr_is_local_frame(vm, node));
                free_temp_reg(r_addr);
            } else {
                gen_addr(vm, node, dest_reg);
                // For scalars, load the value (wide _BitInt/_Decimal stay as address)
                if (node->ty->kind != TY_ARRAY && node->ty->kind != TY_STRUCT &&
                    node->ty->kind != TY_UNION && !is_wide_bitint(node->ty) &&
                    !is_decimal(node->ty)) {
                    emit_load_ex(vm, node->ty, dest_reg, dest_reg, !addr_is_local_frame(vm, node));
                }
            }
        }
        return;

    case ND_DEREF:
        if (restrict_cache_handle_deref(vm, node, dest_reg))
            return;
        // Scalar-promotion alias reads bypass the address entirely (the
        // pointer is proven to always equal &target, see
        // promotion_alias_add()), so a checked-bounds deref (#770/#484)
        // must not take this path -- fall through to gen_addr below so its
        // CHKR check still runs, same reasoning as restrict_cache_handle_deref
        // declining above.
        if (!((vm->flags & CCCC_CHECKED_BOUNDS) && node->checked_bounds_lo &&
              node->checked_bounds_hi) &&
            promoted_deref_target(vm, node)) {
            emit_promoted_read(vm, promoted_deref_target(vm, node), dest_reg);
            return;
        }
        if (emit_indexed_load_if_possible(vm, node, dest_reg))
            return;
        // Routed through gen_addr() (rather than gen_expr(node->lhs, ...)
        // directly, which computes the identical address) so the checked-
        // pointer bounds check (#770/#484) in gen_addr's own ND_DEREF case
        // runs on this load path too, not just the store/address-of paths.
        gen_addr(vm, node, dest_reg);
        // TY_FUNC: dereferencing a function pointer is a no-op in C — *f and f
        // are interchangeable when f has pointer-to-function type.  Do not emit
        // a data load; the register already holds the callable address.
        //
        // TY_VLA (#971): a deref that *yields* a VLA sub-object (the inner
        // row of `v[i]` in a multi-dimensional VLA, e.g. `int v[n][m]`) is
        // address-based, same as TY_ARRAY -- the just-computed row address
        // IS the value. This is the opposite rule from ND_VAR/TY_VLA below
        // (a VLA *local*'s own frame slot holds an alloca'd pointer that
        // must be loaded) -- do not "fix" that asymmetry, it is correct:
        // a VLA variable is a pointer-in-a-slot, but a VLA sub-object
        // reached by pointer arithmetic is not.
        if (node->ty->kind != TY_ARRAY && node->ty->kind != TY_STRUCT &&
            node->ty->kind != TY_UNION && node->ty->kind != TY_FUNC &&
            node->ty->kind != TY_VLA &&
            !is_wide_bitint(node->ty) && !is_decimal(node->ty)) {
            emit_load(vm, node->ty, dest_reg, dest_reg);
        }
        return;

    case ND_ADDR:
        // #973: for a VLA operand the array's base address IS its value, not
        // the address of the thing holding it. A VLA *variable*'s frame slot
        // holds the alloca'd data pointer -- gen_addr's ND_VAR case would
        // hand back the slot's own address (wrong: `(*p)[i]` through
        // `int (*p)[n] = &v` would read the slot, not the data). A VLA
        // sub-object reached by pointer arithmetic (`&v[1]` in a
        // multi-dimensional VLA) is already address-based, so gen_expr and
        // gen_addr agree there and this early-out is a no-op for it. Routing
        // through gen_expr (rather than special-casing gen_addr's ND_VAR)
        // picks up all three of its slot-address branches -- normal local,
        // block capture, outer-function static chain -- for free. This
        // deliberately skips the MARKP provenance emission below: VLA/alloca
        // storage carries a real AllocHeader resolved via
        // sorted_allocs/DYNOBJSZ (see __builtin_dynamic_object_size in
        // COVERAGE.md), not provenance tracking, and the skipped MARKP would
        // have fired on the wrong address (the slot) with the wrong size
        // (8, the pointer's own size) anyway.
        if (node->lhs->ty && node->lhs->ty->kind == TY_VLA) {
            gen_expr(vm, node->lhs, dest_reg);
            return;
        }
        gen_addr(vm, node->lhs, dest_reg);
        // Track explicit address-of a local var for provenance. Dangling-pointer
        // detection no longer needs address-taken tracking here -- it's now a
        // precise dereference-time range check in op_CHKP3_fn (#670).
        if (node->lhs->kind == ND_VAR && node->lhs->var->is_local &&
            !node->lhs->var->is_block_var) {
            if (vm->flags & CCCC_PROVENANCE_TRACK) {
                size_t var_size = node->lhs->var->ty ? node->lhs->var->ty->size : 8;
                emit_markp(vm, dest_reg, dest_reg, 1 /* STACK */, var_size);
            }
        }
        return;

    case ND_NEG:
        if (is_wide_bitint(node->ty)) {
            gen_wide_bitint_unary(vm, node, dest_reg, "__cccc_bitint_neg");
            return;
        }
        if (is_decimal(node->ty)) {
            int r_src = alloc_temp_reg();
            gen_expr(vm, node->lhs, r_src);
            mark_temp_reg_used(r_src);
            long long dst_offset = node->ret_buffer
                ? (long long)node->ret_buffer->offset
                : alloc_decimal_temp(vm, node->ty->size);
            emit_lea3(vm, REG_A0, dst_offset);
            emit_mov3(vm, REG_A1, r_src);
            emit_li3(vm, REG_A2, dec_width_code(node->ty));
            if (vm->flags & CCCC_POINTER_CHECKS) {
                emit_rr(vm, CHKP3, REG_A0, 0);
                emit_rr(vm, CHKP3, REG_A1, 0);
            }
            emit_wide_op(vm, DNEG);
            emit_lea3(vm, dest_reg, dst_offset);
            free_temp_reg(r_src);
            return;
        }
        gen_expr(vm, node->lhs, dest_reg);
        if (is_flonum(node->ty)) {
            emit_frr(vm, fop_for_type(node->ty, FNEG3), dest_reg, dest_reg);
        } else {
            emit_rr(vm, NEG3, dest_reg, dest_reg);
        }
        return;

    case ND_NOT:
        if (node->lhs && (is_wide_bitint(node->lhs->ty) || is_decimal(node->lhs->ty))) {
            gen_cond_expr(vm, node->lhs, dest_reg); // 0/1
            emit_rr(vm, NOT3, dest_reg, dest_reg);  // logical negate of 0/1
            return;
        }
        gen_expr(vm, node->lhs, dest_reg);
        emit_rr(vm, NOT3, dest_reg, dest_reg);
        return;

    case ND_BITNOT:
        if (is_wide_bitint(node->ty)) {
            gen_wide_bitint_unary(vm, node, dest_reg, "__cccc_bitint_not");
            return;
        }
        gen_expr(vm, node->lhs, dest_reg);
        emit_rr(vm, BNOT3, dest_reg, dest_reg);
        return;

    // Binary arithmetic operations
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
    case ND_LE: {
        if (is_complex(node->lhs->ty) || is_complex(node->rhs->ty)) {
            if (node->kind != ND_EQ && node->kind != ND_NE)
                error_tok(vm, node->tok, "unsupported complex comparison");

            int ar = FREG_A0;
            int ai = FREG_A1;
            int br = FREG_A2;
            int bi = FREG_A3;
            gen_complex_expr(vm, node->lhs, ar, ai);
            gen_complex_expr(vm, node->rhs, br, bi);

            int r_real = alloc_temp_reg();
            int r_imag = alloc_temp_reg();
            emit_frrr(vm, fop_for_type(node->lhs->ty->base, FEQ3), r_real, ar,
                      br);
            emit_frrr(vm, fop_for_type(node->lhs->ty->base, FEQ3), r_imag, ai,
                      bi);
            emit_rrr(vm, AND3, dest_reg, r_real, r_imag);
            if (node->kind == ND_NE)
                emit_rr(vm, NOT3, dest_reg, dest_reg);
            free_temp_reg(r_imag);
            free_temp_reg(r_real);
            return;
        }

        // Check if RHS contains a function call - if so, we need to save LHS
        // because function calls clobber caller-saved temp registers
        bool rhs_has_call = contains_funcall(node->rhs);

        // Evaluate LHS first, then allocate r_rhs. gen_expr frees all temps
        // before returning, so the pool is empty after LHS completes. This keeps
        // peak usage at O(1) per level instead of O(chain-depth), fixing register
        // exhaustion on long left-associative chains (ticket #295).
        if (is_flonum(node->lhs->ty)) {
            // Float operations
            gen_expr(vm, node->lhs,
                     dest_reg); // LHS goes directly to dest (float reg)

            // LHS might contain a function call which resets temp regs.
            // Re-mark dest_reg as used so r_rhs allocation doesn't clobber it.
            mark_temp_reg_used(dest_reg);

            // Operands fed to the final float op, and the temp to release after.
            // Fast path: LHS stays in dest_reg (float), RHS goes to a fresh
            // float temp. Spill path (#587): under register pressure, save the
            // LHS float bits to the stack and reuse dest_reg for the RHS so no
            // temp stays live across the RHS recursion. PSH3/POP3 operate on
            // integer regs, so the bits go through FR2R/R2FR (same idiom as the
            // rhs_has_call branch below).
            int r_lhs_op, r_rhs_op, r_free;
            if (temp_regs_free() <= TEMP_REG_SPILL_THRESHOLD) {
                int r_tmp = alloc_temp_reg();
                emit_rr(vm, fop_for_type(node->lhs->ty, FR2R), r_tmp,
                        dest_reg);     // LHS float bits -> int reg
                emit_psh3(vm, r_tmp);  // save LHS on the stack
                free_temp_reg(r_tmp);  // nothing held across the RHS recursion
                gen_expr(vm, node->rhs, dest_reg); // RHS reuses dest; pool free
                r_free = alloc_temp_reg();
                emit_pop3(vm, r_free); // reload LHS bits into int reg
                emit_rr(vm, fop_for_type(node->lhs->ty, R2FR), r_free,
                        r_free);       // int bits -> float reg (in place)
                r_lhs_op = r_free;
                r_rhs_op = dest_reg;
            } else {
                int r_rhs = alloc_temp_reg();
                if (rhs_has_call) {
                    // For floats: convert to int, push to stack, evaluate RHS,
                    // pop, convert back. dest_reg is FREG_*, so we use FR2R to
                    // move bits to an int temp.
                    int r_temp = alloc_temp_reg();
                    emit_rr(vm, fop_for_type(node->lhs->ty, FR2R), r_temp,
                            dest_reg); // Float bits -> int reg
                    emit_psh3(vm, r_temp);               // Push int reg to stack
                    gen_expr(vm, node->rhs,
                             r_rhs);       // Evaluate RHS (may clobber all)
                    emit_pop3(vm, r_temp); // Pop saved bits into int reg
                    emit_rr(vm, fop_for_type(node->lhs->ty, R2FR), dest_reg,
                            r_temp); // Int bits -> float reg
                    free_temp_reg(r_temp);
                } else {
                    gen_expr(vm, node->rhs, r_rhs);
                }
                r_lhs_op = dest_reg;
                r_rhs_op = r_rhs;
                r_free = r_rhs;
            }

            int fop;
            switch (node->kind) {
            case ND_ADD:
                fop = FADD3;
                break;
            case ND_SUB:
                fop = FSUB3;
                break;
            case ND_MUL:
                fop = FMUL3;
                break;
            case ND_DIV:
                fop = FDIV3;
                break;
            case ND_EQ:
                fop = FEQ3;
                break;
            case ND_NE:
                fop = FNE3;
                break;
            case ND_LT:
                fop = FLT3;
                break;
            case ND_LE:
                fop = FLE3;
                break;
            default:
                error("unsupported float op");
            }
            emit_frrr(vm, fop_for_type(node->lhs->ty, fop), dest_reg, r_lhs_op,
                      r_rhs_op);
            free_temp_reg(r_free);
        } else if (is_decimal(node->lhs->ty) || is_decimal(node->ty)) {
            // _Decimal32/64/128 (#402): address-based like wide _BitInt, but
            // dispatches to the dedicated DADD/DSUB/DMUL/DDIV/DCMP opcodes
            // (fixed A-register convention, fully opaque to the optimizer --
            // see the OPS_X comment in cccc.h) instead of a CALLF helper.
            // usual_arith_conv already rejected mixing decimal with a binary
            // float or _BitInt operand, so node->lhs->ty == node->rhs->ty
            // (modulo decimal-vs-decimal rank) here.
            Type *operand_ty = node->lhs->ty;
            int w = dec_width_code(operand_ty);
            bool is_cmp = (node->kind == ND_EQ || node->kind == ND_NE ||
                           node->kind == ND_LT || node->kind == ND_LE);

            // dest_reg may be REG_ZERO (a discarded-value expression
            // statement, e.g. `(void)(a+b);`): REG_ZERO is hardwired to
            // always read back 0, so staging an address through it here
            // would silently produce a null pointer. Fall back to a fresh
            // temp in that case -- everything else still writes the final
            // result to dest_reg, which is the correct discard target.
            int work_reg = (dest_reg == REG_ZERO) ? alloc_temp_reg() : dest_reg;
            gen_expr(vm, node->lhs, work_reg); // decimal operand -> its address
            emit_psh3(vm, work_reg);
            gen_expr(vm, node->rhs, work_reg); // temp pool is empty here
            // The RHS recursion may contain a call whose emit_wide_helper
            // CALLF resets the whole temp-reg bitmap, marking work_reg's bit
            // free again even though it still holds the live RHS value.
            // Re-mark it used so alloc_temp_reg() below can't hand out the
            // same register for r_rhs (which would then get silently
            // clobbered once r_lhs is popped into that same slot).
            mark_temp_reg_used(work_reg);
            int r_rhs = alloc_temp_reg();
            emit_mov3(vm, r_rhs, work_reg);
            if (work_reg != dest_reg)
                free_temp_reg(work_reg);
            int r_lhs = alloc_temp_reg();
            emit_pop3(vm, r_lhs);

            if (is_cmp) {
                emit_mov3(vm, REG_A0, r_lhs);
                emit_mov3(vm, REG_A1, r_rhs);
                emit_li3(vm, REG_A2, w);
                if (vm->flags & CCCC_POINTER_CHECKS) {
                    emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_rr(vm, CHKP3, REG_A1, 0);
                }
                emit_wide_op(vm, DCMP); // A0 = 0=EQ/1=LT/2=GT/3=UNORDERED
                int code = alloc_temp_reg();
                emit_mov3(vm, code, REG_A0);
                int tmp = alloc_temp_reg();
                switch (node->kind) {
                case ND_EQ:
                    emit_li3(vm, tmp, 0);
                    emit_rrr(vm, SEQ3, dest_reg, code, tmp);
                    break;
                case ND_NE:
                    emit_li3(vm, tmp, 0);
                    emit_rrr(vm, SNE3, dest_reg, code, tmp);
                    break;
                case ND_LT:
                    emit_li3(vm, tmp, 1);
                    emit_rrr(vm, SEQ3, dest_reg, code, tmp);
                    break;
                case ND_LE: {
                    // LE is EQ(0) or LT(1) -- i.e. code <= 1, which also
                    // correctly excludes GT(2) and UNORDERED(3). code is
                    // always in [0,3], so plain signed SLE3 is exact here.
                    int lim = alloc_temp_reg();
                    emit_li3(vm, lim, 1);
                    emit_rrr(vm, SLE3, dest_reg, code, lim);
                    free_temp_reg(lim);
                    break;
                }
                default: break;
                }
                free_temp_reg(tmp);
                free_temp_reg(code);
            } else {
                long long dst_offset;
                if (node->ret_buffer)
                    dst_offset = (long long)node->ret_buffer->offset;
                else
                    dst_offset = alloc_decimal_temp(vm, operand_ty->size);

                int decimal_op;
                switch (node->kind) {
                case ND_ADD: decimal_op = DADD; break;
                case ND_SUB: decimal_op = DSUB; break;
                case ND_MUL: decimal_op = DMUL; break;
                case ND_DIV: decimal_op = DDIV; break;
                default:
                    error_tok(vm, node->tok, "unsupported _Decimal operator");
                    decimal_op = DADD;
                }

                emit_lea3(vm, REG_A0, dst_offset);
                emit_mov3(vm, REG_A1, r_lhs);
                emit_mov3(vm, REG_A2, r_rhs);
                emit_li3(vm, REG_A3, w);
                if (vm->flags & CCCC_POINTER_CHECKS) {
                    emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_rr(vm, CHKP3, REG_A1, 0);
                    emit_rr(vm, CHKP3, REG_A2, 0);
                }
                emit_wide_op(vm, decimal_op);
                emit_lea3(vm, dest_reg, dst_offset);
            }
            free_temp_reg(r_rhs);
            free_temp_reg(r_lhs);
        } else if (is_wide_bitint(node->lhs->ty) || is_wide_bitint(node->ty)) {
            // Wide _BitInt operations: delegate to runtime helpers.
            // LHS and RHS are wide → each gen_expr returns an address.
            // Comparison result is scalar; arithmetic result is wide.
            Type *operand_ty = node->lhs->ty;
            bool wide_op = is_wide_bitint(operand_ty);

            // See the matching decimal-binop REG_ZERO note above: dest_reg
            // may be a discarded-value expression statement, so stage
            // through a fresh temp in that case rather than REG_ZERO.
            int work_reg = (dest_reg == REG_ZERO) ? alloc_temp_reg() : dest_reg;
            gen_expr(vm, node->lhs, work_reg);
            emit_psh3(vm, work_reg);
            gen_expr(vm, node->rhs, work_reg); // temp pool is empty here
            // See the decimal-binop comment above: a CALLF inside the RHS
            // (emit_wide_helper, e.g. a bitwise/comparison wide-_BitInt op)
            // resets the temp-reg bitmap, so re-mark work_reg used before
            // anything else can be allocated over it.
            mark_temp_reg_used(work_reg);
            int r_rhs = alloc_temp_reg();
            emit_mov3(vm, r_rhs, work_reg);
            if (work_reg != dest_reg)
                free_temp_reg(work_reg);
            int r_lhs = alloc_temp_reg();
            emit_pop3(vm, r_lhs);

            int words  = wide_op ? (operand_ty->size / 8) : 0;
            int width  = wide_op ? operand_ty->bit_width  : 0;
            bool is_signed = wide_op ? !operand_ty->is_unsigned : false;

            // For arithmetic/bitwise ops: allocate a stack temp for the result.
            long long dst_offset = 0;
            bool is_cmp = (node->kind == ND_EQ || node->kind == ND_NE ||
                           node->kind == ND_LT || node->kind == ND_LE);
            if (!is_cmp && wide_op) {
                if (node->ret_buffer) {
                    dst_offset = (long long)node->ret_buffer->offset;
                } else {
                    dst_offset = alloc_wide_bitint_temp(vm, words);
                }
            }

            // ND_ADD/SUB/MUL/DIV/MOD/SHL/SHR dispatch to dedicated WIDE_*
            // opcodes (#456) instead of a CALLF into the runtime helper —
            // AND/OR/XOR/comparisons stay on the CALLF/emit_wide_helper path.
            int wide_opcode = 0;
            switch (node->kind) {
            case ND_ADD: wide_opcode = WIDE_ADD; break;
            case ND_SUB: wide_opcode = WIDE_SUB; break;
            case ND_MUL: wide_opcode = WIDE_MUL; break;
            case ND_DIV: wide_opcode = WIDE_DIV; break;
            case ND_MOD: wide_opcode = WIDE_MOD; break;
            case ND_SHL: wide_opcode = WIDE_SHL; break;
            case ND_SHR: wide_opcode = is_signed ? WIDE_SHR : WIDE_USHR; break;
            default: break;
            }

            const char *fn = NULL;
            switch (node->kind) {
            case ND_BITAND: fn = "__cccc_bitint_and";  break;
            case ND_BITOR:  fn = "__cccc_bitint_or";   break;
            case ND_BITXOR: fn = "__cccc_bitint_xor";  break;
            case ND_EQ: case ND_NE: case ND_LT: case ND_LE:
                fn = "__cccc_bitint_cmp"; break;
            case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
            case ND_SHL: case ND_SHR:
                break; // handled via wide_opcode above
            default:
                error_tok(vm, node->tok, "unsupported wide _BitInt op");
            }

            if (node->kind == ND_SHL || node->kind == ND_SHR) {
                // Shift: dst(A0), src_addr(A1), shift_amount(A2), words(A3), width(A4)
                emit_lea3(vm, REG_A0, dst_offset);
                emit_mov3(vm, REG_A1, r_lhs);
                emit_mov3(vm, REG_A2, r_rhs); // shift amount is scalar (A2)
                emit_li3(vm, REG_A3, words);
                emit_li3(vm, REG_A4, width);
                if (vm->flags & CCCC_POINTER_CHECKS) {
                    emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_rr(vm, CHKP3, REG_A1, 0);
                }
                emit_wide_op(vm, wide_opcode);
                emit_lea3(vm, dest_reg, dst_offset);
            } else if (is_cmp) {
                // cmp(a, b, words, width, is_signed) → {-1, 0, 1} in REG_A0
                emit_mov3(vm, REG_A0, r_lhs);
                emit_mov3(vm, REG_A1, r_rhs);
                emit_li3(vm, REG_A2, words);
                emit_li3(vm, REG_A3, width);
                emit_li3(vm, REG_A4, is_signed ? 1 : 0);
                emit_wide_helper(vm, fn, 5);
                // Convert cmp result to bool per operator
                int tmp = alloc_temp_reg();
                emit_li3(vm, tmp, 0);
                switch (node->kind) {
                case ND_EQ: emit_rrr(vm, SEQ3, dest_reg, REG_A0, tmp); break;
                case ND_NE: emit_rrr(vm, SNE3, dest_reg, REG_A0, tmp); break;
                case ND_LT: emit_rrr(vm, SLT3, dest_reg, REG_A0, tmp); break;
                case ND_LE: emit_rrr(vm, SLE3, dest_reg, REG_A0, tmp); break;
                default: break;
                }
                free_temp_reg(tmp);
            } else if (node->kind == ND_BITAND || node->kind == ND_BITOR ||
                       node->kind == ND_BITXOR) {
                // Bitwise: dst(A0), a(A1), b(A2), words(A3), width(A4)
                emit_lea3(vm, REG_A0, dst_offset);
                emit_mov3(vm, REG_A1, r_lhs);
                emit_mov3(vm, REG_A2, r_rhs);
                emit_li3(vm, REG_A3, words);
                emit_li3(vm, REG_A4, width);
                emit_wide_helper(vm, fn, 5);
                emit_lea3(vm, dest_reg, dst_offset);
            } else {
                // Arithmetic: dst(A0), a(A1), b(A2), words(A3), width(A4)
                // (DIV/MOD additionally need is_signed in A5)
                emit_lea3(vm, REG_A0, dst_offset);
                emit_mov3(vm, REG_A1, r_lhs);
                emit_mov3(vm, REG_A2, r_rhs);
                emit_li3(vm, REG_A3, words);
                emit_li3(vm, REG_A4, width);
                if (node->kind == ND_DIV || node->kind == ND_MOD)
                    emit_li3(vm, REG_A5, is_signed ? 1 : 0);
                if (vm->flags & CCCC_POINTER_CHECKS) {
                    emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_rr(vm, CHKP3, REG_A1, 0);
                    emit_rr(vm, CHKP3, REG_A2, 0);
                }
                emit_wide_op(vm, wide_opcode);
                emit_lea3(vm, dest_reg, dst_offset);
            }
            free_temp_reg(r_rhs);
            free_temp_reg(r_lhs);
        } else {
            // Narrow integer operations (scalar _BitInt or plain int)
            gen_expr(vm, node->lhs, dest_reg); // LHS goes directly to dest

            // LHS might contain a function call which resets temp regs.
            // Re-mark dest_reg as used so r_rhs allocation doesn't clobber it.
            mark_temp_reg_used(dest_reg);

            // Operands fed to the final op, and the temp to release afterwards.
            // Fast path: LHS stays in dest_reg, RHS goes to a fresh temp.
            // Spill path (#587): under register pressure, save LHS to the stack
            // and reuse dest_reg for the RHS so no temp stays live across the
            // RHS recursion — bounds peak register use on deeply nested trees.
            // The spill path also subsumes the rhs_has_call case (PSH3 protects
            // the LHS across any call inside the RHS).
            int r_lhs_op, r_rhs_op, r_free;
            if (temp_regs_free() <= TEMP_REG_SPILL_THRESHOLD) {
                emit_psh3(vm, dest_reg);            // save LHS
                gen_expr(vm, node->rhs, dest_reg);  // RHS reuses dest; pool free
                r_free = alloc_temp_reg();
                emit_pop3(vm, r_free);              // reload LHS
                r_lhs_op = r_free;
                r_rhs_op = dest_reg;
            } else {
                int r_rhs = alloc_temp_reg();
                if (rhs_has_call) {
                    // Save LHS to stack before function call in RHS
                    emit_psh3(vm, dest_reg);
                    gen_expr(vm, node->rhs, r_rhs);
                    // Restore saved LHS from stack
                    emit_pop3(vm, dest_reg);
                } else {
                    gen_expr(vm, node->rhs, r_rhs);
                }
                r_lhs_op = dest_reg;
                r_rhs_op = r_rhs;
                r_free = r_rhs;
            }

            int op;
            bool checked_arith = (vm->flags & CCCC_OVERFLOW_CHECKS) &&
                                 !node->ty->is_unsigned &&
                                 !(node->lhs->ty && node->lhs->ty->base);
            switch (node->kind) {
            case ND_ADD:
                op = checked_arith ? ADDC : ADD3;
                break;
            case ND_SUB:
                op = checked_arith ? SUBC : SUB3;
                break;
            case ND_MUL:
                op = checked_arith ? MULC : MUL3;
                break;
            case ND_DIV:
                op = node->ty->is_unsigned ? UDIV3 : (checked_arith ? DIVC : DIV3);
                break;
            case ND_MOD:
                op = node->ty->is_unsigned ? UMOD3 : MOD3;
                break;
            case ND_BITAND:
                op = AND3;
                break;
            case ND_BITOR:
                op = OR3;
                break;
            case ND_BITXOR:
                op = XOR3;
                break;
            case ND_SHL:
                op = SHL3;
                break;
            case ND_SHR:
                op = node->ty->is_unsigned ? USHR3 : SHR3;
                break;
            case ND_EQ:
                op = SEQ3;
                break;
            case ND_NE:
                op = SNE3;
                break;
            case ND_LT:
                op = SLT3;
                break;
            case ND_LE:
                op = SLE3;
                break;
            default:
                error("unsupported int op");
            }
            // For pointer add/sub, emit bounds check (CHKB/CHKBN) before the
            // add and provenance check (CHKPA) after.
            bool is_ptr_arith = (node->kind == ND_ADD || node->kind == ND_SUB) &&
                                node->lhs->ty && node->lhs->ty->base;
            // #982 (defect A): a pointer DIFFERENCE (`&a - &b`, result type
            // ptrdiff_t/long, not a pointer) is also an ND_SUB with a
            // pointer lhs, but r_rhs_op here holds the *subtrahend's own
            // address* (new_sub's ptr-ptr arm divides the raw SUB by the
            // element size in a separate step, src/parse.c), not a scaled
            // byte offset -- CHKB/CHKBN would then compare an address
            // against an allocation's size and virtually always "fail".
            // Detected by node->ty (the ND_SUB's own result type) rather
            // than node->rhs->ty->base, confirmed against a real -a AST
            // dump: ptr-int wraps its scaled offset in a `CAST :: T*`, so
            // rhs->ty->base alone can't tell the two apart, but the SUB
            // node's own type can (pointer for ptr-int, integer for
            // ptr-ptr). CHKPA (below) is deliberately NOT gated on this --
            // see its own comment.
            bool is_ptr_diff = node->kind == ND_SUB &&
                               node->lhs->ty && node->lhs->ty->base &&
                               node->ty && !node->ty->base;
            if (is_ptr_arith && !is_ptr_diff && (vm->flags & CCCC_BOUNDS_CHECKS))
                emit_rr(vm, node->kind == ND_SUB ? CHKBN : CHKB, r_lhs_op, r_rhs_op);

            // Unsigned 64-bit comparison: use dedicated ULT3/ULE3 opcodes.
            // Shorter unsigned types (≤32-bit) are zero-extended in 64-bit
            // registers so SLT3/SLE3 already give the correct signed result.
            bool is_u64_cmp =
                (node->kind == ND_LT || node->kind == ND_LE) &&
                node->lhs->ty && node->lhs->ty->is_unsigned &&
                node->lhs->ty->size == 8;
            if (is_u64_cmp) {
                op = (node->kind == ND_LT) ? ULT3 : ULE3;
            }

            emit_rrr(vm, op, dest_reg, r_lhs_op, r_rhs_op);

            if (node->ty->kind == TY_BITINT)
                emit_bitint_trunc(vm, node->ty, dest_reg);

            // #982: still gated on is_ptr_arith alone (not !is_ptr_diff) --
            // narrowing this too was deliberately deferred rather than
            // folded in as a side effect of the CHKB/CHKBN fix above. See
            // tests/test_ptr_diff_*_provenance*.c for the -3 verification
            // this comment refers to.
            if (is_ptr_arith && (vm->flags & (CCCC_INVALID_ARITH | CCCC_PROVENANCE_TRACK))) {
                emit(vm, CHKPA);
                emit_word(vm, ENCODE_R(dest_reg));
            }
            free_temp_reg(r_free);
        }

        return;
    }

    case ND_ASSIGN: {
        // See the matching guard in gen_expr's ND_DEREF case: a checked-
        // bounds store must not take the promotion-alias fast path either,
        // for the same CHKR-bypass reason.
        Obj *lhs_promoted_deref =
            ((vm->flags & CCCC_CHECKED_BOUNDS) && node->lhs->checked_bounds_lo &&
             node->lhs->checked_bounds_hi)
                ? NULL
                : promoted_deref_target(vm, node->lhs);
        if (lhs_promoted_deref) {
            int r_val = dest_reg == REG_ZERO ? alloc_temp_reg() : dest_reg;
            bool need_free = dest_reg == REG_ZERO;
            gen_expr(vm, node->rhs, r_val);
            emit_promoted_write(vm, lhs_promoted_deref, r_val);
            if (!lhs_promoted_deref->is_param &&
                lhs_promoted_deref->ty &&
                lhs_promoted_deref->ty->kind != TY_ARRAY &&
                lhs_promoted_deref->ty->kind != TY_STRUCT &&
                lhs_promoted_deref->ty->kind != TY_UNION) {
                if (vm->flags & CCCC_STACK_INSTR)
                    emit_markw(vm, lhs_promoted_deref->offset);
                if (vm->flags & CCCC_UNINIT_DETECTION)
                    emit_marki(vm, lhs_promoted_deref->offset);
            }
            if (need_free)
                free_temp_reg(r_val);
            return;
        }

        // IMPORTANT: Evaluate RHS *before* computing LHS address!
        // If RHS is a function call, it will clobber temp registers.
        // Computing LHS address after ensures we get a fresh temp reg.

        // For struct/union assignments, we need memcpy (both LHS and RHS are
        // addresses)
        if (node->ty &&
            (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION ||
             is_wide_bitint(node->ty) || is_decimal(node->ty))) {
            // Struct/union/wide-_BitInt/_Decimal assignment: memcpy from
            // RHS to LHS (#402: decimal is address-based, same as these)
            int r_src = alloc_temp_reg();
            gen_expr(vm, node->rhs, r_src); // RHS is address
            mark_temp_reg_used(r_src);

            // If the LHS address expression contains a call, gen_addr below will
            // clobber every caller-saved temp at runtime, destroying the RHS
            // address held in r_src (e.g. `arr[f()] = some_struct;`). Spill it
            // across the address computation and reload afterwards. (#581)
            bool src_has_call = expr_has_call(node->lhs);
            long long r_src_spill = 0;
            if (src_has_call) {
                r_src_spill = alloc_wide_bitint_temp(vm, 1);
                emit_local_store(vm, ty_long, r_src, r_src_spill);
            }

            int r_dest = alloc_temp_reg();
            gen_addr(vm, node->lhs, r_dest); // LHS address

            if (src_has_call) {
                mark_temp_reg_used(r_dest);
                int r_reload = alloc_temp_reg();
                emit_local_load(vm, ty_long, r_reload, r_src_spill);
                free_temp_reg(r_src);
                r_src = r_reload;
            }

            // #939: a store through a [[cccc::ntarray]] + count(n) pointer's
            // widened terminator slot must stay all-zero-bytes for the
            // struct/union/wide-_BitInt/_Decimal pointees that lower to this
            // memcpy path -- CHKNT (the scalar guard below) can't check
            // these, since their value never passes through a single
            // register. CHKNTZ scans r_src (the RHS address, already
            // computed above) BEFORE the MCPY runs, so the slot is never
            // actually clobbered when this traps. node->ty->size (MCPY's
            // count) and node->lhs->checked_access_size are structurally
            // the same number here -- both are get_vm_size() of the same
            // pointee type (src/parse.c's get_vm_size() is just ty->size) --
            // so no CHKR/CHKNT-widened-hi vs. MCPY-size mismatch is possible.
            if ((vm->flags & CCCC_CHECKED_BOUNDS) && node->lhs->kind == ND_DEREF &&
                node->lhs->checked_nt_terminator) {
                mark_temp_reg_used(r_dest);
                mark_temp_reg_used(r_src);
                int r_hi = gen_checked_nt_hi(vm, node->lhs);
                emit_chkntz(vm, r_dest, r_hi, r_src, node->lhs->checked_access_size);
                free_temp_reg(r_hi);
            }

            // #983: struct/union/wide-_BitInt/_Decimal assignment reaches
            // memory purely through this MCPY, bypassing emit_load_ex/
            // emit_store_ex entirely -- so it needs its own CHKD pair,
            // dereference-checking both the destination (a store) and the
            // source (a load) address. Skipped for a compile-time-known
            // local-frame address, same rule as emit_load_ex/emit_store_ex's
            // dangling_check.
            if (vm->flags & CCCC_BOUNDS_CHECKS) {
                mark_temp_reg_used(r_dest);
                mark_temp_reg_used(r_src);
                if (!addr_is_local_frame(vm, node->lhs))
                    emit_rri(vm, CHKD, r_dest, 0, (long long)node->ty->size);
                if (!addr_is_local_frame(vm, node->rhs))
                    emit_rri(vm, CHKD, r_src, 0, (long long)node->ty->size);
            }

            // MCPY: REG_A0=dest, REG_A1=src, REG_A2=size
            emit_mov3(vm, REG_A0, r_dest);
            emit_mov3(vm, REG_A1, r_src);
            emit_li3(vm, REG_A2, node->ty->size);
            emit(vm, MCPY);

            free_temp_reg(r_src);
            free_temp_reg(r_dest);

            // Assignment expression result is the destination address
            if (dest_reg != REG_ZERO) {
                emit_mov3(vm, dest_reg, REG_A0);
            }
            return;
        }

        // First, evaluate RHS into a temporary or dest_reg
        int r_val = dest_reg;
        bool need_free = false;
        // Use a temp reg if dest_reg is zero or if we need to preserve it
        // (though dest_reg is output) But critically, if LHS is a bitfield, we
        // definitely need temp regs for RMW
        if (dest_reg == REG_ZERO ||
            (node->lhs->kind == ND_MEMBER && node->lhs->member->is_bitfield)) {
            r_val = alloc_temp_reg();
            need_free = true;
        }
        gen_expr(vm, node->rhs, r_val);

        // CRITICAL: If RHS contained a function call, reset_temp_regs() was
        // called. We need to re-mark r_val as in-use before allocating r_addr!
        mark_temp_reg_used(r_val);

        bool rhs_promoted_addr = node->lhs->kind == ND_VAR &&
                                 node->lhs->var->is_local &&
                                 node->lhs->var->name &&
                                 node->lhs->var->name[0] == '\0' &&
                                 node->rhs &&
                                 node->rhs->kind == ND_ADDR &&
                                 node->rhs->lhs &&
                                 node->rhs->lhs->kind == ND_VAR &&
                                 is_promoted_local(vm, node->rhs->lhs->var);

        // Fused local store: skip LEA3+STR for simple locals
        bool lhs_fused = node->lhs->kind == ND_VAR &&
                         is_simple_local_scalar(vm, node->lhs);
        IndexedAddr lhs_idx_check = {};
        bool lhs_indexed = node->lhs->kind == ND_DEREF &&
                           match_indexed_addr(vm, node->lhs->lhs, &lhs_idx_check) &&
                           !expr_has_call(lhs_idx_check.base) &&
                           !expr_has_call(lhs_idx_check.index);
        // If the LHS address expression itself contains a function call (e.g.
        // `form[strlen(form) - 1] = 's'`), that call clobbers every
        // caller-saved temp register at runtime — including the one holding the
        // already-evaluated RHS in r_val. (reset_temp_regs() inside the call's
        // codegen mirrors this real clobber.) Spill r_val to a one-word stack
        // slot across the address computation and reload it afterwards so the
        // store sees the correct value. (#581)
        bool lhs_has_call = !lhs_fused && expr_has_call(node->lhs);
        long long r_val_spill = 0;
        if (lhs_has_call) {
            r_val_spill = alloc_wide_bitint_temp(vm, 1);
            emit_local_store(vm, node->ty, r_val, r_val_spill);
        }

        int r_addr = -1;
        if (!lhs_fused && !lhs_indexed) {
            // Now compute LHS address (after any function calls in RHS are done)
            r_addr = alloc_temp_reg();
            gen_addr(vm, node->lhs, r_addr);
        }

        if (lhs_has_call) {
            // The address is now in r_addr; reload r_val into a fresh temp that
            // is guaranteed distinct from r_addr. (The old r_val register was
            // clobbered by the call and its allocator bit cleared by the reset.)
            if (r_addr >= 0)
                mark_temp_reg_used(r_addr);
            int r_reload = alloc_temp_reg();
            emit_local_load(vm, node->ty, r_reload, r_val_spill);
            if (need_free)
                free_temp_reg(r_val);
            r_val = r_reload;
            need_free = true;
        }

        // #923: a store through a [[cccc::ntarray]] + count(n) pointer's
        // widened terminator slot must stay null -- CHKR (emitted by gen_addr
        // above, inside the r_addr computation) already range-checked r_addr;
        // this only re-checks the stored *value*, which gen_addr has no
        // access to. Runs on the ND_DEREF-lhs store path only: lhs_fused is
        // ND_VAR-only and lhs_indexed's match_indexed_addr() already declines
        // under CCCC_CHECKED_BOUNDS (CCCC_FUSION_UNSAFE_FLAGS,
        // src/codegen_regalloc.c's #770/#484 fusion-gate comment), so an ND_DEREF
        // checked store always has r_addr >= 0 and reaches the standard
        // emit_store_ex below -- verified by the -O2/-O3 tests, not assumed.
        if ((vm->flags & CCCC_CHECKED_BOUNDS) && node->lhs->kind == ND_DEREF &&
            node->lhs->checked_nt_terminator && r_addr >= 0) {
            mark_temp_reg_used(r_addr);
            mark_temp_reg_used(r_val);
            // #939: a float/double r_val is a FReg index, not an int one --
            // CHKNT's value operand is read as an integer register, so
            // transfer the raw bits into a fresh int temp first (same
            // FR2R/FR2R_F32 idiom used elsewhere for a flat-double value
            // that needs to cross into an int register). TY_LDOUBLE never
            // reaches here with checked_nt_terminator set (the parse-time
            // gate excludes it, src/parse.c's checked_nt_pointee_supported())
            // -- an 8-byte FR2R read would silently ignore its other 8 bytes.
            int r_ntval = r_val;
            bool free_ntval = false;
            if (is_flonum(node->lhs->ty)) {
                r_ntval = alloc_temp_reg();
                emit_rr(vm, fop_for_type(node->lhs->ty, FR2R), r_ntval, r_val);
                free_ntval = true;
            }
            // #945: gen_checked_nt_hi() re-runs the object-expression hoist
            // init before reading `hi` -- see the gen_addr ND_DEREF CHKR
            // site's identical comment. r_addr's own CHKR (emitted by
            // gen_addr while computing it) already ran this same init once;
            // re-running it here is idempotent, not redundant work that
            // could be skipped -- this site must not assume gen_addr's CHKR
            // always fires first.
            int r_hi = gen_checked_nt_hi(vm, node->lhs);
            emit_chknt(vm, r_addr, r_hi, r_ntval, node->lhs->checked_access_size);
            free_temp_reg(r_hi);
            if (free_ntval)
                free_temp_reg(r_ntval);
        }

        // #653: a store through a union member must clear (not stamp) the
        // type shadow for the accessed range -- see emit_store_ex's doc
        // comment and is_union_member_access above.
        bool lhs_saved_union_flag = vm->compiler.in_union_member_access;
        vm->compiler.in_union_member_access = is_union_member_access(node->lhs);

        // Handle Bitfields specially (Read-Modify-Write)
        if (node->lhs->kind == ND_MEMBER && node->lhs->member->is_bitfield) {
            Member *mem = node->lhs->member;
            int r_container = alloc_temp_reg();
            bool lhs_local_frame = addr_is_local_frame(vm, node->lhs);

            // Load container value
            emit_load_ex(vm, mem->ty, r_container,
                         r_addr, !lhs_local_frame); // Use member type (container)

            // Clear the bitfield bits: container &= ~(mask << bit_offset)
            int r_mask = alloc_temp_reg();
            long long mask = ((1ULL << mem->bit_width) - 1);
            emit_li3(vm, r_mask, ~(mask << mem->bit_offset));
            emit_rrr(vm, AND3, r_container, r_container, r_mask);

            // Prepare new value: (val & mask) << bit_offset
            int r_new = alloc_temp_reg();
            emit_mov3(vm, r_new, r_val);
            emit_li3(vm, r_mask, mask); // Reuse r_mask for positive mask
            emit_rrr(vm, AND3, r_new, r_new, r_mask); // Truncate val to width
            // Shift new value into position
            if (mem->bit_offset > 0) {
                int r_shift = alloc_temp_reg();
                emit_li3(vm, r_shift, mem->bit_offset);
                emit_rrr(vm, SHL3, r_new, r_new, r_shift);
                free_temp_reg(r_shift);
            }

            // OR new value into container
            emit_rrr(vm, OR3, r_container, r_container, r_new);

            // Store back
            emit_store_ex(vm, mem->ty, r_container,
                          r_addr, !lhs_local_frame); // Use member type (container)

            free_temp_reg(r_new);
            free_temp_reg(r_mask);
            free_temp_reg(r_container);
        } else if (node->lhs->kind == ND_VAR &&
                   is_promoted_local(vm, node->lhs->var)) {
            emit_promoted_write(vm, node->lhs->var, r_val);
        } else if (node->lhs->kind == ND_VAR &&
                   is_fp_promoted_local(vm, node->lhs->var)) {
            emit_fp_promoted_write(vm, node->lhs->var, r_val);
        } else if (lhs_indexed &&
                   emit_indexed_store_if_possible(vm, node->lhs, node->ty,
                                                  r_val)) {
            // stored by fused indexed opcode
        } else if (lhs_fused) {
            emit_local_store(vm, node->ty, r_val, node->lhs->var->offset);
        } else {
            // Standard store
            emit_store_ex(vm, node->ty, r_val, r_addr, !addr_is_local_frame(vm, node->lhs));
        }
        vm->compiler.in_union_member_access = lhs_saved_union_flag;

        // #944: assignment-time bounds implication (Checked C's
        // _Assume_bounds_cast direction) -- verify the value just stored
        // into a declared-checked lhs actually satisfies the lhs's OWN
        // declared bounds, given a declared-checked rhs. Must run AFTER the
        // store above: checked_assign_dst_lo/hi are the lhs's own bounds
        // expressions, deliberately left unevaluated by
        // verify_checked_assign_rewrite() (src/parse.c) so gen_expr reads
        // the lhs's just-written value here, not its pre-assignment one --
        // the inverse ordering from checked_assign_src_lo/hi, which were
        // already snapshotted into temps BEFORE the store by that same
        // rewrite (the source may alias or be overwritten by the store
        // itself). r_val/r_addr are marked in-use first since dest_reg's
        // final mov and the r_addr free below both still need them live
        // across this block's own temp allocations.
        if ((vm->flags & CCCC_CHECKED_BOUNDS) && node->checked_assign_dst_lo) {
            mark_temp_reg_used(r_val);
            if (r_addr >= 0)
                mark_temp_reg_used(r_addr);
            int r_slo = alloc_temp_reg();
            gen_expr(vm, node->checked_assign_src_lo, r_slo);
            mark_temp_reg_used(r_slo);
            int r_shi = alloc_temp_reg();
            gen_expr(vm, node->checked_assign_src_hi, r_shi);
            mark_temp_reg_used(r_shi);

            int r_dlo = alloc_temp_reg();
            // #947: re-run the target's object-expression hoist init (if
            // any) before reading dst_lo/hi -- they may read it back
            // through `*t`. Result discarded (r_dlo is free scratch); only
            // the store to `t` matters. Must happen here, after the store
            // above, not folded into the pre-store src snapshot -- see
            // Node.checked_assign_dst_obj_init's comment (src/cccc.h).
            if (node->checked_assign_dst_obj_init)
                gen_expr(vm, node->checked_assign_dst_obj_init, r_dlo);
            gen_expr(vm, node->checked_assign_dst_lo, r_dlo);
            emit_chkab(vm, r_dlo, r_slo, r_shi, false);
            free_temp_reg(r_dlo);

            int r_dhi = alloc_temp_reg();
            gen_expr(vm, node->checked_assign_dst_hi, r_dhi);
            emit_chkab(vm, r_dhi, r_slo, r_shi, true);
            free_temp_reg(r_dhi);

            free_temp_reg(r_shi);
            free_temp_reg(r_slo);
        }

        // Update or invalidate the restrict cache for this store.
        restrict_cache_handle_store(vm, node->lhs, r_val);

        // Stack instrumentation: record write and mark initialized (scalars
        // only). A VLA declaration's lhs is ND_VLA_PTR, not ND_VAR (its
        // lowering is ND_ASSIGN(ND_VLA_PTR, alloca(...)) -- see parse.c) but
        // it still writes the same frame slot a later ND_VAR read of the
        // VLA loads through, so it needs the identical MARKI or that read's
        // CHKI trips a false UNINITIALIZED VARIABLE READ (#980). Unlike
        // ND_VAR, ND_VLA_PTR only ever names a non-param local VLA, so no
        // further guard is needed here.
        if (node->lhs->kind == ND_VLA_PTR) {
            if (vm->flags & CCCC_STACK_INSTR)
                emit_markw(vm, node->lhs->var->offset);
            if (vm->flags & CCCC_UNINIT_DETECTION)
                emit_marki(vm, node->lhs->var->offset);
        } else if (node->lhs->kind == ND_VAR && node->lhs->var->is_local &&
            !node->lhs->var->is_param &&
            node->lhs->var->ty && node->lhs->var->ty->kind != TY_ARRAY &&
            node->lhs->var->ty->kind != TY_STRUCT &&
            node->lhs->var->ty->kind != TY_UNION) {
            if (vm->flags & CCCC_STACK_INSTR)
                emit_markw(vm, node->lhs->var->offset);
            if (vm->flags & CCCC_UNINIT_DETECTION)
                emit_marki(vm, node->lhs->var->offset);
        }

        if (r_addr >= 0)
            free_temp_reg(r_addr);

        // Assignment result is the value
        // If bitfield, r_val holds the RHS value, which is correct
        if (dest_reg != REG_ZERO && dest_reg != r_val) {
            emit_mov3(vm, dest_reg, r_val);
        }

        if (need_free) {
            free_temp_reg(r_val);
        }
        if (rhs_promoted_addr)
            promotion_alias_add(vm, node->lhs->var, node->rhs->lhs->var);
        return;
    }

    case ND_COND: {
        // Ternary: cond ? then : else.
        // Reuse dest_reg for the condition scratch to avoid O(depth) register
        // accumulation on deeply nested && / || conditions (#587 gap).
        // Guard: dest_reg may be REG_ZERO (discarded expression statement) — the
        // zero register silently discards writes, so the condition is always read
        // back as 0, making the branch always take the else arm. Allocate a real
        // temp when dest_reg == REG_ZERO and free it before generating the branches.
        int r_cond = (dest_reg == REG_ZERO) ? alloc_temp_reg() : dest_reg;
        mark_temp_reg_used(r_cond);
        gen_cond_expr(vm, node->cond, r_cond);
        Pc jz_else = emit_jz3(vm, r_cond);
        if (r_cond != dest_reg) free_temp_reg(r_cond);

        gen_expr(vm, node->then, dest_reg);
        emit(vm, JMP);
        Pc jmp_end = emit_word_ptr(vm);

        vm->text_seg[jz_else] = vm->text_ptr + 1;
        gen_expr(vm, node->els, dest_reg);
        vm->text_seg[jmp_end] = vm->text_ptr + 1;
        return;
    }

    case ND_COMMA:
        gen_expr(vm, node->lhs, REG_ZERO); // Discard result
        gen_expr(vm, node->rhs, dest_reg);
        return;

    case ND_MEMBER: {
        // For float/double members, dest_reg is a float register, and
        // FREG_A0-A7 have the same raw numbers as REG_A0-A7 (same hazard
        // ND_VAR's flonum branch above avoids). Use a temp register for the
        // address so it never aliases the value register the load targets.
        // Bitfields are always integer-typed, so temp_addr is only set on
        // the standard-member path below.
        bool local_frame = addr_is_local_frame(vm, node);
        bool temp_addr = !node->member->is_bitfield && is_flonum(node->ty);
        int r_addr = temp_addr ? alloc_temp_reg() : dest_reg;
        gen_addr(vm, node, r_addr);

        bool is_union_member = is_union_member_access(node);
        bool saved_union_flag = vm->compiler.in_union_member_access;
        vm->compiler.in_union_member_access = is_union_member;

        if (node->member->is_bitfield) {
            Member *mem = node->member;
            // Load container value
            emit_load_ex(vm, mem->ty, dest_reg, r_addr, !local_frame);

            if (mem->ty->is_unsigned) {
                // Unsigned: (val >> bit_offset) & mask
                if (mem->bit_offset > 0) {
                    int r_shift = alloc_temp_reg();
                    emit_li3(vm, r_shift, mem->bit_offset);
                    emit_rrr(vm, SHR3, dest_reg, dest_reg,
                             r_shift); // Logical shift right
                    free_temp_reg(r_shift);
                }
                long long mask = (1ULL << mem->bit_width) - 1;
                int r_mask = alloc_temp_reg();
                emit_li3(vm, r_mask, mask);
                emit_rrr(vm, AND3, dest_reg, dest_reg, r_mask);
                free_temp_reg(r_mask);
            } else {
                // Signed: (val << (64 - width - offset)) >> (64 - width)
                int r_shift = alloc_temp_reg();
                int left_shift = 64 - mem->bit_width - mem->bit_offset;
                int right_shift = 64 - mem->bit_width;

                emit_li3(vm, r_shift, left_shift);
                emit_rrr(vm, SHL3, dest_reg, dest_reg, r_shift);

                emit_li3(vm, r_shift, right_shift);
                emit_rrr(vm, SHR3, dest_reg, dest_reg,
                         r_shift); // Arithmetic shift preserves sign
                free_temp_reg(r_shift);
            }
        } else {
            // Standard member
            if (node->ty->kind != TY_ARRAY && node->ty->kind != TY_STRUCT &&
                node->ty->kind != TY_UNION) {
                emit_load_ex(vm, node->ty, dest_reg, r_addr, !local_frame);
            }
        }
        vm->compiler.in_union_member_access = saved_union_flag;
        if (temp_addr)
            free_temp_reg(r_addr);
        return;
    }

    case ND_CAST:
        if (is_complex(node->lhs->ty)) {
            int imag_reg = (dest_reg == FREG_A7) ? FREG_A6 : FREG_A7;
            gen_complex_expr(vm, node->lhs, dest_reg, imag_reg);
            if (!is_flonum(node->ty))
                emit_rr(vm,
                        fop_for_type(node->lhs->ty->base,
                                     is_u64_int(node->ty) ? F2U3 : F2I3),
                        dest_reg, dest_reg);
            else if (node->ty->kind == TY_FLOAT)
                emit_fround_f32(vm, dest_reg, dest_reg);
            return;
        }
        // Wide _BitInt conversion handling
        if (is_wide_bitint(node->lhs->ty) || is_wide_bitint(node->ty)) {
            Type *src = node->lhs->ty;
            Type *dst = node->ty;
            if (is_wide_bitint(dst) && !is_wide_bitint(src)) {
                // Narrow int/float → wide _BitInt
                long long dst_offset = alloc_wide_bitint_temp(vm, dst->size / 8);
                emit_lea3(vm, REG_A0, dst_offset);
                if (is_flonum(src)) {
                    // float/double → wide: pass double bits as raw int64
                    // Float regs always store as double internally, so FR2R gives
                    // us the IEEE-754 double representation regardless of f32/f64.
                    int r_tmp = alloc_temp_reg();
                    gen_expr(vm, node->lhs, r_tmp); // puts float/double in float reg
                    emit_rr(vm, FR2R, REG_A1, r_tmp); // double bits → int reg
                    free_temp_reg(r_tmp);
                    emit_lea3(vm, REG_A0, dst_offset);
                    emit_li3(vm, REG_A2, dst->size / 8);  // words
                    emit_li3(vm, REG_A3, dst->bit_width);
                    emit_li3(vm, REG_A4, !dst->is_unsigned);
                    emit_wide_helper(vm, "__cccc_bitint_from_double", 5);
                } else {
                    gen_expr(vm, node->lhs, REG_A1); // narrow int value
                    emit_li3(vm, REG_A2, dst->size / 8);  // words
                    emit_li3(vm, REG_A3, dst->bit_width);
                    const char *fn = (!dst->is_unsigned || src->is_unsigned)
                                     ? "__cccc_bitint_from_i64"
                                     : "__cccc_bitint_from_u64";
                    emit_wide_helper(vm, fn, 4);
                }
                emit_lea3(vm, dest_reg, dst_offset);
                return;
            } else if (!is_wide_bitint(dst) && is_wide_bitint(src)) {
                // Wide _BitInt → narrow int/float
                int r_src = alloc_temp_reg();
                gen_expr(vm, node->lhs, r_src); // address of wide value
                emit_mov3(vm, REG_A0, r_src);
                free_temp_reg(r_src);
                emit_li3(vm, REG_A1, src->size / 8);  // words
                emit_li3(vm, REG_A2, src->bit_width);
                emit_li3(vm, REG_A3, !src->is_unsigned);
                if (is_flonum(dst)) {
                    emit_wide_helper(vm, "__cccc_bitint_to_double", 4);
                    // result is raw double bits in REG_A0; reinterpret as float reg
                    emit_rr(vm, R2FR, dest_reg, REG_A0);
                    if (dst->kind == TY_FLOAT)
                        emit_fround_f32(vm, dest_reg, dest_reg);
                } else if (dst->kind == TY_BOOL) {
                    // (_Bool) must reflect the whole value, not just the low
                    // word: a wide value with only high bits set is still true.
                    emit_li3(vm, REG_A1, src->size / 8); // words (A0 = address)
                    emit_wide_helper(vm, "__cccc_bitint_nonzero", 2);
                    emit_mov3(vm, dest_reg, REG_A0);
                } else {
                    emit_wide_helper(vm, "__cccc_bitint_to_i64", 4);
                    emit_mov3(vm, dest_reg, REG_A0);
                    // Apply target int truncation
                    if (dst->kind == TY_BOOL)
                        emit_rrr(vm, SNE3, dest_reg, dest_reg, REG_ZERO);
                    else if (dst->kind == TY_CHAR)
                        emit_rr(vm, dst->is_unsigned ? ZX1 : SX1, dest_reg, dest_reg);
                    else if (dst->kind == TY_SHORT)
                        emit_rr(vm, dst->is_unsigned ? ZX2 : SX2, dest_reg, dest_reg);
                    else if (dst->kind == TY_INT)
                        emit_rr(vm, dst->is_unsigned ? ZX4 : SX4, dest_reg, dest_reg);
                    else if (dst->kind == TY_BITINT)
                        emit_bitint_trunc(vm, dst, dest_reg);
                }
                return;
            } else if (is_wide_bitint(src) && is_wide_bitint(dst)) {
                // Wide → wide: sign/zero-extend (per src signedness) when
                // growing, or truncate when shrinking.
                int words_src = src->size / 8;
                int words_dst = dst->size / 8;
                long long dst_offset = alloc_wide_bitint_temp(vm, words_dst);
                int r_src = alloc_temp_reg();
                gen_expr(vm, node->lhs, r_src);
                emit_lea3(vm, REG_A0, dst_offset);
                emit_mov3(vm, REG_A1, r_src);
                free_temp_reg(r_src);
                emit_li3(vm, REG_A2, words_src);
                emit_li3(vm, REG_A3, src->bit_width);
                emit_li3(vm, REG_A4, words_dst);
                emit_li3(vm, REG_A5, dst->bit_width);
                emit_li3(vm, REG_A6, !src->is_unsigned);
                emit_wide_helper(vm, "__cccc_bitint_extend", 7);
                emit_lea3(vm, dest_reg, dst_offset);
                return;
            }
        }
        // _Decimal32/64/128 conversion handling (#402): address-based, like
        // wide _BitInt above, but dispatches to DFROMI/DTOI/DFROMBITS/
        // DTOBITS/DCVT instead of a bitint runtime helper.
        if (is_decimal(node->lhs->ty) || is_decimal(node->ty)) {
            Type *src = node->lhs->ty;
            Type *dst = node->ty;
            if (is_decimal(dst) && !is_decimal(src)) {
                long long dst_offset = node->ret_buffer
                    ? (long long)node->ret_buffer->offset
                    : alloc_decimal_temp(vm, dst->size);
                emit_lea3(vm, REG_A0, dst_offset);
                if (is_flonum(src)) {
                    // binary float/double -> decimal: bit-reinterpret via
                    // FR2R/FR2R_F32 (float reg's raw bits -> int reg), then
                    // DFROMBITS. FR2R_F32 for an f32 source packs just the
                    // 32-bit float pattern (not a misread of the full 64-bit
                    // double pattern FReg would otherwise hold it as).
                    int r_tmp = alloc_temp_reg();
                    gen_expr(vm, node->lhs, r_tmp); // src in a float reg
                    emit_rr(vm, src->kind == TY_FLOAT ? FR2R_F32 : FR2R,
                            REG_A1, r_tmp);
                    free_temp_reg(r_tmp);
                    emit_lea3(vm, REG_A0, dst_offset);
                    emit_li3(vm, REG_A2, dec_width_code(dst));
                    emit_li3(vm, REG_A3, src->kind == TY_FLOAT ? 1 : 0);
                    if (vm->flags & CCCC_POINTER_CHECKS)
                        emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_wide_op(vm, DFROMBITS);
                } else {
                    // int -> decimal
                    gen_expr(vm, node->lhs, REG_A1);
                    emit_lea3(vm, REG_A0, dst_offset); // REG_A1 may share REG_A0's slot; reload
                    emit_li3(vm, REG_A2, dec_width_code(dst));
                    emit_li3(vm, REG_A3, src->is_unsigned ? 1 : 0);
                    if (vm->flags & CCCC_POINTER_CHECKS)
                        emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_wide_op(vm, DFROMI);
                }
                emit_lea3(vm, dest_reg, dst_offset);
                return;
            } else if (!is_decimal(dst) && is_decimal(src)) {
                // dest_reg holds the source address directly (#838): no
                // fresh temp is held across anything here, matching the
                // O(1)-per-level discipline the binop branches use, so a
                // decimal cast nested in a deep operand chain costs no
                // extra register. dest_reg is written last in every arm
                // below (result overwrites the address it started with).
                // Exception: dest_reg may be REG_ZERO (discarded-value
                // statement, e.g. `(void)(_Decimal64)d;`) -- REG_ZERO always
                // reads back 0, so staging the source address through it
                // would hand DTOI/DTOBITS/DCMP a null pointer. Use a fresh
                // temp in that case instead.
                int r_src = (dest_reg == REG_ZERO) ? alloc_temp_reg() : dest_reg;
                gen_expr(vm, node->lhs, r_src); // address of decimal value
                // node->lhs may be a call (e.g. a decimal-returning FFI
                // function) whose CALLF resets the temp-reg bitmap. The
                // TY_BOOL arm below allocates r_zero before r_src is fully
                // consumed -- re-mark r_src used so that alloc can't hand
                // out the same register out from under it.
                mark_temp_reg_used(r_src);
                if (is_flonum(dst)) {
                    emit_mov3(vm, REG_A0, r_src);
                    emit_li3(vm, REG_A1, dec_width_code(src));
                    emit_li3(vm, REG_A2, dst->kind == TY_FLOAT ? 1 : 0);
                    if (vm->flags & CCCC_POINTER_CHECKS)
                        emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_wide_op(vm, DTOBITS); // A0 = raw f32/f64 bits
                    // R2FR reinterprets the full 64 bits of A0 as a double
                    // bit pattern -- wrong for an f32 destination, where
                    // cccc_dec_to_bin packed only a 32-bit float pattern
                    // into the low half of A0 (upper bits zero). R2FR_F32
                    // is the f32-specific counterpart that does this
                    // correctly (same convention I2F3_F32/FR2R_F32 use
                    // elsewhere for float-reg bit transfers).
                    emit_rr(vm, dst->kind == TY_FLOAT ? R2FR_F32 : R2FR,
                            dest_reg, REG_A0);
                } else if (dst->kind == TY_BOOL) {
                    // (_Bool) is a truthiness test (nonzero -> 1): DCMP
                    // against a zero literal of src's width, same test as
                    // gen_cond_expr's decimal branch, but reusing r_src
                    // (already evaluated above) instead of re-evaluating
                    // node->lhs, which could duplicate side effects.
                    int w = dec_width_code(src);
                    unsigned char zero_bits[16] = {0};
                    long long zoff = vm->data_ptr - vm->data_seg;
                    zoff = (zoff + (src->align - 1)) & ~(long long)(src->align - 1);
                    vm->data_ptr = vm->data_seg + zoff;
                    check_data_capacity(vm, zoff + src->size);
                    if (!cccc_dec_encode_literal("0", w, zero_bits))
                        error_tok(vm, node->tok,
                                  "_Decimal requires a build with CCCC_HAS_DECIMAL=1");
                    memcpy(vm->data_ptr, zero_bits, (size_t)src->size);
                    vm->data_ptr += src->size;

                    int r_zero = alloc_temp_reg();
                    emit_lda3(vm, r_zero, zoff);
                    emit_mov3(vm, REG_A0, r_src);
                    emit_mov3(vm, REG_A1, r_zero);
                    emit_li3(vm, REG_A2, w);
                    if (vm->flags & CCCC_POINTER_CHECKS)
                        emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_wide_op(vm, DCMP);
                    int tmp = alloc_temp_reg();
                    emit_li3(vm, tmp, 0);
                    emit_rrr(vm, SNE3, dest_reg, REG_A0, tmp);
                    free_temp_reg(tmp);
                    free_temp_reg(r_zero);
                } else {
                    // decimal -> int (truncating, C semantics)
                    emit_mov3(vm, REG_A0, r_src);
                    emit_li3(vm, REG_A1, dec_width_code(src));
                    emit_li3(vm, REG_A2, dst->is_unsigned ? 1 : 0);
                    if (vm->flags & CCCC_POINTER_CHECKS)
                        emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_wide_op(vm, DTOI);
                    emit_mov3(vm, dest_reg, REG_A0);
                    if (dst->kind == TY_CHAR)
                        emit_rr(vm, dst->is_unsigned ? ZX1 : SX1, dest_reg, dest_reg);
                    else if (dst->kind == TY_SHORT)
                        emit_rr(vm, dst->is_unsigned ? ZX2 : SX2, dest_reg, dest_reg);
                    else if (dst->kind == TY_INT)
                        emit_rr(vm, dst->is_unsigned ? ZX4 : SX4, dest_reg, dest_reg);
                    else if (dst->kind == TY_BITINT && !is_wide_bitint(dst))
                        emit_bitint_trunc(vm, dst, dest_reg);
                }
                if (r_src != dest_reg)
                    free_temp_reg(r_src);
                return;
            } else if (is_decimal(src) && is_decimal(dst)) {
                // Same dest_reg discipline (and REG_ZERO exception) as the
                // decimal->non-decimal arm above -- with one more wrinkle:
                // a fixed decimal FFI/native argument in register position
                // evaluates its expression with dest_reg == REG_A0+i
                // directly (see the "generic branch" callers of gen_expr
                // with REG_A0+int_arg_idx for a decimal arg). So r_src can
                // legitimately alias REG_A0 here. emit_mov3(REG_A1, r_src)
                // MUST run before emit_lea3(REG_A0, dst_offset) clobbers it
                // -- reading r_src first makes the sequence correct
                // regardless of which register it aliases.
                int r_src = (dest_reg == REG_ZERO) ? alloc_temp_reg() : dest_reg;
                gen_expr(vm, node->lhs, r_src);
                // node->lhs may be a call; re-mark r_src used in case its
                // CALLF reset the temp-reg bitmap (matches the cast arm
                // above -- alloc_decimal_temp itself never allocates a
                // temp reg, but this keeps the two arms' discipline
                // identical and independent of that implementation detail).
                mark_temp_reg_used(r_src);
                long long dst_offset = node->ret_buffer
                    ? (long long)node->ret_buffer->offset
                    : alloc_decimal_temp(vm, dst->size);
                emit_mov3(vm, REG_A1, r_src);
                emit_lea3(vm, REG_A0, dst_offset);
                emit_li3(vm, REG_A2, dec_width_code(dst));
                emit_li3(vm, REG_A3, dec_width_code(src));
                if (vm->flags & CCCC_POINTER_CHECKS) {
                    emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_rr(vm, CHKP3, REG_A1, 0);
                }
                emit_wide_op(vm, DCVT);
                emit_lea3(vm, dest_reg, dst_offset);
                if (r_src != dest_reg)
                    free_temp_reg(r_src);
                return;
            }
        }
        gen_expr(vm, node->lhs, dest_reg);
        // Add type conversion if needed
        if (is_flonum(node->ty) && !is_flonum(node->lhs->ty)) {
            // int -> float
            emit_rr(vm,
                    fop_for_type(node->ty,
                                 is_u64_int(node->lhs->ty) ? U2F3 : I2F3),
                    dest_reg, dest_reg);
        } else if (!is_flonum(node->ty) && is_flonum(node->lhs->ty)) {
            // float -> int
            emit_rr(vm,
                    fop_for_type(node->lhs->ty,
                                 is_u64_int(node->ty) ? F2U3 : F2I3),
                    dest_reg, dest_reg);
        } else if (node->ty->kind == TY_FLOAT &&
                   node->lhs->ty->kind != TY_FLOAT) {
            emit_fround_f32(vm, dest_reg, dest_reg);
        } else if (!is_flonum(node->ty) && !is_flonum(node->lhs->ty)) {
            // Integer conversion - handle truncation/extension
            if (node->ty->kind == TY_CHAR) {
                emit_rr(vm, node->ty->is_unsigned ? ZX1 : SX1, dest_reg,
                        dest_reg);
            } else if (node->ty->kind == TY_SHORT) {
                emit_rr(vm, node->ty->is_unsigned ? ZX2 : SX2, dest_reg,
                        dest_reg);
            } else if (node->ty->kind == TY_INT) {
                emit_rr(vm, node->ty->is_unsigned ? ZX4 : SX4, dest_reg,
                        dest_reg);
            } else if (node->ty->kind == TY_BOOL) {
                emit_rrr(vm, SNE3, dest_reg, dest_reg,
                         REG_ZERO); // dest_reg = (dest_reg != 0)
            } else if (node->ty->kind == TY_BITINT) {
                // Container sign/zero-extend then bit-precise truncate
                if (node->ty->size == 1)
                    emit_rr(vm, node->ty->is_unsigned ? ZX1 : SX1, dest_reg, dest_reg);
                else if (node->ty->size == 2)
                    emit_rr(vm, node->ty->is_unsigned ? ZX2 : SX2, dest_reg, dest_reg);
                else if (node->ty->size == 4)
                    emit_rr(vm, node->ty->is_unsigned ? ZX4 : SX4, dest_reg, dest_reg);
                emit_bitint_trunc(vm, node->ty, dest_reg);
            }
        }
        return;

    case ND_FUNCALL: {
        // Invalidate the restrict cache up front for every call, not just the
        // general CALL/CALLN/CALLF path below. Several intrinsics (malloc/
        // free/calloc/realloc/... under CCCC_VM_HEAP, setjmp/longjmp/signal/
        // raise/dlopen/dlsym/dlclose/dlerror) lower to a dedicated opcode via
        // an early `return` that never reaches the general path's invalidate,
        // leaving stale cache entries after e.g. free() (#754). The general
        // path below still invalidates again *after* the call: this call's
        // own argument expressions run after this point and may fill a cache
        // entry (e.g. f(*p) as the first access to *p), which must not
        // survive the call it was evaluated for. Over-invalidating only costs
        // cache throughput, never correctness.
        //
        // Residual gap, not fixed here: an intrinsic's *own* argument
        // expression can itself fill a cache entry (e.g. realloc(p, *p) --
        // *p is read as the size argument), and that intrinsic's early
        // `return` below skips any invalidate after it runs, same as before
        // this fix. Unlike the general-path case above, there is no
        // "after this call" invalidate to add for those branches without
        // touching every intrinsic's early return individually. Any
        // subsequent call (of any kind) still invalidates via its own
        // top-of-case entry into this invalidate, so the gap is narrow:
        // an intrinsic argument fill immediately followed by a stale access
        // with no further call in between. restrict_cache_handle_deref's
        // hit-site checks (#750) still catch anything CHKP3/CHKT3 would
        // catch on a real load; only a silent value divergence under no
        // safety flags at all would slip through. Tracked as a follow-up.
        restrict_cache_invalidate_all(vm);

        // Capture and clear the tail-call flag immediately so that argument
        // sub-calls (e.g. return f(g(x))) and inlined bodies never see it.
        // The captured value is used below when deciding CALL vs CALLT.
        bool is_tail = vm->compiler.emitting_tail_call;
        vm->compiler.emitting_tail_call = false;

        if (is_complex(node->ty))
            error_tok(vm, node->tok,
                      "complex function return ABI is not supported");
        for (Node *arg = node->args; arg; arg = arg->next)
            if (is_complex(arg->ty))
                error_tok(vm, arg->tok,
                          "complex function argument ABI is not supported");

        // Check if this is a builtin alloca call (used for VLAs, and for an
        // explicit __builtin_alloca)
        if (node->lhs->kind == ND_VAR &&
            node->lhs->var->is_builtin_alloca) {
            // Special handling for alloca: uses the ALCA/ALCV opcode pair
            // (#979/#981 -- alloca and VLA backing storage is automatic
            // storage, not a user allocation, so it must never show up in
            // a leak report; both have MALC's exact register shape, just
            // tagged with a different AllocKind on the AllocHeader). Which
            // opcode depends on which one this call actually is:
            // node->is_vla_alloca_call is set only by parse.c's
            // new_alloca(), reached only through a VLA declaration's
            // lowering -- an explicit __builtin_alloca call never sets it.
            // The two are NOT interchangeable for #981's reclamation: a
            // VLA's storage dies at the end of its declaring *block*
            // (ALCV/ALLOC_KIND_FRAME, swept by HREL below), a bare
            // alloca's storage lives until the *function* returns (ALCA/
            // ALLOC_KIND_ALLOCA, swept only at LEV3) -- see ALCA's/ALCV's
            // own comments in src/cccc.h.
            if (!node->args) {
                error_tok(vm, node->tok, "alloca requires a size argument");
            }
            // Evaluate size argument into REG_A0 (ALCA/ALCV read from REG_A0)
            reset_temp_regs();
            gen_expr(vm, node->args, REG_A0);
            emit(vm, node->is_vla_alloca_call ? ALCV : ALCA); // Size in REG_A0, returns pointer in REG_A0
            if (dest_reg != REG_A0) {
                emit_mov3(vm, dest_reg, REG_A0);
            }
            return;
        }

        // Check if this is VM-managed signal() builtin
        if (is_extern_func_name(node->lhs, "signal") ||
            (node->lhs->kind == ND_VAR &&
             node->lhs->var == vm->compiler.builtin_signal)) {
            if (!node->args || !node->args->next) {
                error_tok(vm, node->tok, "signal requires sig and handler arguments");
            }
            reset_temp_regs();
            gen_expr(vm, node->args, REG_A0);             // sig
            gen_expr(vm, node->args->next, REG_A1);       // handler
            emit(vm, VSIGNAL);
            if (dest_reg != REG_A0)
                emit_mov3(vm, dest_reg, REG_A0);
            return;
        }

        // Check if this is VM-managed raise() builtin
        if (is_extern_func_name(node->lhs, "raise") ||
            (node->lhs->kind == ND_VAR &&
             node->lhs->var == vm->compiler.builtin_raise)) {
            if (!node->args) {
                error_tok(vm, node->tok, "raise requires a sig argument");
            }
            reset_temp_regs();
            gen_expr(vm, node->args, REG_A0); // sig
            emit(vm, VRAISE);
            /* VRAISE may jump into a VM handler; when the handler returns it
               lands here. Normalise REG_A0 = 0 so raise() always returns 0. */
            emit_li3(vm, REG_A0, 0);
            if (dest_reg != REG_A0)
                emit_mov3(vm, dest_reg, REG_A0);
            return;
        }

        // Check if this is setjmp builtin (or its POSIX _setjmp alias)
        if (node->lhs->kind == ND_VAR &&
            (node->lhs->var == vm->compiler.builtin_setjmp ||
             node->lhs->var == vm->compiler.builtin__setjmp)) {
            if (!node->args) {
                error_tok(vm, node->tok, "setjmp requires a jmp_buf argument");
            }
            // Evaluate jmp_buf address into REG_A0 (SETJMP reads from REG_A0)
            reset_temp_regs();
            gen_expr(vm, node->args, REG_A0);
            emit(vm, SETJMP); // Save context, returns 0 in REG_A0
            if (dest_reg != REG_A0) {
                emit_mov3(vm, dest_reg, REG_A0);
            }
            return;
        }

        // Check if this is longjmp builtin (or its POSIX _longjmp alias)
        if (node->lhs->kind == ND_VAR &&
            (node->lhs->var == vm->compiler.builtin_longjmp ||
             node->lhs->var == vm->compiler.builtin__longjmp)) {
            if (!node->args || !node->args->next) {
                error_tok(vm, node->tok,
                          "longjmp requires jmp_buf and int arguments");
            }
            // LONGJMP: env in REG_A0, val in REG_A1
            reset_temp_regs();
            gen_expr(vm, node->args, REG_A0);       // env (jmp_buf address)
            gen_expr(vm, node->args->next, REG_A1); // val
            emit(vm, LONGJMP); // Restore context and jump (does not return)
            return;
        }

        if (is_extern_func_name(node->lhs, "dlopen")) {
            reset_temp_regs();
            if (!node->args || !node->args->next)
                error_tok(vm, node->tok, "dlopen requires path and mode arguments");
            gen_expr(vm, node->args, REG_A0);
            gen_expr(vm, node->args->next, REG_A1);
            emit(vm, DLOPEN);
            if (dest_reg != REG_A0)
                emit_mov3(vm, dest_reg, REG_A0);
            return;
        }

        if (is_extern_func_name(node->lhs, "dlsym")) {
            reset_temp_regs();
            if (!node->args || !node->args->next)
                error_tok(vm, node->tok, "dlsym requires handle and symbol arguments");
            gen_expr(vm, node->args, REG_A0);
            gen_expr(vm, node->args->next, REG_A1);
            emit(vm, DLSYM);
            if (dest_reg != REG_A0)
                emit_mov3(vm, dest_reg, REG_A0);
            return;
        }

        if (is_extern_func_name(node->lhs, "dlclose")) {
            reset_temp_regs();
            if (!node->args)
                error_tok(vm, node->tok, "dlclose requires a handle argument");
            gen_expr(vm, node->args, REG_A0);
            emit(vm, DLCLOSE);
            if (dest_reg != REG_A0)
                emit_mov3(vm, dest_reg, REG_A0);
            return;
        }

        if (is_extern_func_name(node->lhs, "dlerror")) {
            reset_temp_regs();
            emit(vm, DLERROR);
            if (dest_reg != REG_A0)
                emit_mov3(vm, dest_reg, REG_A0);
            return;
        }

        // When CCCC_VM_HEAP is set, route malloc/free/calloc/realloc through VM
        // heap opcodes instead of system allocators via FFI.
        if (vm->flags & CCCC_VM_HEAP) {
            if (is_extern_func_name(node->lhs, "malloc")) {
                if (!node->args)
                    error_tok(vm, node->tok, "malloc requires a size argument");
                reset_temp_regs();
                gen_expr(vm, node->args, REG_A0);
                emit(vm, MALC);
                if (dest_reg != REG_A0)
                    emit_mov3(vm, dest_reg, REG_A0);
                return;
            }
            if (is_extern_func_name(node->lhs, "free")) {
                if (!node->args)
                    error_tok(vm, node->tok, "free requires a pointer argument");
                reset_temp_regs();
                gen_expr(vm, node->args, REG_A0);
                emit(vm, MFRE);
                return;
            }
            // free_sized/free_aligned_sized (C23) route through the same MFRE
            // opcode as free: MFRE derives the real size from the AllocHeader
            // and already falls back to the host free() for non-VM-heap
            // pointers (ops.c op_MFRE_fn), so the size/alignment arguments
            // are only evaluated for side effects and otherwise discarded.
            // Without this, a VM-heap malloc() paired with free_sized() would
            // hand a VM-heap pointer straight to the host's free_sized() via
            // FFI and abort (#665 fallout: VM heap is on by default now).
            if (is_extern_func_name(node->lhs, "free_sized")) {
                if (!node->args || !node->args->next)
                    error_tok(vm, node->tok, "free_sized requires ptr and size arguments");
                reset_temp_regs();
                gen_expr(vm, node->args, REG_A0);
                gen_expr(vm, node->args->next, REG_A1);
                emit(vm, MFRE);
                return;
            }
            if (is_extern_func_name(node->lhs, "free_aligned_sized")) {
                if (!node->args || !node->args->next || !node->args->next->next)
                    error_tok(vm, node->tok,
                              "free_aligned_sized requires ptr, alignment, and size arguments");
                reset_temp_regs();
                gen_expr(vm, node->args, REG_A0);
                gen_expr(vm, node->args->next, REG_A1);
                gen_expr(vm, node->args->next->next, REG_A2);
                emit(vm, MFRE);
                return;
            }
            if (is_extern_func_name(node->lhs, "calloc")) {
                if (!node->args || !node->args->next)
                    error_tok(vm, node->tok, "calloc requires nmemb and size arguments");
                reset_temp_regs();
                gen_expr(vm, node->args, REG_A0);
                gen_expr(vm, node->args->next, REG_A1);
                emit(vm, CALC);
                if (dest_reg != REG_A0)
                    emit_mov3(vm, dest_reg, REG_A0);
                return;
            }
            if (is_extern_func_name(node->lhs, "realloc")) {
                if (!node->args || !node->args->next)
                    error_tok(vm, node->tok, "realloc requires ptr and size arguments");
                reset_temp_regs();
                gen_expr(vm, node->args, REG_A0);
                gen_expr(vm, node->args->next, REG_A1);
                emit(vm, REALC);
                if (dest_reg != REG_A0)
                    emit_mov3(vm, dest_reg, REG_A0);
                return;
            }
            // reallocarray (#699): full parity with the rest of the malloc
            // family via the VM heap's overflow-checked REALCA opcode,
            // instead of falling through to a generic FFI call (which would
            // both skip heap-safety tracking and be unavailable on hosts
            // without a native reallocarray, e.g. this macOS SDK).
            if (is_extern_func_name(node->lhs, "reallocarray")) {
                if (!node->args || !node->args->next || !node->args->next->next)
                    error_tok(vm, node->tok, "reallocarray requires ptr, nmemb, and size arguments");
                reset_temp_regs();
                gen_expr(vm, node->args, REG_A0);
                gen_expr(vm, node->args->next, REG_A1);
                gen_expr(vm, node->args->next->next, REG_A2);
                emit(vm, REALCA);
                if (dest_reg != REG_A0)
                    emit_mov3(vm, dest_reg, REG_A0);
                return;
            }
            // aligned_alloc/posix_memalign (C11/C23) route through the VM
            // heap's alignment-aware bump allocator (MALCA/PMEMA) so their
            // allocations get an AllocHeader and full heap safety coverage
            // (canaries, bounds/UAF/type checks, leak detection, tagging),
            // mirroring malloc/calloc/realloc/free above. Before this, they
            // fell through to the host allocator via FFI: not a crash (MFRE
            // already falls back to host free() for non-VM-heap pointers),
            // but heap safety silently didn't apply to them (#668).
            if (is_extern_func_name(node->lhs, "aligned_alloc")) {
                if (!node->args || !node->args->next)
                    error_tok(vm, node->tok, "aligned_alloc requires alignment and size arguments");
                reset_temp_regs();
                gen_expr(vm, node->args->next, REG_A0); // size
                gen_expr(vm, node->args, REG_A1);       // alignment
                emit(vm, MALCA);
                if (dest_reg != REG_A0)
                    emit_mov3(vm, dest_reg, REG_A0);
                return;
            }
            if (is_extern_func_name(node->lhs, "posix_memalign")) {
                if (!node->args || !node->args->next || !node->args->next->next)
                    error_tok(vm, node->tok, "posix_memalign requires memptr, alignment, and size arguments");
                reset_temp_regs();
                gen_expr(vm, node->args, REG_A0);             // memptr
                gen_expr(vm, node->args->next, REG_A1);       // alignment
                gen_expr(vm, node->args->next->next, REG_A2); // size
                emit(vm, PMEMA);
                if (dest_reg != REG_A0)
                    emit_mov3(vm, dest_reg, REG_A0);
                return;
            }
        }

        // Check for FFI call - foreign functions use register-based calling
        // convention with operand-based metadata (ffi_idx, nargs,
        // double_arg_mask)
        int ffi_idx = -1;
        if (node->lhs->kind == ND_VAR && node->lhs->var->is_function) {
            ffi_idx = ffi_index_for_callee(vm, node->lhs->var);
        }

        if (ffi_idx >= 0) {
            // FFI call: args are stored as source-order 64-bit slots.
            // Slots 0-7 use REG_A0-A7; slots 8+ are pushed on the VM stack.
            reset_temp_regs();

            // Vector-by-value through the native FFI marshalling path isn't
            // wired up (#714 only covers the internal CCCC call ABI) --
            // reject with a clear diagnostic rather than silently
            // mis-marshalling a vregs[] value as a 64-bit slot.
            if (is_vector(node->ty))
                error_tok(vm, node->tok,
                          "vector return values through FFI calls are not "
                          "supported");
            // #402/#830: same rationale as the vector rejection above --
            // libffi has no decimal ffi_type, so a _Decimal return through
            // FFI would be mis-marshalled as a 64-bit int/double slot rather
            // than rejected. Remains out of scope here; see #830.
            if (is_decimal(node->ty))
                error_tok(vm, node->tok,
                          "_Decimal return values through FFI calls are not "
                          "supported");

            // #829: a decimal argument in the *variadic tail* of an FFI call
            // (our own cccc_printf/cccc_fprintf/... engine, which expects a
            // pointer for %Hf/%Df/%DDf) is passed by pointer -- see
            // gen_decimal_arg_ptr. A FIXED decimal FFI parameter has no such
            // convention to lean on (no libffi decimal ffi_type exists), so
            // it stays rejected; that by-value FFI case is #830's scope.
            bool ffi_is_variadic_call = node->func_ty && node->func_ty->is_variadic;
            int ffi_fixed_param_count = 0;
            if (ffi_is_variadic_call) {
                for (Type *p = node->func_ty->params; p; p = p->next)
                    ffi_fixed_param_count++;
            }

            // Count arguments and compute double_arg_mask/float_arg_mask
            int nargs = 0;
            uint64_t double_arg_mask = 0;
            uint64_t float_arg_mask = 0;
            for (Node *arg = node->args; arg; arg = arg->next) {
                bool ffi_arg_is_vararg = ffi_is_variadic_call &&
                                         nargs >= ffi_fixed_param_count;
                if (is_flonum(arg->ty)) {
                    if (nargs >= 64)
                        error_tok(vm, arg->tok,
                                  "too many floating-point FFI arguments");
                    if (arg->ty->kind == TY_FLOAT)
                        float_arg_mask |= (1ULL << nargs);
                    else
                        double_arg_mask |= (1ULL << nargs);
                } else if (is_vector(arg->ty)) {
                    error_tok(vm, arg->tok,
                              "vector arguments through FFI calls are not "
                              "supported");
                } else if (is_decimal(arg->ty) && !ffi_arg_is_vararg) {
                    error_tok(vm, arg->tok,
                              "_Decimal arguments through FFI calls are not "
                              "supported");
                }
                nargs++;
            }

            // Collect args into array for indexed access
            Node **arg_array = NULL;
            if (nargs > 0) {
                arg_array = calloc(nargs, sizeof(Node *));
                if (!arg_array)
                    error("out of memory");
                int idx = 0;
                for (Node *a = node->args; a; a = a->next) {
                    arg_array[idx++] = a;
                }
            }

            int num_stack_args = (nargs > 8) ? (nargs - 8) : 0;

            // Push overflow args (8+) right-to-left so vm->sp[0] is arg 8.
            for (int i = nargs - 1; i >= 8; i--) {
                Node *arg = arg_array[i];
                if (is_zero_size_aggregate(arg->ty)) {
                    gen_zero_size_arg(vm, arg, REG_T0);
                } else if (is_flonum(arg->ty)) {
                    int fs = gen_flonum_arg_to_scratch(vm, arg);
                    emit_rr(vm, fop_for_type(arg->ty, FR2R), REG_T0, fs);
                    free_temp_reg(fs);
                } else if (is_decimal(arg->ty)) {
                    // #829: only reachable here for a variadic-tail decimal
                    // arg -- a fixed one already errored out above.
                    gen_decimal_arg_ptr(vm, arg, REG_T0);
                } else {
                    gen_expr(vm, arg, REG_T0);
                }
                emit_psh3(vm, REG_T0);
            }

            // Check which register arguments contain function calls (to handle
            // clobbering). Nested FFI calls will clobber REG_A0-A7.
            bool *arg_has_call = calloc(nargs > 0 ? nargs : 1, sizeof(bool));
            if (!arg_has_call)
                error("out of memory");
            for (int i = 0; i < nargs; i++) {
                arg_has_call[i] = contains_funcall(arg_array[i]);
            }

            // Evaluate source slots 0-7 into REG_A0-A7.
            // CRITICAL: If arg[i] contains a function call, it will clobber
            // REG_A0-A7. We must save any previous args before evaluating such
            // an arg.
            int saved_reg_count = 0;
            for (int i = 0; i < nargs && i < 8; i++) {
                Node *arg = arg_array[i];

                // Before evaluating this arg, check if it contains a function
                // call. If so, save all previously-evaluated arg registers.
                if (arg_has_call[i] && i > 0) {
                    for (int j = i - 1; j >= 0; j--) {
                        emit_psh3(vm, REG_A0 + j);
                    }
                    saved_reg_count = i;
                }

                if (is_zero_size_aggregate(arg->ty)) {
                    gen_zero_size_arg(vm, arg, REG_A0 + i);
                } else if (is_flonum(arg->ty)) {
                    int fs = gen_flonum_arg_to_scratch(vm, arg);
                    emit_rr(vm, fop_for_type(arg->ty, FR2R), REG_A0 + i, fs);
                    free_temp_reg(fs);
                } else if (is_decimal(arg->ty)) {
                    // #829: only reachable here for a variadic-tail decimal
                    // arg -- a fixed one already errored out above.
                    gen_decimal_arg_ptr(vm, arg, REG_A0 + i);
                } else {
                    gen_expr(vm, arg, REG_A0 + i);
                }

                // After evaluating this arg, if we saved previous regs, restore
                // them now.
                if (arg_has_call[i] && saved_reg_count > 0) {
                    for (int j = 0; j < saved_reg_count; j++) {
                        emit_pop3(vm, REG_A0 + j);
                    }
                    saved_reg_count = 0;
                }
            }

            free(arg_has_call);
            if (arg_array)
                free(arg_array);

            // Emit CALLF (or skip if pure/const and result unused).
            Obj *ffi_fn = (node->lhs->kind == ND_VAR) ? node->lhs->var : NULL;
            bool skip_dead_callf = vm->compiler.opt_level >= 1 &&
                                   dest_reg == REG_ZERO &&
                                   ffi_fn &&
                                   (ffi_fn->is_pure || ffi_fn->is_func_const);
            if (!skip_dead_callf) {
                emit(vm, CALLF);
                emit_word(vm, ffi_idx);
                emit_word(vm, nargs);
                emit_i64(vm, (long long)double_arg_mask);
                emit_i64(vm, (long long)float_arg_mask);
            }

            if (num_stack_args > 0) {
                emit_with_arg(vm, ADJ, num_stack_args);
            }

            // Noreturn functions never return — trap if execution continues
            if (node->func_ty->is_noreturn) {
                emit(vm, BTRAP);
                return;
            }

            // Reset temp regs after call; function may have modified *restrict_params.
            // Also invalidate the restrict cache again: the up-front invalidate at
            // the top of ND_FUNCALL runs before this call's own argument
            // expressions are evaluated, so an argument access that fills a cache
            // entry (e.g. f(*p) as the first access to *p) would otherwise survive
            // past the call it was evaluated for (#754).
            restrict_cache_invalidate_all(vm);
            reset_temp_regs();

            // Result in REG_A0/FREG_A0
            if (is_flonum(node->ty)) {
                if (dest_reg != FREG_A0) {
                    emit_fmov3(vm, dest_reg, FREG_A0);
                }
            } else {
                if (dest_reg != REG_A0) {
                    emit_mov3(vm, dest_reg, REG_A0);
                }
            }
            return;
        }

        // Static inline inlining opportunity
        if (node->lhs->kind == ND_VAR && node->lhs->var->is_function) {
            Obj *callee = node->lhs->var;
            if (callee->is_inline && callee->is_static &&
                callee->body && callee->body->kind == ND_BLOCK) {
                Node *body_stmt = callee->body->body;

                // Fast path: single-return inlining (no exit label)
                if (body_stmt && !body_stmt->next &&
                    body_stmt->kind == ND_RETURN && body_stmt->lhs &&
                    !contains_self_call(body_stmt->lhs, callee)) {
                    Type *ret_ty = body_stmt->lhs->ty;
                    if (!(ret_ty && (ret_ty->kind == TY_STRUCT ||
                                     ret_ty->kind == TY_UNION ||
                                     is_wide_bitint(ret_ty)))) {
                        reset_temp_regs();
                        Node *inlined = clone_subst(vm, body_stmt->lhs,
                                                    callee->params, node->args);
                        gen_expr(vm, inlined, dest_reg);
                        return;
                    }
                }

                // Multi-statement inlining (gated by opt_level >= 2)
                if (vm->compiler.opt_level >= 2 &&
                    vm->compiler.inline_node_limit > 0 &&
                    !contains_self_call(callee->body, callee) &&
                    !contains_unsupported_control_flow(callee->body)) {
                    Type *ret_ty = callee->ty->return_ty;
                    bool void_ret = !ret_ty || ret_ty->kind == TY_VOID;
                    if ((void_ret || !(ret_ty->kind == TY_STRUCT ||
                                       ret_ty->kind == TY_UNION ||
                                       is_wide_bitint(ret_ty))) &&
                        count_ast_nodes(callee->body) <=
                        vm->compiler.inline_node_limit) {
                        reset_temp_regs();

                        // Clone entire function body with parameter substitution
                        Node *inlined_body = clone_subst(vm, callee->body,
                                                         callee->params,
                                                         node->args);

                        // Remap callee locals into caller's frame
                        int nlocals = 0;
                        Obj **orig_locals = NULL;
                        Obj **new_locals = NULL;
                        for (Obj *v = callee->locals; v; v = v->next) {
                            if (v->is_param || v == callee->va_area ||
                                v == callee->alloca_bottom)
                                continue;
                            nlocals++;
                        }
                        if (nlocals > 0) {
                            orig_locals = calloc(nlocals, sizeof(Obj *));
                            new_locals = calloc(nlocals, sizeof(Obj *));
                            int idx = 0;
                            for (Obj *v = callee->locals; v; v = v->next) {
                                if (v->is_param || v == callee->va_area ||
                                    v == callee->alloca_bottom)
                                    continue;
                                orig_locals[idx] = v;
                                Obj *nv = arena_alloc(&vm->compiler.parser_arena,
                                                       sizeof(Obj));
                                memset(nv, 0, sizeof(Obj));
                                *nv = *v;
                                char *name = arena_format(vm, "%s_inline%d",
                                                          v->name,
                                                          vm->compiler.unique_name_counter++);
                                nv->name = name;
                                nv->display_name = name;
                                int slots = var_stack_slots(v);
                                vm->compiler.ent3_extra_stack += slots;
                                nv->offset = -(vm->compiler.ent3_base_stack +
                                               vm->compiler.ent3_extra_stack);
                                new_locals[idx++] = nv;
                            }
                            replace_locals_in_ast(inlined_body, orig_locals,
                                                  new_locals, nlocals);
                        }

                        // Generate unique exit label name
                        char *exit_name = arena_format(vm, ".Linline_exit_%d",
                                                       vm->compiler.unique_name_counter++);

                        // Set inlining context
                        vm->compiler.inline_exit_name = exit_name;
                        vm->compiler.inline_result_reg = void_ret ? REG_ZERO : dest_reg;

                        // Generate inlined body
                        gen_stmt(vm, inlined_body);

                        // Define exit label (JMP targets from inlined returns)
                        define_label(vm, exit_name);

                        // Clear inlining context
                        vm->compiler.inline_exit_name = NULL;

                        // Free temporary arrays
                        if (orig_locals) free(orig_locals);
                        if (new_locals) free(new_locals);

                        return;
                    }
                }
            }
        }

        // Internal function call: evaluate arguments
        // For variadic functions, varargs (including doubles) go to integer
        // registers so ENT3 can spill them to stack for va_arg to read

        // Check if we're calling a nested function - need to pass static link
        bool calling_nested =
            (node->lhs->kind == ND_VAR && node->lhs->var->is_function &&
             node->lhs->var->is_nested);
        int static_link_offset =
            calling_nested ? 1 : 0; // Reserve A0 for static link

        bool is_variadic_call = node->func_ty && node->func_ty->is_variadic;
        int fixed_param_count = 0;
        if (is_variadic_call) {
            for (Type *p = node->func_ty->params; p; p = p->next) {
                fixed_param_count++;
            }
        }

        // Count total args and collect into array for indexed access
        int nargs = 0;
        uint64_t call_double_arg_mask = 0;
        uint64_t call_float_arg_mask = 0;
        for (Node *a = node->args; a; a = a->next) {
            if (is_flonum(a->ty)) {
                if (nargs >= 64)
                    error_tok(vm, a->tok,
                              "too many floating-point native-call arguments");
                // Variadic tail args are promoted float->double by the parser,
                // so a TY_FLOAT arg here is always a fixed parameter.
                if (a->ty->kind == TY_FLOAT)
                    call_float_arg_mask |= (1ULL << nargs);
                else
                    call_double_arg_mask |= (1ULL << nargs);
            }
            nargs++;
        }

        Node **arg_array = NULL;
        if (nargs > 0) {
            arg_array = calloc(nargs, sizeof(Node *));
            if (!arg_array)
                error("out of memory");
            int idx = 0;
            for (Node *a = node->args; a; a = a->next) {
                arg_array[idx++] = a;
            }
        }

        // Calculate how many args go on stack (args 8+)
        int num_stack_args = (nargs > 8) ? (nargs - 8) : 0;

        // #885: for an indirect call whose callee expression itself contains
        // a call (e.g. `((int(*)(int,int))dlsym(h,"f"))(a,b)`), evaluating
        // the callee AFTER staging args into REG_A0-A7 (the old order, below)
        // lets the nested call clobber those registers -- op_ENT3_fn only
        // saves bp, not the register file, so a temp register isn't safe
        // across it either. Overflow args (8+) are pushed straight to the
        // stack, not through REG_A0-A7, so they aren't at risk -- only the
        // register-arg staging loop below is. Direct calls (node->lhs is a
        // known Obj*) are unaffected -- they never evaluate node->lhs at all.
        bool is_indirect_call = !(node->lhs->kind == ND_VAR && node->lhs->var->is_function);
        bool hoist_callee = is_indirect_call && nargs > 0 && contains_funcall(node->lhs);

        // Push overflow args (8+) right-to-left BEFORE register args
        // Stack grows downward, so push last arg first
        // After all pushes and CALL, these will be at bp[+2], bp[+3], etc.
        if (num_stack_args > 0) {
            for (int j = nargs - 1; j >= 8; j--) {
                Node *arg = arg_array[j];
                if (is_zero_size_aggregate(arg->ty)) {
                    gen_zero_size_arg(vm, arg, REG_T0);
                    emit_psh3(vm, REG_T0);
                } else if (is_flonum(arg->ty)) {
                    // Float arg: evaluate to a temp-numbered float scratch (not
                    // FREG_A0 -- see gen_flonum_arg_to_scratch, #712), move bits
                    // to int reg, push
                    int freg = gen_flonum_arg_to_scratch(vm, arg);
                    emit_rr(vm, fop_for_type(arg->ty, FR2R), REG_T0,
                            freg); // Move bits to REG_T0
                    free_temp_reg(freg);
                    emit_psh3(vm, REG_T0);
                } else if (is_vector(arg->ty)) {
                    // Vector arg: copy value to scratch slot, push its address
                    // like a struct-by-value arg (#714). A variadic tail arg
                    // (#721) works the same way -- it lands in exactly one
                    // 8-byte stack slot holding the scratch pointer, which
                    // <stdarg.h>'s va_arg dereferences via
                    // __builtin_classify_type.
                    gen_vector_arg_ptr(vm, arg, REG_T0);
                    emit_psh3(vm, REG_T0);
                } else if (is_decimal(arg->ty) &&
                          is_variadic_call && j >= fixed_param_count) {
                    // Decimal variadic tail arg (#829), stack-passed: same
                    // padded-scratch-copy rationale as gen_decimal_arg_ptr's
                    // comment below. A fixed decimal param at a stack
                    // position instead falls to the generic branch, matching
                    // #402's existing address-passthrough ABI.
                    gen_decimal_arg_ptr(vm, arg, REG_T0);
                    emit_psh3(vm, REG_T0);
                } else {
                    // Integer/pointer arg: evaluate to temp reg, push
                    gen_expr(vm, arg, REG_T0);
                    emit_psh3(vm, REG_T0);
                }
            }
        }

        // #885: spill the callee now -- after the overflow-arg pushes (which
        // op_CALLN_fn reads via vm->sp[i-8], so nothing may intervene between
        // them and CALLN except a strictly-balanced push/pop) but before the
        // register-arg staging loop the callee's own call would clobber.
        int hoisted_callee_reg = -1;
        if (hoist_callee) {
            hoisted_callee_reg = alloc_temp_reg();
            gen_expr(vm, node->lhs, hoisted_callee_reg);
            emit_psh3(vm, hoisted_callee_reg);
            free_temp_reg(hoisted_callee_reg);
        }

        // Check which arguments contain function calls (to handle register
        // clobbering)
        bool *arg_has_call = calloc(nargs, sizeof(bool));
        if (!arg_has_call && nargs > 0)
            error("out of memory");
        for (int i = 0; i < nargs; i++) {
            arg_has_call[i] = contains_funcall(arg_array[i]);
        }

        // Now evaluate first 8 args into registers
        // CRITICAL: If arg[i] contains a function call, it will clobber
        // REG_A0-A7. We must save any previous args before evaluating such an
        // arg. For nested function calls, reserve A0 for static link
        int int_arg_idx = static_link_offset; // Start at 1 if calling nested
                                              // (A0 = static_link)
        int float_arg_idx = 0;
        int saved_int_count = 0;   // How many int regs we saved
        int saved_float_count = 0; // How many float regs we saved
        bool float_arg_is_f32[8] = {0};

        for (int i = 0; i < nargs && i < 8; i++) {
            Node *arg = arg_array[i];
            bool is_vararg = is_variadic_call && (i >= fixed_param_count);

            // Before evaluating this arg, check if it contains a function call.
            // If so, save all previously-evaluated arg registers to the stack.
            if (arg_has_call[i] && (int_arg_idx > 0 || float_arg_idx > 0)) {
                // Push int regs in reverse order (so we pop in correct order
                // later)
                for (int j = int_arg_idx - 1; j >= 0; j--) {
                    emit_psh3(vm, REG_A0 + j);
                }
                saved_int_count = int_arg_idx;

                // Push float regs: convert to int bits, push
                for (int j = float_arg_idx - 1; j >= 0; j--) {
                    emit_rr(vm, float_arg_is_f32[j] ? FR2R_F32 : FR2R,
                            REG_T0, FREG_A0 + j);
                    emit_psh3(vm, REG_T0);
                }
                saved_float_count = float_arg_idx;
            }

            if (is_zero_size_aggregate(arg->ty)) {
                if (int_arg_idx < 8) {
                    gen_zero_size_arg(vm, arg, REG_A0 + int_arg_idx);
                    int_arg_idx++;
                }
            } else if (is_flonum(arg->ty)) {
                if (is_vararg) {
                    // Variadic double: put in integer register (as bit pattern)
                    // ENT3 will spill REG_A* to stack; va_arg reads from stack
                    if (int_arg_idx < 8) {
                        // Generate double value into a temp-numbered float
                        // scratch (not FREG_A0 -- see gen_flonum_arg_to_scratch,
                        // #712), then move bits to int reg
                        int freg = gen_flonum_arg_to_scratch(vm, arg);
                        // Move double bits from freg to int reg (bit-pattern,
                        // not conversion)
                        emit_rr(vm, FR2R, REG_A0 + int_arg_idx, freg);
                        free_temp_reg(freg);
                        int_arg_idx++;
                    }
                } else {
                    // Fixed param double: evaluate into a temp-numbered float
                    // scratch (#712), then move into place with FMOV3 (writes
                    // only fregs[], can never clobber a live int arg register).
                    if (float_arg_idx < 8) {
                        int freg = gen_flonum_arg_to_scratch(vm, arg);
                        emit_fmov3(vm, FREG_A0 + float_arg_idx, freg);
                        free_temp_reg(freg);
                        float_arg_is_f32[float_arg_idx] =
                            arg->ty->kind == TY_FLOAT;
                        float_arg_idx++;
                    }
                }
            } else if (is_decimal(arg->ty) && is_vararg) {
                // Decimal variadic tail arg (#829, follow-up to #402's
                // "deferred" rejection): pass by pointer to a fresh,
                // width-independent scratch copy -- see gen_decimal_arg_ptr's
                // comment. A FIXED decimal param falls through to the
                // generic branch below instead, unchanged from #402:
                // gen_expr's address-based decimal representation already
                // gives the right by-address ABI with no extra copy needed.
                if (int_arg_idx < 8) {
                    gen_decimal_arg_ptr(vm, arg, REG_A0 + int_arg_idx);
                    int_arg_idx++;
                }
            } else if (is_vector(arg->ty)) {
                // Vector arg: pass by memory like a struct-by-value arg
                // (#714) -- copy the value to a scratch slot, pass its
                // address in the integer arg register. Works identically for
                // a variadic tail arg (#721): the pointer occupies exactly
                // one int arg slot, which ENT3 spills like any other, and
                // <stdarg.h>'s va_arg dereferences it via
                // __builtin_classify_type.
                if (int_arg_idx < 8) {
                    gen_vector_arg_ptr(vm, arg, REG_A0 + int_arg_idx);
                    int_arg_idx++;
                }
            } else {
                // Integer/pointer argument - always goes in integer register
                if (int_arg_idx < 8) {
                    gen_expr(vm, arg, REG_A0 + int_arg_idx);
                    int_arg_idx++;
                }
            }

            // After evaluating this arg, if we saved previous regs, restore
            // them now. This ensures all arg regs have correct values after
            // each step.
            if (arg_has_call[i] &&
                (saved_int_count > 0 || saved_float_count > 0)) {
                // Restore float regs (were pushed last, pop first)
                for (int j = 0; j < saved_float_count; j++) {
                    emit_pop3(vm, REG_T0);
                    emit_rr(vm, float_arg_is_f32[j] ? R2FR_F32 : R2FR,
                            FREG_A0 + j, REG_T0);
                }
                // Restore int regs
                for (int j = 0; j < saved_int_count; j++) {
                    emit_pop3(vm, REG_A0 + j);
                }
                saved_int_count = 0;
                saved_float_count = 0;
            }
        }

        free(arg_has_call);

        if (arg_array)
            free(arg_array);

        // For nested function calls, set up REG_A0 with static link
        // The static link is the callee's parent's frame pointer
        if (calling_nested) {
            Obj *callee = node->lhs->var;
            Obj *callee_parent = callee->parent_fn;
            Obj *current_fn = vm->compiler.current_fn;

            // Determine the static link value based on relationship
            // Static-link/bp passing for nested calls is compiler-internal
            // ABI plumbing, not a user-visible &local (#676): skip recording.
            if (callee_parent == current_fn) {
                // Calling our own nested function - pass our bp
                emit_lea3_internal(vm, REG_A0, 0); // LEA3 with offset 0 = current bp
            } else if (current_fn && current_fn->is_nested) {
                // We're nested and calling a sibling or parent's nested
                // function Walk our static chain to find callee's parent's bp
                Obj *static_link = find_static_link_var(current_fn);
                if (static_link) {
                    emit_lea3_internal(vm, REG_A0, static_link->offset);
                    emit_rr(vm, LDR_D, REG_A0, REG_A0);
                    // Walk chain if needed
                    for (Obj *fn = current_fn->parent_fn;
                         fn && fn != callee_parent; fn = fn->parent_fn) {
                        emit_addi3(vm, REG_A0, REG_A0,
                                   -8); // static_link offset
                        emit_rr(vm, LDR_D, REG_A0, REG_A0);
                    }
                } else {
                    // Fallback: use current bp
                    emit_lea3_internal(vm, REG_A0, 0);
                }
            } else {
                // Fallback: use current bp (shouldn't happen if parser is
                // correct)
                emit_lea3_internal(vm, REG_A0, 0);
            }
        }

        // Call function
        if (node->lhs->kind == ND_VAR && node->lhs->var->is_function) {
            Obj *fn = node->lhs->var;
            if (is_tail) {
                // Record callee; ND_RETURN emits CALLT + patch after cleanup.
                // If inlining/builtins fired earlier and we never reach here,
                // pending_tail_callee stays NULL and ND_RETURN falls back to LEV3.
                vm->compiler.pending_tail_callee = fn;
            } else {
                // Dead-call elimination: skip pure/const calls whose result is unused.
                // Arguments were already evaluated above, so their side effects still run.
                bool skip_dead_call = vm->compiler.opt_level >= 1 &&
                                      dest_reg == REG_ZERO &&
                                      (fn->is_pure || fn->is_func_const);
                if (!skip_dead_call) {
                    emit(vm, CALL);
                    Pc patch = emit_word_ptr(vm);
                    vm->text_seg[patch] = 0; // Will be patched later

                    // Record call patch location for later resolution
                    PATCH_GROW(vm, call_patches, num_call_patches, call_patches_cap);
                    vm->compiler.call_patches[vm->compiler.num_call_patches].location =
                        patch;
                    vm->compiler.call_patches[vm->compiler.num_call_patches].function =
                        fn;
                    vm->compiler.num_call_patches++;
                }
            }
        } else {
            // Indirect call - function pointer in register.
            // Evaluate the fn-ptr expression unconditionally (it may have
            // side effects, e.g. table[i++]).  Only skip the dispatch when
            // the callee's function type is annotated pure/const and the
            // result is unused.
            int r_fn = alloc_temp_reg();
            if (hoist_callee) {
                // #885: already evaluated and spilled above, before any arg
                // register was staged -- pop it back now instead of
                // re-evaluating (which would run its side effects, e.g. the
                // dlsym() call, a second time).
                emit_pop3(vm, r_fn);
            } else {
                gen_expr(vm, node->lhs, r_fn);
            }
            bool skip_dead_calln = vm->compiler.opt_level >= 1 &&
                                   dest_reg == REG_ZERO &&
                                   node->func_ty &&
                                   (node->func_ty->is_pure ||
                                    node->func_ty->is_func_const);
            if (!skip_dead_calln) {
                // Meta word (InstrWord = uint32_t) layout: bits 0-15 nargs,
                // bit 16 returns_double, bit 17 returns_float, bit 18
                // is_variadic, bits 19-31 fixed_param_count (13 bits, up to
                // 8191 fixed params) (#874/#875 -- lets op_CALLN_fn tell
                // fixed flonum params (FREG_A0+) from variadic-tail doubles
                // (bit-pattern in REG_A0+, matching the internal-call ABI
                // above) and select ffi_prep_cif_var for the callee).
                emit(vm, CALLN);
                emit_word(vm, ENCODE_R(r_fn));
                emit_word(vm, ((InstrWord)(node->ty->kind == TY_FLOAT ? 0
                                                : is_flonum(node->ty) ? 1 : 0) << 16) |
                                  ((InstrWord)(node->ty->kind == TY_FLOAT ? 1 : 0) << 17) |
                                  ((InstrWord)(is_variadic_call ? 1 : 0) << 18) |
                                  ((InstrWord)(fixed_param_count & 0x1FFF) << 19) |
                                  (InstrWord)(nargs & 0xFFFF));
                emit_i64(vm, (long long)call_double_arg_mask);
                emit_i64(vm, (long long)call_float_arg_mask);
            }
            free_temp_reg(r_fn);
        }

        // Clean up stack args pushed before the call
        if (num_stack_args > 0) {
            emit_with_arg(vm, ADJ, num_stack_args);
        }

        // Noreturn functions never return — trap if execution continues
        if (node->func_ty->is_noreturn) {
            emit(vm, BTRAP);
            return;
        }

        // Function calls clobber all temp registers (caller-saved)
        // Reset allocator so caller will recompute any addresses it needs.
        // Also invalidate the restrict cache again: see the comment on the
        // matching invalidate in the tail-call branch above (#754) -- this
        // call's own argument expressions may have filled a cache entry
        // after the up-front invalidate at the top of ND_FUNCALL ran.
        restrict_cache_invalidate_all(vm);
        reset_temp_regs();

        // Note: With runtime return buffer rotation (RETBUF opcode), chained
        // calls like f(g(), h()) automatically get different buffers for g()
        // and h()'s results. No caller-side copy is needed.

        // Result in REG_A0/FREG_A0
        if (is_flonum(node->ty)) {
            if (dest_reg != FREG_A0) {
                emit_fmov3(vm, dest_reg, FREG_A0);
            }
        } else if (is_vector(node->ty)) {
            // Vector return (#714): REG_A0 holds the RETBUF buffer address
            // (see ND_RETURN's vector branch) -- load the value out of it
            // into the destination vreg (width from the return type, #722).
            // Skip when the result is discarded (dest_reg == REG_ZERO):
            // unlike int/float write opcodes, VLDR has no built-in "writes to
            // REG_ZERO are discarded" guard (op_VLDR_fn, ops.c), so an
            // unconditional load here would write into vregs[0] instead of
            // being a no-op -- harmless in practice (index 0 is never handed
            // out by alloc_temp_reg) but skip it anyway rather than relying
            // on that.
            if (dest_reg != REG_ZERO) {
                if (vm->flags & CCCC_POINTER_CHECKS)
                    emit_rr(vm, CHKP3, REG_A0, 0);
                emit_rrs(vm, VLDR, dest_reg, REG_A0, node->ty->size);
            }
        } else {
            if (dest_reg != REG_A0) {
                emit_mov3(vm, dest_reg, REG_A0);
            }
        }
        return;
    }

    // ND_MEMZERO: zero-clear a local variable's storage region.
    // Emitted as the first operand of the ND_COMMA produced by lvar_initializer
    // for partial aggregate initialisers (e.g. `T arr[N] = {0}`), and (#982)
    // by var_definition's VLA initializer path for a partial VLA brace
    // initializer.  The MSET opcode mirrors MCPY: dest=REG_A0, count=REG_A2.
    //
    // __block variables: their stack slot (bp+offset) holds an 8-byte heap
    // pointer written by the function prologue; a blind MSET of var->ty->size
    // bytes at the slot would corrupt the pointer.  Instead, dereference the
    // slot to obtain the heap cell address and zero through that — mirroring
    // gen_addr's normal-local block path.  This ensures that `__block T arr[N]
    // = {partial}` correctly zeroes the unspecified elements in the heap cell.
    //
    // TY_VLA (#982, defect D): the frame slot holds the alloca'd data
    // pointer, same shape as the __block case above, so it's dereferenced
    // the same way. A VLA and a __block variable can't currently coincide
    // (no __block VLA is reachable through the parser), so the two arms are
    // mutually exclusive by construction; asserted rather than silently
    // preferring one if that ever changes. The byte count is NOT
    // var->ty->size -- that's TY_VLA's placeholder constant (vla_of()) --
    // it's the runtime value var_definition() stashed in node->rhs (a
    // ND_VAR read of ty->vla_size, the same Obj the preceding alloca() call
    // was sized from).
    case ND_MEMZERO:
        // Compiler-internal zero-init of the var's own storage: the address
        // is consumed synchronously by the MSET below and never survives
        // beyond it (#676).
        // A __block VLA is not reachable through the parser today, so the
        // is_block_var and TY_VLA slot-dereference paths below have never
        // been decided against each other -- hard-error rather than
        // silently letting branch order guess (an `assert` would instead
        // vanish under -DNDEBUG, e.g. the `release` build target, src/
        // build.c's make_cccc_exe_named_opt, leaving this exact case
        // silently mis-zeroed in a release binary).
        if (node->var->is_block_var && node->var->ty->kind == TY_VLA)
            error_tok(vm, node->tok,
                      "internal error: __block VLA zero-init is not "
                      "implemented (unreachable through the current parser)");
        if (node->var->is_block_var || node->var->ty->kind == TY_VLA) {
            emit_lea3_internal(vm, REG_A0, node->var->offset); // &stack slot
            emit_rr(vm, LDR_D, REG_A0, REG_A0);       // heap ptr -> A0
        } else {
            emit_lea3_internal(vm, REG_A0, node->var->offset); // bp + offset -> A0
        }
        if (node->rhs) {
            reset_temp_regs();
            gen_expr(vm, node->rhs, REG_A2); // runtime byte count -> A2
        } else {
            emit_li3(vm, REG_A2, node->var->ty->size); // byte count -> A2
        }
        emit(vm, MSET);
        return;

    case ND_LOGAND: {
        // Logical AND with short-circuit evaluation.
        // Reuse dest_reg as the condition scratch to avoid O(depth) register
        // accumulation for deeply nested && chains (#587 gap).
        // Guard: dest_reg may be REG_ZERO (discarded expression statement) — the
        // zero register silently discards writes, so both conditions read back as 0
        // and jz is always taken, preventing the rhs and any side-effects from
        // running. Allocate a real temp when dest_reg == REG_ZERO and free it after
        // the last jz (before generating the final 0/1 into dest_reg).
        int r_cond = (dest_reg == REG_ZERO) ? alloc_temp_reg() : dest_reg;
        mark_temp_reg_used(r_cond); // protect from inner allocs
        gen_cond_expr(vm, node->lhs, r_cond);
        Pc jz_false = emit_jz3(vm, r_cond);

        gen_cond_expr(vm, node->rhs, r_cond);
        Pc jz_false2 = emit_jz3(vm, r_cond);
        if (r_cond != dest_reg) free_temp_reg(r_cond);

        // Both true
        emit_li3(vm, dest_reg, 1);
        emit(vm, JMP);
        Pc jmp_end = emit_word_ptr(vm);

        // At least one false
        vm->text_seg[jz_false] = vm->text_ptr + 1;
        vm->text_seg[jz_false2] = vm->text_ptr + 1;
        emit_li3(vm, dest_reg, 0);
        vm->text_seg[jmp_end] = vm->text_ptr + 1;
        return;
    }

    case ND_LOGOR: {
        // Logical OR with short-circuit evaluation.
        // Same dest_reg reuse + REG_ZERO guard as ND_LOGAND above (see comment there).
        int r_cond = (dest_reg == REG_ZERO) ? alloc_temp_reg() : dest_reg;
        mark_temp_reg_used(r_cond);
        gen_cond_expr(vm, node->lhs, r_cond);
        Pc jnz_true = emit_jnz3(vm, r_cond);

        gen_cond_expr(vm, node->rhs, r_cond);
        Pc jnz_true2 = emit_jnz3(vm, r_cond);
        if (r_cond != dest_reg) free_temp_reg(r_cond);

        // Both false
        emit_li3(vm, dest_reg, 0);
        emit(vm, JMP);
        Pc jmp_end = emit_word_ptr(vm);

        // At least one true
        vm->text_seg[jnz_true] = vm->text_ptr + 1;
        vm->text_seg[jnz_true2] = vm->text_ptr + 1;
        emit_li3(vm, dest_reg, 1);
        vm->text_seg[jmp_end] = vm->text_ptr + 1;
        return;
    }

    case ND_STMT_EXPR: {
        // Statement expression ({ stmt; expr })
        // Generate all statements, result of last expression goes to dest_reg
        for (Node *n = node->body; n; n = n->next) {
            if (!n->next && n->kind == ND_EXPR_STMT && n->lhs) {
                // Last statement - evaluate and keep result
                gen_expr(vm, n->lhs, dest_reg);
            } else {
                gen_stmt(vm, n);
            }
        }
        return;
    }

    case ND_FRAME_ADDR:
        // __builtin_frame_address(0) - returns current base pointer
        // LEA3 with offset 0 loads bp + 0 = bp into dest_reg
        emit_lea3(vm, dest_reg, 0);
        return;

    case ND_RETURN_ADDR:
        // __builtin_return_address(n) - returns return address n frames up as void*
        // Emits RETADDR opcode which walks the saved-bp chain at runtime and
        // bounds-checks each step.  Returns NULL past the outermost frame.
        emit_ri(vm, RETADDR, dest_reg, node->val);
        return;

    case ND_DYNOBJ_SIZE: {
        // __builtin_dynamic_object_size(ptr, type) — runtime heap size lookup.
        // Evaluate the pointer into a temp register, then emit DYNOBJSZ which
        // reads AllocHeader.requested_size for VM heap base pointers.
        int ptr_reg = alloc_temp_reg();
        gen_expr(vm, node->lhs, ptr_reg);
        emit_rri(vm, DYNOBJSZ, dest_reg, ptr_reg, node->val);
        free_temp_reg(ptr_reg);
        return;
    }

    case ND_BITOP: {
        // Bit-manipulation builtins: val = (op_selector<<8) | bit_width
        int bitop_op = (int)(node->val >> 8);
        int bitop_width = (int)(node->val & 0xFF);
        int bitop_tmp = alloc_temp_reg();
        gen_expr(vm, node->lhs, bitop_tmp);
        switch (bitop_op) {
        case 0: // CLZ
            emit_rri(vm, CLZ, dest_reg, bitop_tmp, bitop_width);
            break;
        case 1: // CTZ
            emit_rri(vm, CTZ, dest_reg, bitop_tmp, bitop_width);
            break;
        case 2: // POPCOUNT
            emit_rr(vm, POPCOUNT, dest_reg, bitop_tmp);
            break;
        case 3: { // PARITY = popcount & 1
            int parity_tmp = alloc_temp_reg();
            emit_rr(vm, POPCOUNT, dest_reg, bitop_tmp);
            emit_li3(vm, parity_tmp, 1);
            emit_rrr(vm, AND3, dest_reg, dest_reg, parity_tmp);
            free_temp_reg(parity_tmp);
            break;
        }
        case 4: // FFS
            emit_rri(vm, FFS, dest_reg, bitop_tmp, bitop_width);
            break;
        case 5: // BSWAP (bitop_width is byte-width)
            emit_rri(vm, BSWAP, dest_reg, bitop_tmp, bitop_width);
            break;
        default:
            error_tok(vm, node->tok, "codegen: unknown ND_BITOP selector %d", bitop_op);
        }
        free_temp_reg(bitop_tmp);
        return;
    }

    case ND_OVERFLOW_ARITH: {
        // Checked arithmetic: val=0/1/2 (add/sub/mul)
        // a→REG_A0, b→REG_A1, ptr→REG_A2; result bool→dest_reg
        // Encode op_type and result type kind in a single immediate
        Type *result_ty = node->cas_addr->ty->base;
        // type_kind encoding: (size_bytes << 1) | is_unsigned
        int kind_enc = (result_ty->size << 1) | (result_ty->is_unsigned ? 1 : 0);
        long long packed = ((long long)node->val << 8) | kind_enc;
        reset_temp_regs();
        gen_expr(vm, node->lhs, REG_A0);
        gen_expr(vm, node->rhs, REG_A1);
        gen_expr(vm, node->cas_addr, REG_A2);
        emit_with_arg(vm, IOVFL, packed);
        if (dest_reg != REG_A0)
            emit_mov3(vm, dest_reg, REG_A0);
        return;
    }

    case ND_ALOAD: {
        // Atomic-tagged load via ALDR: rd = *(T*)addr, tags atomic_shadow.
        // Falls back to plain emit_load for floats or exotic sizes.
        int r_addr = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_addr);
        Type *base_ty = node->lhs->ty->base;
        int sz = base_ty->size;
        if ((sz == 1 || sz == 2 || sz == 4 || sz == 8) && !is_flonum(base_ty)) {
            long long width_enc = ((long long)sz << 1) | (base_ty->is_unsigned ? 1 : 0);
            // #985: ALDR bypasses emit_load_ex/emit_load_safety_checks
            // entirely, so it needs its own CHKD. Unconditional (no
            // addr_is_local_frame gate like the vector CHKD sites use):
            // node->lhs here is a pointer-*valued* expression, not the
            // lvalue node itself, so addr_is_local_frame's default arm
            // would always return false anyway -- op_CHKD_fn already
            // no-ops for a non-heap address, so the gate would buy
            // nothing but a dead call. See #983's CHKD, #497 for why this
            // was originally deferred (that hazard is in ALDR's own
            // operand-word decode in src/optimize.c, which this
            // standalone CHKD instruction never touches).
            if (vm->flags & CCCC_BOUNDS_CHECKS)
                emit_rri(vm, CHKD, r_addr, 0, (long long)sz);
            emit_rri(vm, ALDR, dest_reg, r_addr, width_enc);
        } else {
            emit_load(vm, base_ty, dest_reg, r_addr);
        }
        free_temp_reg(r_addr);
        return;
    }

    case ND_ASTORE: {
        // Atomic-tagged store via ASTR: *(T*)addr = val, tags atomic_shadow.
        // Falls back to plain emit_store for floats or exotic sizes.
        // Result is the stored value (C assignment semantics).
        int r_val  = alloc_temp_reg();
        int r_addr = alloc_temp_reg();
        gen_expr(vm, node->rhs, r_val);
        gen_expr(vm, node->lhs, r_addr);
        Type *base_ty = node->lhs->ty->base;
        int sz = base_ty->size;
        if ((sz == 1 || sz == 2 || sz == 4 || sz == 8) && !is_flonum(base_ty)) {
            long long width_enc = ((long long)sz << 1) | (base_ty->is_unsigned ? 1 : 0);
            // #985: ASTR bypasses emit_store_ex entirely, same reasoning
            // as the ALDR CHKD above.
            if (vm->flags & CCCC_BOUNDS_CHECKS)
                emit_rri(vm, CHKD, r_addr, 0, (long long)sz);
            emit_rri(vm, ASTR, r_val, r_addr, width_enc);
        } else {
            emit_store(vm, base_ty, r_val, r_addr);
        }
        if (dest_reg != r_val)
            emit_mov3(vm, dest_reg, r_val);
        free_temp_reg(r_val);
        free_temp_reg(r_addr);
        return;
    }

    case ND_EXCH: {
        // atomic_exchange(obj_ptr, new_val) → old_val
        // Operands in REG_A0 (addr), REG_A1 (new value); result in REG_A0.
        Type *base_ty = node->lhs->ty->base;
        int sz = base_ty->size;
        if ((sz != 1 && sz != 2 && sz != 4 && sz != 8) || is_flonum(base_ty))
            error_tok(vm, node->tok,
                      "atomic_exchange: unsupported type (must be 1/2/4/8-byte integer or pointer)");
        long long width_enc = ((long long)sz << 1) | (base_ty->is_unsigned ? 1 : 0);
        reset_temp_regs();
        gen_expr(vm, node->lhs, REG_A0); // addr (obj pointer)
        gen_expr(vm, node->rhs, REG_A1); // new value
        // #985: CHKD ahead of AXCHG itself, same unconditional reasoning
        // as ALDR/ASTR above. This is a standalone CHKD instruction
        // emitted before AXCHG -- it never touches AXCHG's own operand
        // word, so it cannot reopen the #497 aliasing hazard that gated
        // this off originally.
        if (vm->flags & CCCC_BOUNDS_CHECKS)
            emit_rri(vm, CHKD, REG_A0, 0, (long long)sz);
        emit_with_arg(vm, AXCHG, width_enc);
        // old value returned in REG_A0
        if (dest_reg != REG_A0)
            emit_mov3(vm, dest_reg, REG_A0);
        return;
    }

    case ND_CAS: {
        // compare_and_swap(obj_ptr, expected_ptr, desired) → bool
        // Operands: REG_A0 = obj_ptr (T*), REG_A1 = expected_ptr (T*),
        //           REG_A2 = desired (T); result bool in REG_A0.
        Type *base_ty = node->cas_addr->ty->base;
        int sz = base_ty->size;
        if ((sz != 1 && sz != 2 && sz != 4 && sz != 8) || is_flonum(base_ty))
            error_tok(vm, node->tok,
                      "compare_and_swap: unsupported type (must be 1/2/4/8-byte integer or pointer)");
        long long width_enc = ((long long)sz << 1) | (base_ty->is_unsigned ? 1 : 0);
        reset_temp_regs();
        gen_expr(vm, node->cas_addr, REG_A0); // T* — pointer to atomic variable
        gen_expr(vm, node->cas_old,  REG_A1); // T* — pointer to expected value
        gen_expr(vm, node->cas_new,  REG_A2); // T  — desired value

        // #937: an `_Atomic` [[cccc::ntarray]] element's `+=`/`++`/`--`
        // desugars (to_assign(), src/parse.c) into this ND_CAS rather than
        // an ND_ASSIGN, so it needs its own CHKNT emission -- the ND_ASSIGN
        // site below never sees this store. checked_nt_terminator/
        // checked_bounds_hi/checked_access_size are populated on `node`
        // itself by to_assign()'s atomic branch when the original lvalue
        // deref (`s[n]` in `s[n] += 1`) had them set. r_hi is a fresh T-reg
        // (alloc_temp_reg(), disjoint from REG_A0-A2), and checked_bounds_hi
        // is guaranteed call-free (side-effect-free bounds expression), so
        // evaluating it here cannot clobber the just-staged REG_A0/A2 this
        // check (and the ACAS call right after) both still need.
        if ((vm->flags & CCCC_CHECKED_BOUNDS) && node->checked_nt_terminator) {
            // #939: no float/aggregate handling needed here -- the
            // unsupported-type check above (is_flonum(base_ty) / sz not in
            // {1,2,4,8}) already rejects every pointee CHKNT can't guard
            // before this point is ever reached.
            // #945: gen_checked_nt_hi() is the same object-expression hoist
            // re-init as the other two CHKR/CHKNT sites (gen_addr's
            // ND_DEREF case and the ND_ASSIGN store guard above) -- also
            // call-free by the same reasoning as checked_bounds_hi itself
            // (node_has_side_effects() already declined the hoist candidate
            // otherwise), so it's just as safe to evaluate here without
            // disturbing the just-staged REG_A0-A2 this check and the ACAS
            // call right after both need.
            int r_hi = gen_checked_nt_hi(vm, node);
            emit_chknt(vm, REG_A0, r_hi, REG_A2, node->checked_access_size);
            free_temp_reg(r_hi);
        }

        // #985: two CHKDs ahead of ACAS -- obj (REG_A0) and expected
        // (REG_A1). ACAS reads *and writes* through expected on failure,
        // so guarding only the object pointer would leave half the
        // dereference surface unchecked. Same unconditional/no-gate and
        // #497-safety reasoning as the ALDR/ASTR/AXCHG sites above; mirrors
        // the CHKNT emission just above, which already proved staging
        // extra checks here doesn't disturb REG_A0-A2.
        if (vm->flags & CCCC_BOUNDS_CHECKS) {
            emit_rri(vm, CHKD, REG_A0, 0, (long long)sz);
            emit_rri(vm, CHKD, REG_A1, 0, (long long)sz);
        }

        emit_with_arg(vm, ACAS, width_enc);
        // bool result in REG_A0
        if (dest_reg != REG_A0)
            emit_mov3(vm, dest_reg, REG_A0);
        return;
    }

    case ND_VLA_PTR:
        // VLA pointer/designator: load the stored pointer value
        // VLAs are implemented by storing a pointer to dynamically allocated
        // memory The pointer itself is a local variable
        if (node->var->is_local) {
            // Slot address only feeds the immediate load below (#676).
            emit_lea3_internal(vm, dest_reg, node->var->offset); // Address of pointer
            emit_rr(vm, LDR_D, dest_reg, dest_reg); // Load the pointer value
        } else {
            error_tok(vm, node->tok, "VLA must be local");
        }
        return;

    case ND_LABEL_VAL: {
        // Label address: &&label (GCC extension for computed goto)
        // Emit LTA3 with placeholder offset that will be patched later
        Pc label_addr_loc = emit_lta3(vm, dest_reg, 0);
        // Record patch location so it gets resolved when label is defined
        add_label_patch(node->unique_label ? node->unique_label : node->label,
                        label_addr_loc, true);
        return;
    }

    case ND_BLOCK_LITERAL: {
        // Block descriptor layout (stack-allocated in enclosing function's frame):
        //   [0]  = invoke pointer (function address)
        //   [8]  = descriptor byte-size (for Block_copy to know how much to malloc)
        //   [16] = first captured value, cc_block_capture_offset(block_fn,1)-th
        //          byte = second captured value, ...
        //
        // #994: capture slots are no longer a flat one-word-each array -- a
        // by-value aggregate capture wider than 8 bytes gets a wider slot
        // (cc_block_capture_offset, shared with parse.c so the two files
        // can't drift apart on the layout).
        //
        // Stack allocation (via block_desc_var) gives each function invocation its
        // own descriptor, so multiple calls to the same function return independent
        // block instances without aliasing.

        int num_captures = node->num_block_captures;
        long descriptor_size = node->block_desc_var->ty->size;

        // Load address of the pre-allocated stack descriptor slot
        int r_desc = alloc_temp_reg();
        emit_lea3(vm, r_desc, node->block_desc_var->offset);
        mark_temp_reg_used(r_desc);

        // Load function address (will be patched later)
        int r_invoke = alloc_temp_reg();
        Pc invoke_addr_loc = emit_lta3(vm, r_invoke, 0); // Placeholder

        // Record patch for block function address
        PATCH_GROW(vm, func_addr_patches, num_func_addr_patches, func_addr_patches_cap);
        vm->compiler.func_addr_patches[vm->compiler.num_func_addr_patches]
            .location = invoke_addr_loc;
        vm->compiler.func_addr_patches[vm->compiler.num_func_addr_patches]
            .function = node->block_fn;
        vm->compiler.num_func_addr_patches++;

        // Store invoke pointer at descriptor[0]
        emit_rr(vm, STR_D, r_invoke, r_desc);
        free_temp_reg(r_invoke);

        // Store descriptor size at descriptor[1] so Block_copy knows how much to copy
        int r_size = alloc_temp_reg();
        emit_li3(vm, r_size, descriptor_size);
        int r_size_slot = alloc_temp_reg();
        emit_addi3(vm, r_size_slot, r_desc, 8);
        emit_rr(vm, STR_D, r_size, r_size_slot);
        free_temp_reg(r_size_slot);
        free_temp_reg(r_size);

        // Copy captured variable values into descriptor.
        // A capture may come from the enclosing block's own stack frame (direct
        // local) or from the enclosing block's descriptor (transitive capture
        // from a grandparent scope).  Check the enclosing function's capture
        // list first so we read from the right source.
        //
        // #994: an is_block_var/TY_VLA capture always copies a fixed 8-byte
        // pointer, as before. Any other capture whose type needs more than
        // one word (block_capture_needs_mcpy) has r_val left holding its
        // source *address* instead of its loaded value, and is copied via
        // MCPY -- an exact ty->size copy, not a truncating 8-byte load.
        Obj *enc_fn = vm->compiler.current_fn;
        for (int i = 0; i < num_captures; i++) {
            Obj *cap = node->block_captures[i];
            int r_val = alloc_temp_reg();

            int enc_cap_idx = (enc_fn && enc_fn->is_block)
                              ? find_capture_index(enc_fn, cap) : -1;
            bool wide_copy = !cap->is_block_var && cap->ty->kind != TY_VLA &&
                              block_capture_needs_mcpy(cap->ty);

            if (enc_cap_idx >= 0) {
                // Variable lives in the enclosing block's descriptor: read via
                // __static_link so we don't use a stale stack offset from an
                // outer function's frame. Compiler-internal chase (#676):
                // every intermediate address here feeds an immediate load.
                Obj *static_link = find_static_link_var(enc_fn);
                emit_lea3_internal(vm, r_val, static_link->offset);
                emit_rr(vm, LDR_D, r_val, r_val); // descriptor ptr
                emit_addi3(vm, r_val, r_val,
                           cc_block_capture_offset(enc_fn, enc_cap_idx)); // slot addr
                if (cap->is_block_var)
                    emit_rr(vm, LDR_D, r_val, r_val); // heap ptr from descriptor slot
                else if (!wide_copy)
                    emit_load(vm, cap->ty, r_val, r_val); // value from descriptor slot
                // else: r_val already holds the slot's own address, used as
                // the MCPY source below -- the enclosing descriptor's slot
                // holds the aggregate's bytes inline, same as any other
                // capture source.
            } else if (cap->is_block_var) {
                // __block var directly in enclosing stack: copy heap pointer.
                // Slot address only feeds the immediate load (#676).
                emit_lea3_internal(vm, r_val, cap->offset);
                emit_rr(vm, LDR_D, r_val, r_val);
            } else if (cap->is_local) {
                // Regular local directly in enclosing stack: copy value.
                // Slot address only feeds the immediate load (#676) unless
                // it's the MCPY source address itself (wide_copy).
                emit_lea3_internal(vm, r_val, cap->offset);
                // #994: a struct/union/vector/wide-_BitInt/_Decimal
                // *parameter*'s frame slot holds a pointer to the value,
                // not the value's own bytes -- same ABI fact gen_addr's
                // plain ND_VAR case already accounts for (codegen.c,
                // search "passed by pointer too"). One extra dereference
                // is needed here before r_val is a usable source address
                // (or, for a non-wide type that still hits this rule --
                // none do today, but the check is by kind not size, same
                // as block_capture_needs_mcpy -- before emit_load below).
                if (cap->is_param &&
                    (cap->ty->kind == TY_STRUCT || cap->ty->kind == TY_UNION ||
                     cap->ty->kind == TY_VECTOR || is_wide_bitint(cap->ty) ||
                     is_decimal(cap->ty)))
                    emit_rr(vm, LDR_D, r_val, r_val); // load pointer from slot
                if (!wide_copy)
                    emit_load(vm, cap->ty, r_val, r_val);
            } else {
                // Global
                emit_lda3(vm, r_val, cap->offset);
                if (!wide_copy)
                    emit_load(vm, cap->ty, r_val, r_val);
            }

            // Store at descriptor[cc_block_capture_offset(node->block_fn, i)]
            int r_cap_addr = alloc_temp_reg();
            emit_addi3(vm, r_cap_addr, r_desc,
                       cc_block_capture_offset(node->block_fn, i));
            if (wide_copy) {
                // #983: destination is always this call's own freshly
                // allocated descriptor local -- a compile-time-known frame
                // address, never captured or escaped yet, so no CHKD is
                // needed there. The source address may come from a runtime
                // pointer chase through __static_link (enc_cap_idx >= 0),
                // so it gets checked unconditionally, matching emit_load's
                // own dangling_check=true default that the scalar capture
                // path above already always pays.
                mark_temp_reg_used(r_val);
                mark_temp_reg_used(r_cap_addr);
                if (vm->flags & CCCC_BOUNDS_CHECKS)
                    emit_rri(vm, CHKD, r_val, 0, (long long)cap->ty->size);
                emit_mov3(vm, REG_A0, r_cap_addr);
                emit_mov3(vm, REG_A1, r_val);
                emit_li3(vm, REG_A2, (long long)cap->ty->size);
                emit(vm, MCPY);
            } else {
                emit_rr(vm, STR_D, r_val, r_cap_addr);
            }
            free_temp_reg(r_cap_addr);
            free_temp_reg(r_val);
        }

        // Return descriptor address
        if (dest_reg != r_desc) {
            emit_mov3(vm, dest_reg, r_desc);
        }
        free_temp_reg(r_desc);
        return;
    }

    case ND_BLOCK_CALL: {
        // Block invocation via descriptor:
        // 1. Evaluate block expression to get descriptor address
        // 2. Load function pointer from descriptor[0]
        // 3. Pass descriptor in A0 (as __static_link for captured vars)
        // 4. Pass user arguments in A1-A7

        // First, evaluate block expression to get descriptor address
        int r_desc = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_desc);
        mark_temp_reg_used(r_desc);

        // Count arguments
        // int nargs = 0;
        // for (Node *a = node->args; a; a = a->next) nargs++;

        // Generate user arguments into A1-A7 for ints, FREG_A0-A7 for floats.
        // The descriptor occupies REG_A0 (int slot 0), so int args start at
        // int_arg_idx=1; there is no float static-link, so float args start
        // at float_arg_idx=0. This must match the independent int/float
        // register counters ENT3 uses to spill incoming params (op_ENT3_fn,
        // src/ops.c) -- a single combined "user args start at A1" counter
        // applied to both register files (the previous scheme here) puts
        // float args in the wrong FREG_A* slot as soon as a block takes both
        // an int and a float parameter, or a float parameter at all, since
        // ENT3 counts int and float params separately. Pre-existing bug,
        // found and fixed alongside #712.
        int int_arg_idx = 1;
        int float_arg_idx = 0;
        for (Node *a = node->args; a; a = a->next) {
            add_type(vm, a);
            if (is_vector(a->ty)) {
                // Block invocation has no by-memory ABI for aggregates at
                // all (no RETBUF/pointer-arg machinery here) -- reject
                // cleanly rather than mis-marshalling a vregs[] value
                // through a plain int arg register (#714).
                error_tok(vm, a->tok,
                          "vector arguments to block calls are not "
                          "supported");
            }
            if (is_flonum(a->ty)) {
                if (float_arg_idx >= 8) {
                    error_tok(vm, a->tok, "too many block arguments");
                }
                // Evaluate into a temp-numbered float scratch, not
                // FREG_A0+float_arg_idx directly -- see
                // gen_flonum_arg_to_scratch (#712): a leading integer arg
                // already placed in REG_A1+k could otherwise be clobbered by
                // an integer scratch this expression's own codegen reuses
                // (e.g. deref address, int->float cast source).
                int fs = gen_flonum_arg_to_scratch(vm, a);
                emit_fmov3(vm, FREG_A0 + float_arg_idx, fs);
                free_temp_reg(fs);
                float_arg_idx++;
            } else {
                if (int_arg_idx >= 8) {
                    error_tok(vm, a->tok, "too many block arguments");
                }
                gen_expr(vm, a, REG_A0 + int_arg_idx);
                int_arg_idx++;
            }
        }

        // Load function pointer from descriptor[0]
        int r_fn = alloc_temp_reg();
        emit_rr(vm, LDR_D, r_fn, r_desc);

        // Pass descriptor in A0 (for __static_link access to captures)
        emit_mov3(vm, REG_A0, r_desc);
        free_temp_reg(r_desc);

        // Indirect call via function pointer
        emit(vm, CALLI);
        emit_word(vm, ENCODE_R(r_fn));
        free_temp_reg(r_fn);

        reset_temp_regs();

        // Result is in REG_A0 or FREG_A0
        if (is_flonum(node->ty)) {
            if (dest_reg != FREG_A0) {
                emit_fmov3(vm, dest_reg, FREG_A0);
            }
        } else if (dest_reg != REG_A0) {
            emit_mov3(vm, dest_reg, REG_A0);
        }
        return;
    }

    default:
        error_tok(vm, node->tok, "codegen: unsupported expression node kind %d",
                  node->kind);
    }
}

