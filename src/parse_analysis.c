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

// Standalone analysis passes that run over an already-parsed function body:
// constant evaluation (eval/const_expr), __builtin_object_size escape and
// allocation tracking, and the flow-sensitive nonnull checker (#679/#689).

#include "./parse_internal.h"

int64_t eval(VirtualMachine *vm, Node *node) {
    return eval2(vm, node, NULL);
}

static Initializer *constexpr_init_for_node(Node *node) {
    if (!node)
        return NULL;
    if (node->kind == ND_VAR && node->var && node->var->is_constexpr)
        return (Initializer *)node->var->constexpr_init;
    if (node->kind == ND_MEMBER) {
        Initializer *base = constexpr_init_for_node(node->lhs);
        if (!base || !node->member)
            return NULL;
        if (base->ty->kind == TY_UNION) {
            if (base->mem != node->member)
                return NULL;
            return base->children[node->member->idx];
        }
        if (base->ty->kind != TY_STRUCT)
            return NULL;
        return base->children[node->member->idx];
    }
    return NULL;
}

Node *constexpr_expr_for_node(Node *node) {
    Initializer *init = constexpr_init_for_node(node);
    return init ? init->expr : NULL;
}

static int64_t eval_rval(VirtualMachine *vm, Node *node, char ***label);

// ========== Wide (>8-byte) compile-time constant folding (#1122) ==========
//
// eval2 above is int64_t end-to-end, so it has no channel for a value wider
// than 64 bits. That's fine for everyday constant expressions, but two
// things need more: a scalar global initializer whose *own* type is a
// _BitInt(65..65535)/__int128 (write_gvar_data's scalar tail, previously an
// unconditional 64-bit eval2 + write_buf that either crashed on the >8-byte
// write or silently truncated), and any narrower constant expression that
// merely *contains* wide arithmetic (e.g. `(unsigned long long)((wide_expr)
// / 3)` -- eval2 would fold the wide subexpression in 64 bits and get a
// wrong answer with no diagnostic at all).
//
// Rather than a bespoke 128-bit evaluator, this reuses the arbitrary-width
// word-array helpers in src/stdlib/wide_bitint.c (declared in internal.h)
// that the VM itself uses for runtime _BitInt(>64) arithmetic -- so a global
// initializer folds to exactly the same bytes a local variable would compute
// at runtime, and arbitrary width (up to BITINT_MAXWIDTH) comes for free.

// words/width for a wide-or-narrow integer type's own storage. Narrow types
// (<=8 bytes) are treated as a single 64-bit word for extension purposes --
// eval2 already produces a correctly sign/zero-extended 64-bit value for
// them, so width=64 here is exact, not an approximation.
static void wide_words_of(Type *ty, int *words, int *width) {
    *words = ty->size > 8 ? ty->size / 8 : 1;
    *width = (ty->kind == TY_BITINT && ty->bit_width > 64) ? ty->bit_width
                                                           : *words * 64;
}

typedef struct {
    uint64_t *buf;
    int       words;
    int       width;
    bool      is_unsigned;
} WideVal;

static void eval_wide_node(VirtualMachine *vm, Node *node, uint64_t *dst);

// Evaluate `node` at its own type's width, allocating the backing buffer in
// the parser arena. Narrow (<=8-byte) nodes go through eval2 -- a single
// 64-bit word is exact for them (see wide_words_of above).
static WideVal eval_wide_operand(VirtualMachine *vm, Node *node) {
    add_type(vm, node);
    WideVal v;
    v.is_unsigned = node->ty && node->ty->is_unsigned;
    if (is_wide_bitint(node->ty)) {
        wide_words_of(node->ty, &v.words, &v.width);
        v.buf = arena_alloc(&vm->compiler.parser_arena, (size_t)v.words * 8);
        eval_wide_node(vm, node, v.buf);
    } else {
        v.words  = 1;
        v.width  = 64;
        v.buf    = arena_alloc(&vm->compiler.parser_arena, 8);
        v.buf[0] = (uint64_t)eval2(vm, node, NULL);
    }
    return v;
}

// Extend/truncate an already-evaluated operand into a (words,width)-sized
// destination buffer (which the caller owns).
static void wide_extend_into(WideVal v, int words, int width, uint64_t *dst) {
    __cccc_bitint_extend(dst, v.buf, v.words, v.width, words, width,
                         !v.is_unsigned);
}

// True if `node` (already add_type'd) is non-zero, folding wide operands via
// __cccc_bitint_nonzero instead of a truncating eval2 call.
static bool eval_wide_truthy(VirtualMachine *vm, Node *node) {
    add_type(vm, node);
    if (is_wide_bitint(node->ty)) {
        WideVal v = eval_wide_operand(vm, node);
        return __cccc_bitint_nonzero(v.buf, v.words) != 0;
    }
    return eval2(vm, node, NULL) != 0;
}

// Evaluate `node`, which must itself have a wide _BitInt type, into dst
// (node->ty->size bytes, little-endian words). Mirrors eval2's node-kind
// coverage, but every arithmetic/bitwise/shift op routes through the
// wide_bitint.c word-array helpers instead of native int64_t operators.
static void eval_wide_node(VirtualMachine *vm, Node *node, uint64_t *dst) {
    add_type(vm, node);
    Type *ty = node->ty;
    int   words, width;
    wide_words_of(ty, &words, &width);

    switch (node->kind) {
        case ND_NUM:
            if (node->wide_digits)
                __cccc_bitint_from_str(dst, node->wide_digits, node->wide_base,
                                       words, width);
            else if (ty->is_unsigned)
                __cccc_bitint_from_u64(dst, (unsigned long long)node->val,
                                       words, width);
            else
                __cccc_bitint_from_i64(dst, (long long)node->val, words, width);
            return;
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_BITAND:
        case ND_BITOR:
        case ND_BITXOR: {
            WideVal   l = eval_wide_operand(vm, node->lhs);
            WideVal   r = eval_wide_operand(vm, node->rhs);
            uint64_t *le =
                arena_alloc(&vm->compiler.parser_arena, (size_t)words * 8);
            uint64_t *re =
                arena_alloc(&vm->compiler.parser_arena, (size_t)words * 8);
            wide_extend_into(l, words, width, le);
            wide_extend_into(r, words, width, re);
            switch (node->kind) {
                case ND_ADD:
                    __cccc_bitint_add(dst, le, re, words, width);
                    break;
                case ND_SUB:
                    __cccc_bitint_sub(dst, le, re, words, width);
                    break;
                case ND_MUL:
                    __cccc_bitint_mul(dst, le, re, words, width);
                    break;
                case ND_BITAND:
                    __cccc_bitint_and(dst, le, re, words, width);
                    break;
                case ND_BITOR:
                    __cccc_bitint_or(dst, le, re, words, width);
                    break;
                default:
                    __cccc_bitint_xor(dst, le, re, words, width);
                    break;
            }
            return;
        }
        case ND_DIV:
        case ND_MOD: {
            WideVal   l = eval_wide_operand(vm, node->lhs);
            WideVal   r = eval_wide_operand(vm, node->rhs);
            uint64_t *le =
                arena_alloc(&vm->compiler.parser_arena, (size_t)words * 8);
            uint64_t *re =
                arena_alloc(&vm->compiler.parser_arena, (size_t)words * 8);
            wide_extend_into(l, words, width, le);
            wide_extend_into(r, words, width, re);
            if (!__cccc_bitint_nonzero(re, words))
                error_tok(vm, node->rhs->tok,
                          "division by zero in constant expression");
            bool is_signed = !ty->is_unsigned;
            if (node->kind == ND_DIV) {
                if (is_signed)
                    __cccc_bitint_sdiv(dst, le, re, words, width);
                else
                    __cccc_bitint_udiv(dst, le, re, words, width);
            } else {
                if (is_signed)
                    __cccc_bitint_smod(dst, le, re, words, width);
                else
                    __cccc_bitint_umod(dst, le, re, words, width);
            }
            return;
        }
        case ND_SHL:
        case ND_SHR: {
            WideVal   l = eval_wide_operand(vm, node->lhs);
            uint64_t *le =
                arena_alloc(&vm->compiler.parser_arena, (size_t)words * 8);
            wide_extend_into(l, words, width, le);
            long long shift = eval2(vm, node->rhs, NULL);
            if (node->kind == ND_SHL)
                __cccc_bitint_shl(dst, le, shift, words, width);
            else if (ty->is_unsigned)
                __cccc_bitint_ushr(dst, le, shift, words, width);
            else
                __cccc_bitint_sshr(dst, le, shift, words, width);
            return;
        }
        case ND_NEG:
        case ND_BITNOT: {
            WideVal   l = eval_wide_operand(vm, node->lhs);
            uint64_t *le =
                arena_alloc(&vm->compiler.parser_arena, (size_t)words * 8);
            wide_extend_into(l, words, width, le);
            if (node->kind == ND_NEG)
                __cccc_bitint_neg(dst, le, words, width);
            else
                __cccc_bitint_not(dst, le, words, width);
            return;
        }
        case ND_COND:
            if (eval_wide_truthy(vm, node->cond))
                eval_wide_node(vm, node->then, dst);
            else
                eval_wide_node(vm, node->els, dst);
            return;
        case ND_COMMA:
            eval_wide_node(vm, node->rhs, dst);
            return;
        case ND_CAST:
            if (is_flonum(node->lhs->ty)) {
                double    val = eval_double(vm, node->lhs);
                long long bits;
                memcpy(&bits, &val, sizeof(bits));
                __cccc_bitint_from_double(dst, bits, words, width,
                                          !ty->is_unsigned);
                return;
            }
            if (is_decimal(node->lhs->ty))
                error_tok(vm, node->tok,
                          "_Decimal to wide _BitInt constant folding is not "
                          "supported");
            {
                WideVal l = eval_wide_operand(vm, node->lhs);
                wide_extend_into(l, words, width, dst);
            }
            return;
        case ND_VAR:
            if (node->var->is_constexpr) {
                Node *expr = constexpr_expr_for_node(node);
                if (!expr)
                    error_tok(vm, node->tok,
                              "not a scalar compile-time constant");
                eval_wide_node(vm, expr, dst);
                return;
            }
            error_tok(vm, node->tok,
                      "not a compile-time constant (variable reference)");
            return;
        default:
            error_tok(vm, node->tok,
                      "not a compile-time constant (wide integer expression)");
    }
}

// Public entry point (declared in parse_internal.h): fold `node` to a
// constant of type `ty` (which must be an integer type; `ty->size` bytes are
// written to dst, little-endian words). `node`'s own type need not already
// be `ty` -- e.g. a global `_BitInt(128) g = 3;` folds the narrow literal
// `3` and then extends it, exactly like the pre-existing narrow path does
// for e.g. `long g = 3;`.
void eval_wide(VirtualMachine *vm, Node *node, Type *ty, uint64_t *dst) {
    int words, width;
    wide_words_of(ty, &words, &width);
    WideVal v = eval_wide_operand(vm, node);
    wide_extend_into(v, words, width, dst);
}

// #1122: compare two (possibly mixed narrow/wide) operands at their common
// width, for eval2's EQ/NE/LT/LE arms when either side is a wide _BitInt.
// Signedness is keyed off the left operand only, matching the pre-existing
// (imperfect, but unchanged here) convention already used by ND_LT/ND_LE
// below for the narrow case.
static long long eval_wide_cmp(VirtualMachine *vm, Node *node) {
    WideVal   l     = eval_wide_operand(vm, node->lhs);
    WideVal   r     = eval_wide_operand(vm, node->rhs);
    int       words = l.words > r.words ? l.words : r.words;
    int       width = l.width > r.width ? l.width : r.width;
    uint64_t *le = arena_alloc(&vm->compiler.parser_arena, (size_t)words * 8);
    uint64_t *re = arena_alloc(&vm->compiler.parser_arena, (size_t)words * 8);
    wide_extend_into(l, words, width, le);
    wide_extend_into(r, words, width, re);
    return __cccc_bitint_cmp(le, re, words, width, !l.is_unsigned);
}

// Evaluate a given node as a constant expression.
//
// A constant expression is either just a number or ptr+n where ptr
// is a pointer to a global variable and n is a postiive/negative
// number. The latter form is accepted only as an initialization
// expression for a global variable.
int64_t eval2(VirtualMachine *vm, Node *node, char ***label) {
    add_type(vm, node);

    // _Decimal32/64/128: a node whose *own* type is decimal (as opposed to
    // an ND_CAST *of* a decimal operand, handled in the ND_CAST arm below
    // via eval_decimal, #832) can only reach here through a decimal
    // comparison or equality operator (e.g. `1.1dd == 1.1dd` used directly
    // in an integer constant expression) -- that's still rejected; folding
    // it needs decimal arms in ND_EQ/ND_NE/ND_LT/ND_LE, deferred to a
    // follow-up ticket. This message is imprecise for that specific case
    // (it reads like decimal is categorically unusable in an ICE, when only
    // decimal *comparisons* are), but reject explicitly either way rather
    // than falling through to a switch arm that reads node->val (always 0
    // for a decimal literal -- see tokenize.c).
    if (is_decimal(node->ty))
        error_tok(vm, node->tok,
                  "_Decimal is not valid in an integer constant expression");

    if (is_flonum(node->ty))
        // A bare "return eval_double(...)" here implicitly truncates a
        // double to int64_t via a plain C cast -- UB in the host
        // compiler for NaN/out-of-range values, same as F2I3/F2I3_F32 in
        // ops.c (#775). This fires when a float/double-typed constant
        // subexpression needs folding into an integer-typed constant
        // context (e.g. under an outer ND_CAST to an integer type).
        return cccc_f64_to_i64(eval_double(vm, node));

    // #1122: a node whose own type (after usual arithmetic conversions) is
    // a wide _BitInt/__int128 can't be folded by this function's plain
    // int64_t arithmetic below without silently discarding everything past
    // bit 63 -- e.g. `(unsigned long long)(((unsigned __int128)1<<64|7)/3)`
    // used to fold the division in 64 bits and get 2 instead of the correct
    // 6148914691236517207. Delegate the whole subtree to the wide evaluator
    // and narrow the result exactly like a runtime `(int64_t)wide_val`
    // cast would (__cccc_bitint_to_i64 takes the low word).
    if (is_wide_bitint(node->ty)) {
        WideVal v = eval_wide_operand(vm, node);
        return __cccc_bitint_to_i64(v.buf, v.words, v.width, !v.is_unsigned);
    }

    switch (node->kind) {
        case ND_ADD:
            return eval2(vm, node->lhs, label) + eval(vm, node->rhs);
        case ND_SUB:
            return eval2(vm, node->lhs, label) - eval(vm, node->rhs);
        case ND_MUL:
            return eval(vm, node->lhs) * eval(vm, node->rhs);
        case ND_DIV: {
            int64_t rhs = eval(vm, node->rhs);
            if (rhs == 0)
                error_tok(vm, node->rhs->tok,
                          "division by zero in constant expression");
            if (node->ty->is_unsigned)
                return (uint64_t)eval(vm, node->lhs) / (uint64_t)rhs;
            return eval(vm, node->lhs) / rhs;
        }
        case ND_NEG:
            return -eval(vm, node->lhs);
        case ND_MOD: {
            int64_t rhs = eval(vm, node->rhs);
            if (rhs == 0)
                error_tok(vm, node->rhs->tok,
                          "division by zero in constant expression");
            if (node->ty->is_unsigned)
                return (uint64_t)eval(vm, node->lhs) % (uint64_t)rhs;
            return eval(vm, node->lhs) % rhs;
        }
        case ND_BITAND:
            return eval(vm, node->lhs) & eval(vm, node->rhs);
        case ND_BITOR:
            return eval(vm, node->lhs) | eval(vm, node->rhs);
        case ND_BITXOR:
            return eval(vm, node->lhs) ^ eval(vm, node->rhs);
        case ND_SHL:
            return eval(vm, node->lhs) << eval(vm, node->rhs);
        case ND_SHR:
            if (node->ty->is_unsigned && node->ty->size == 8)
                return (uint64_t)eval(vm, node->lhs) >> eval(vm, node->rhs);
            return eval(vm, node->lhs) >> eval(vm, node->rhs);
        // #1122: EQ/NE/LT/LE produce an `int` result, so the wide
        // interception above (keyed on node->ty) doesn't apply here even
        // when the *operands* are wide -- fold those through
        // __cccc_bitint_cmp instead of a truncating eval(), which could
        // silently compare 64-bit-truncated garbage (e.g. a `<<` by >=64
        // bits is UB in the int64_t path but well-defined in the wide one).
        case ND_EQ:
            if (is_wide_bitint(node->lhs->ty) || is_wide_bitint(node->rhs->ty))
                return eval_wide_cmp(vm, node) == 0;
            return eval(vm, node->lhs) == eval(vm, node->rhs);
        case ND_NE:
            if (is_wide_bitint(node->lhs->ty) || is_wide_bitint(node->rhs->ty))
                return eval_wide_cmp(vm, node) != 0;
            return eval(vm, node->lhs) != eval(vm, node->rhs);
        case ND_LT:
            if (is_wide_bitint(node->lhs->ty) || is_wide_bitint(node->rhs->ty))
                return eval_wide_cmp(vm, node) < 0;
            if (node->lhs->ty->is_unsigned)
                return (uint64_t)eval(vm, node->lhs) < eval(vm, node->rhs);
            return eval(vm, node->lhs) < eval(vm, node->rhs);
        case ND_LE:
            if (is_wide_bitint(node->lhs->ty) || is_wide_bitint(node->rhs->ty))
                return eval_wide_cmp(vm, node) <= 0;
            if (node->lhs->ty->is_unsigned)
                return (uint64_t)eval(vm, node->lhs) <= eval(vm, node->rhs);
            return eval(vm, node->lhs) <= eval(vm, node->rhs);
        case ND_COND:
            return eval(vm, node->cond) ? eval2(vm, node->then, label)
                                        : eval2(vm, node->els, label);
        case ND_COMMA:
            return eval2(vm, node->rhs, label);
        case ND_NOT:
            if (is_wide_bitint(node->lhs->ty))
                return !eval_wide_truthy(vm, node->lhs);
            return !eval(vm, node->lhs);
        case ND_BITNOT:
            return ~eval(vm, node->lhs);
        case ND_LOGAND:
            if (is_wide_bitint(node->lhs->ty) || is_wide_bitint(node->rhs->ty))
                return eval_wide_truthy(vm, node->lhs) &&
                       eval_wide_truthy(vm, node->rhs);
            return eval(vm, node->lhs) && eval(vm, node->rhs);
        case ND_LOGOR:
            if (is_wide_bitint(node->lhs->ty) || is_wide_bitint(node->rhs->ty))
                return eval_wide_truthy(vm, node->lhs) ||
                       eval_wide_truthy(vm, node->rhs);
            return eval(vm, node->lhs) || eval(vm, node->rhs);
        case ND_CAST: {
            // #832: a decimal-to-integer cast (e.g. `(int)1.5dd`, legal in an
            // ICE since 1.5dd is the cast's immediate operand) folds via
            // eval_decimal + cccc_dec_to_int instead of recursing into eval2,
            // which would just hit this function's own is_decimal(node->ty)
            // guard above on the recursive call. `(int)(1.1dd + 2.2dd)` also
            // reaches here and folds -- a GCC-compatible extension beyond
            // strict C, which only permits a floating constant as the cast's
            // *immediate* operand.
            if (is_decimal(node->lhs->ty)) {
                int           w = dec_width_code(node->lhs->ty);
                unsigned char tmp[16];
                eval_decimal(vm, node->lhs, w, tmp);
                long long out = 0;
                if (!cccc_dec_to_int(w, tmp, &out, node->ty->is_unsigned,
                                     CCCC_DEC_ENV_STATIC))
                    error_tok(vm, node->tok,
                              "_Decimal literals require a build with "
                              "CCCC_HAS_DECIMAL=1");
                if (is_integer(node->ty)) {
                    switch (node->ty->size) {
                        case 1:
                            return node->ty->is_unsigned ? (uint8_t)out
                                                         : (int8_t)out;
                        case 2:
                            return node->ty->is_unsigned ? (uint16_t)out
                                                         : (int16_t)out;
                        case 4:
                            return node->ty->is_unsigned ? (uint32_t)out
                                                         : (int32_t)out;
                    }
                }
                return out;
            }
            if (is_flonum(node->lhs->ty) && is_integer(node->ty) &&
                node->ty->is_unsigned && node->ty->size == 8)
                // #780: an unsigned 64-bit destination saturates against
                // [0, 2^64), not the signed [-2^63, 2^63) rule eval2 applies
                // to a bare flonum node above (which doesn't see this cast's
                // destination type once we've recursed past it).
                return (int64_t)cccc_f64_to_u64(eval_double(vm, node->lhs));
            int64_t val = eval2(vm, node->lhs, label);
            if (is_integer(node->ty)) {
                switch (node->ty->size) {
                    case 1:
                        return node->ty->is_unsigned ? (uint8_t)val
                                                     : (int8_t)val;
                    case 2:
                        return node->ty->is_unsigned ? (uint16_t)val
                                                     : (int16_t)val;
                    case 4:
                        return node->ty->is_unsigned ? (uint32_t)val
                                                     : (int32_t)val;
                }
            }
            return val;
        }
        case ND_ADDR:
            return eval_rval(vm, node->lhs, label);
        case ND_LABEL_VAL:
            // #1122: same NULL-`label` hazard as eval_rval's ND_VAR arm --
            // a caller with no relocation channel (eval()/eval_wide_operand's
            // narrow fallback) reaching a label-as-value here used to
            // dereference NULL unconditionally.
            if (!label)
                error_tok(vm, node->tok,
                          "not a compile-time constant (label address)");
            *label = &node->unique_label;
            return 0;
        case ND_MEMBER: {
            Initializer *init = constexpr_init_for_node(node);
            if (init)
                return init->expr ? eval(vm, init->expr) : 0;
        }
            if (!label)
                error_tok(vm, node->tok,
                          "not a compile-time constant (member access)");
            if (node->ty->kind != TY_ARRAY)
                error_tok(vm, node->tok,
                          "invalid initializer (member is not an array)");
            return eval_rval(vm, node->lhs, label) + node->member->offset;
        case ND_VAR:
            if (node->var->is_constexpr) {
                Node *expr = constexpr_expr_for_node(node);
                if (!expr)
                    error_tok(vm, node->tok,
                              "not a scalar compile-time constant");
                return eval(vm, expr);
            }
            if (!label)
                error_tok(vm, node->tok,
                          "not a compile-time constant (variable reference)");
            if (node->var->ty->kind != TY_ARRAY &&
                node->var->ty->kind != TY_FUNC)
                error_tok(vm, node->tok,
                          "invalid initializer (expected address of array or "
                          "function)");
            *label = &node->var->name;
            return 0;
        case ND_NUM:
            return node->val;
        default:
            error_tok(vm, node->tok,
                      "not a compile-time constant (expression)");
            return 0;
    }
}

static int64_t eval_rval(VirtualMachine *vm, Node *node, char ***label) {
    // #1122: a caller with no way to carry a relocation (eval()/eval() via
    // eval_wide_operand's narrow fallback, both of which pass label==NULL)
    // reaching an address-of-global here used to dereference that NULL
    // unconditionally at `*label = ...` below -- e.g. `(int)&global` inside
    // a bitfield initializer, or `(__int128)&global`, both pre-existing
    // crashes (the latter newly reachable through eval_wide as of #1122).
    // Reject with the same diagnostic eval2's own ND_VAR arm uses for a
    // non-constant reference, instead of segfaulting. ND_MEMBER must NOT be
    // included here even though it also writes nothing to `*label` itself
    // -- offsetof(T, m)'s expansion `&(((T*)0)->m)` bottoms out an
    // ND_MEMBER/ND_DEREF chain at a null-pointer constant (see
    // is_offsetof_chain below) and folds fine with label==NULL; erroring on
    // ND_MEMBER here would reject every offsetof() use in a constant
    // expression, not just an actual `&global.member`, which already hits
    // this same check when the recursion bottoms out at ND_VAR.
    if (!label && node->kind == ND_VAR)
        error_tok(vm, node->tok,
                  "not a compile-time constant (address of variable)");
    switch (node->kind) {
        case ND_VAR:
            if (node->var->is_local)
                error_tok(vm, node->tok,
                          "not a compile-time constant (local variable)");
            *label = &node->var->name;
            return 0;
        case ND_DEREF:
            return eval2(vm, node->lhs, label);
        case ND_MEMBER:
            return eval_rval(vm, node->lhs, label) + node->member->offset;
        default:
            error_tok(vm, node->tok, "invalid initializer");
            return 0;
    }
}

// #1042(d): is offsetof(T, member)'s expansion (include/stddef.h:
// `(size_t)&(((type *)0)->member)`, `->` already desugared to `(*x).y` by
// parse_postfix.c) -- a chain of ND_MEMBER/ND_DEREF hops (any depth, for a
// nested member) bottoming out at a null-pointer constant, itself optionally
// wrapped in casts. This is exactly the shape eval_rval()'s ND_MEMBER/
// ND_DEREF arms (above) already fold without needing a `label`, so anything
// this admits is something eval() can actually compute -- deliberately much
// narrower than "whatever eval_rval() accepts" (which also allows `&global`,
// admitting that here would make an address-valued expression pass
// is_const_expr at every other caller, e.g. parse_expr.c/
// static_branch_value, which then call eval() with no label and hard-error).
static bool is_offsetof_null_base(Node *node) {
    while (node->kind == ND_CAST)
        node = node->lhs;
    return node->kind == ND_NUM && node->val == 0;
}

static bool is_offsetof_chain(Node *node) {
    switch (node->kind) {
        case ND_MEMBER:
        case ND_DEREF:
            return is_offsetof_chain(node->lhs);
        case ND_CAST:
            return is_offsetof_chain(node->lhs);
        default:
            return is_offsetof_null_base(node);
    }
}

bool is_const_expr(VirtualMachine *vm, Node *node) {
    add_type(vm, node);

    switch (node->kind) {
        case ND_ADDR:
            return is_offsetof_chain(node->lhs);
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
        case ND_BITAND:
        case ND_BITOR:
        case ND_BITXOR:
        case ND_SHL:
        case ND_SHR:
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
        case ND_LOGAND:
        case ND_LOGOR:
            return is_const_expr(vm, node->lhs) && is_const_expr(vm, node->rhs);
        case ND_COND:
            if (!is_const_expr(vm, node->cond))
                return false;
            return is_const_expr(vm,
                                 eval(vm, node->cond) ? node->then : node->els);
        case ND_COMMA:
            return is_const_expr(vm, node->rhs);
        case ND_NEG:
        case ND_NOT:
        case ND_BITNOT:
        case ND_CAST:
            return is_const_expr(vm, node->lhs);
        case ND_NUM:
            return true;
        case ND_VAR:
        case ND_MEMBER:
            return constexpr_init_for_node(node) != NULL;
        default:
            return false;
    }
}

int64_t const_expr(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node = conditional(vm, rest, tok);
    return eval(vm, node);
}

// #1095: true when `node` is (possibly wrapped in one or more ND_CAST, the
// same tolerance serialize_expr's own recursive tree walk gets for free
// when a sizeof/_Alignof survives as a bare ND_NUM elsewhere in an
// expression, e.g. `(int)sizeof(struct statfs)`) a bare sizeof/_Alignof
// fold of a from_include type -- i.e. an ND_NUM carrying Node.layout_ty
// (set at the four fold sites in src/parse_postfix.c, #1031). A compound
// expression (`sizeof(T) + 1`) does not qualify -- eval() has already
// combined it with something else by the time any of this batch's callers
// would use this, and there is no single Type left to re-materialize
// against. Mirrors #1031's own limitation for the bare-expression path;
// the const_expr()/eval() consumers this feeds (array dimensions, case
// labels, enum values -- see const_expr_layout() below) inherit it rather
// than attempting anything broader.
bool node_layout_const(Node *node, Type **out_ty, bool *out_align) {
    while (node && node->kind == ND_CAST)
        node = node->lhs;
    if (!node || node->kind != ND_NUM || !node->layout_ty)
        return false;
    if (out_ty)
        *out_ty = node->layout_ty;
    if (out_align)
        *out_align = node->layout_is_align;
    return true;
}

// #1095: const_expr()'s own body, but keeping the parsed Node around long
// enough to test node_layout_const() on it before eval() discards it --
// every const_expr()/eval() consumer that wants to re-materialize a
// sizeof/_Alignof of a from_include type (array dimensions, case labels,
// enum values) needs this instead of a plain const_expr() call.
int64_t const_expr_layout(VirtualMachine *vm, Token **rest, Token *tok,
                          Type **out_ty, bool *out_align) {
    Node *node = conditional(vm, rest, tok);
    node_layout_const(node, out_ty, out_align);
    return eval(vm, node);
}

// static_branch_value — decide whether a condition node is a compile-time
// constant or an unsigned tautology, without a full range analysis.
//
// Returns:
//   1  — condition is always true  (then-branch live, else-branch dead)
//   0  — condition is always false (then-branch dead, else-branch live)
//  -1  — unknown / runtime
//
// Tier 1: plain constant fold via is_const_expr / eval.
// Tier 2: unsigned boundary tautologies that arise in _FORTIFY_SOURCE idioms.
//   All relational operators lower to ND_LT / ND_LE at parse time:
//     a > b  →  ND_LT(b, a)
//     a >= b →  ND_LE(b, a)
//   For ND_LT(lhs, rhs) / ND_LE(lhs, rhs) where exactly one side is a
//   compile-time constant C and the other side R is a runtime unsigned type,
//   we check boundary values in the uint64 domain:
//     ND_LT(C, R):  C < R  — always false if C == UMAX  (e.g. SIZE_MAX < len)
//     ND_LT(R, C):  R < C  — always false if C == 0
//     ND_LE(C, R):  C <= R — always true  if C == 0
//     ND_LE(R, C):  R <= C — always true  if C == UMAX
//   where UMAX = (1u << (8 * width)) - 1 (or UINT64_MAX for 8-byte types).
//
// Note: this helper is only called from the `if`-statement parser when
// vm->compiler.saw_diag_attr is set, so normal compiles pay nothing.
int static_branch_value(VirtualMachine *vm, Node *cond) {
    add_type(vm, cond);

    // Tier 1: plain constant fold.
    if (is_const_expr(vm, cond))
        return eval(vm, cond) ? 1 : 0;

    // Tier 2: unsigned tautology on relational ops.
    if (cond->kind != ND_LT && cond->kind != ND_LE)
        return -1;

    Node *lhs = cond->lhs;
    Node *rhs = cond->rhs;
    add_type(vm, lhs);
    add_type(vm, rhs);

    bool lhs_const = is_const_expr(vm, lhs);
    bool rhs_const = is_const_expr(vm, rhs);

    // Need exactly one constant side and one runtime unsigned side.
    if (lhs_const == rhs_const)
        return -1; // both constant (already folded above) or both runtime

    Node   *R        = lhs_const ? rhs : lhs; // runtime operand
    int64_t C_signed = lhs_const ? eval(vm, lhs) : eval(vm, rhs);
    bool    C_is_lhs = lhs_const;

    if (!R->ty || !R->ty->is_unsigned)
        return -1;

    int      width = R->ty->size; // bytes: 1, 2, 4, 8
    uint64_t UMAX =
        (width >= 8) ? UINT64_MAX : ((uint64_t)1 << (8 * width)) - 1;
    uint64_t C = (uint64_t)C_signed;

    if (cond->kind == ND_LT) {
        // Stored as ND_LT(lhs, rhs) meaning lhs < rhs.
        if (C_is_lhs) {
            // C < R: always false when C == UMAX (e.g. SIZE_MAX < len).
            if (C == UMAX)
                return 0;
        } else {
            // R < C: always false when C == 0.
            if (C == 0)
                return 0;
        }
    } else { // ND_LE
        // Stored as ND_LE(lhs, rhs) meaning lhs <= rhs.
        if (C_is_lhs) {
            // C <= R: always true when C == 0.
            if (C == 0)
                return 1;
        } else {
            // R <= C: always true when C == UMAX.
            if (C == UMAX)
                return 1;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// __builtin_object_size static AST walkers
//
// These resolve a pointer expression to (base_size, base_offset, sub_size,
// sub_offset) without emitting any code.  The contract mirrors GCC's
// __builtin_object_size specification:
//   - We only succeed when the base object and all offsets are compile-time
//     constants.  Any non-constant index or unknown base causes bail (return
//     false), which lets the caller fall back to the conservative default.
//   - The argument is never evaluated; no side-effects are emitted.
//
// Pointer-arithmetic offsets in ND_ADD nodes have already been byte-scaled
// by new_add() (see parse.c new_add: rhs *= sizeof(*lhs)), so we use eval()
// on them directly.
// ---------------------------------------------------------------------------

// Resolve the lvalue `node` points to into an ObjSizeInfo describing the
// base object and nearest surrounding subobject.
static bool objsize_resolve_lvalue(VirtualMachine *vm, Node *node,
                                   ObjSizeInfo *r) {
    // #999: unlike this function's siblings (mark_escaping_root,
    // constexpr_init_for_node), nothing upstream NULL-checks before
    // recursing here -- the ND_MEMBER case below passes node->lhs straight
    // through, and struct_ref()'s error-recovery paths can leave that NULL
    // on a member access that already failed to typecheck (a placeholder
    // node with ty = ty_error, no ->lhs). add_type(vm, NULL) itself is
    // safe, but the switch just below is not.
    if (!node)
        return false;
    add_type(vm, node);
    switch (node->kind) {
        case ND_VAR: {
            Type *ty = node->var->ty;
            // VLAs have no compile-time size; bail.
            if (ty->kind == TY_VLA || ty->size <= 0)
                return false;
            r->base_size   = ty->size;
            r->base_offset = 0;
            r->sub_size    = ty->size;
            r->sub_offset  = 0;
            return true;
        }
        case ND_MEMBER: {
            // Resolve the containing aggregate, then step into the member.
            ObjSizeInfo base;
            if (!objsize_resolve_lvalue(vm, node->lhs, &base))
                return false;
            Member *m = node->member;
            if (!m || m->is_bitfield || !m->ty || m->ty->size <= 0)
                return false;
            r->base_size   = base.base_size;
            r->base_offset = base.base_offset + m->offset;
            r->sub_size    = m->ty->size;
            r->sub_offset  = 0;
            return true;
        }
        case ND_DEREF:
            // *ptr  →  resolve ptr as a pointer expression.
            // This is how arr[k] arrives: x[y] lowers to *(x+y).
            return objsize_resolve_ptr(vm, node->lhs, r);
        default:
            return false;
    }
}

// Resolve a pointer-valued `node` into an ObjSizeInfo.
bool objsize_resolve_ptr(VirtualMachine *vm, Node *node, ObjSizeInfo *r) {
    add_type(vm, node);
    switch (node->kind) {
        case ND_CAST:
            // See through casts.
            return objsize_resolve_ptr(vm, node->lhs, r);
        case ND_ADDR:
            // &lvalue → resolve the lvalue.
            return objsize_resolve_lvalue(vm, node->lhs, r);
        case ND_ADD:
        case ND_SUB: {
            // ptr + scaled_int / ptr - scaled_int (rhs is already byte-scaled
            // by new_add()/new_sub()). ND_SUB also covers ptr - ptr (element
            // count, node->ty == ty_long), which must NOT be peeled here --
            // excluded by the node->ty->base check (a ptr-ptr node has no base
            // type).
            if (node->kind == ND_SUB && !(node->ty && node->ty->base))
                return false;
            ObjSizeInfo base;
            if (!objsize_resolve_ptr(vm, node->lhs, &base))
                return false;
            if (!is_const_expr(vm, node->rhs))
                return false;
            int64_t byte_delta = eval(vm, node->rhs);
            if (node->kind == ND_SUB)
                byte_delta = -byte_delta;
            if (byte_delta < INT_MIN || byte_delta > INT_MAX)
                return false; // avoid narrowing overflow below
            int64_t new_base_off = (int64_t)base.base_offset + byte_delta;
            int64_t new_sub_off  = (int64_t)base.sub_offset + byte_delta;
            if (new_base_off < 0 || new_base_off > INT_MAX || new_sub_off < 0 ||
                new_sub_off > INT_MAX)
                return false; // pointer moved before the start of the
                              // (sub)object
            r->base_size   = base.base_size;
            r->base_offset = (int)new_base_off;
            r->sub_size    = base.sub_size;
            r->sub_offset  = (int)new_sub_off;
            return true;
        }
        case ND_VAR:
            // A bare array name decays to a pointer to its first element.
            // The node kind remains ND_VAR with array type.
            if (node->var->ty->kind == TY_ARRAY && node->var->ty->size > 0) {
                r->base_size   = node->var->ty->size;
                r->base_offset = 0;
                r->sub_size    = node->var->ty->size;
                r->sub_offset  = 0;
                return true;
            }
            return false; // pointer variable or unknown → bail
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// #649: attribute-driven allocation-size tracking (generalizes #642).
//
// Recognizes `rhs` (a pointer initializer expression, casts already peeled by
// the caller) as a call to a function declared __attribute__((alloc_size(n)))
// / __attribute__((alloc_size(n,m))), with compile-time constant argument(s)
// at the designated 1-based index/indices, and returns the allocated byte
// count in *out.
//
// The attribute is authoritative: a function is only recognized as an
// allocator if it (or a prior compatible declaration merged onto its Type,
// see inherit_semantic_attrs/declare_function_prototype) carries alloc_size.
// This is deliberately *not* name-based any more -- a user function literally
// named "malloc" with no attribute is correctly left untracked, and any
// annotated custom allocator (arena/pool wrapper) is tracked the same way
// libc's malloc/calloc/realloc/reallocarray/aligned_alloc are (see their
// declarations in include/stdlib.h).
bool objsize_alloc_from_call(VirtualMachine *vm, Node *rhs, int *out) {
    while (rhs && rhs->kind == ND_CAST)
        rhs = rhs->lhs;
    if (!rhs || rhs->kind != ND_FUNCALL || !rhs->lhs ||
        rhs->lhs->kind != ND_VAR)
        return false;
    Obj *fn = rhs->lhs->var;
    if (!fn || !fn->name || !fn->is_function || !fn->ty ||
        fn->ty->kind != TY_FUNC)
        return false;
    int idx1 = fn->ty->alloc_size_idx;
    int idx2 = fn->ty->alloc_size_idx2;
    if (idx1 <= 0)
        return false; // no alloc_size attribute on this declaration

    // Fetch the Nth argument (0-based), or NULL if out of range.
    Node *args[8] = {0};
    int   nargs   = 0;
    for (Node *a = rhs->args; a && nargs < 8; a = a->next)
        args[nargs++] = a;

    if (idx1 > nargs || (idx2 && idx2 > nargs))
        return false; // attribute refers to an argument this call doesn't have
    if (!is_const_expr(vm, args[idx1 - 1]) ||
        (idx2 && !is_const_expr(vm, args[idx2 - 1])))
        return false;

    int64_t size = eval(vm, args[idx1 - 1]);
    if (idx2) {
        // calloc(nmemb, size)-style two-factor form: product of both args.
        int64_t elem = eval(vm, args[idx2 - 1]);
        if (size < 0 || elem < 0)
            return false;
        // Overflow guard: bail rather than fold a wrapped-around size.
        if (elem != 0 && size > (INT64_MAX / elem))
            return false;
        size *= elem;
    }

    if (size < 0 || size > INT_MAX)
        return false; // negative or too large to fit ObjSizeInfo's int fields
    *out = (int)size;
    return true;
}

// #697/#700/#701: peel casts and constant-offset ND_ADD/ND_SUBs off `node`,
// accumulating the total byte delta (rhs is already byte-scaled by
// new_add()/new_sub()), down to a bare ND_VAR. `*out_offset` is the offset of
// `node`'s pointer value *relative to `*out_base`*, and may legitimately be
// negative here -- e.g. `q - 16` where `q` is itself a derived variable
// (`q = p + 64`) peels to base=q, offset=-16, which is only 16 bytes before
// `q`, not before the ultimate allocation (48 bytes into it). Only the final,
// fully-chased offset from the true root (computed by
// objsize_effective_remaining, which knows the whole derivation chain) must
// be non-negative -- this helper just does the syntactic peel and range-checks
// the narrowing to `int`. `p->ty->base` (ND_SUB only) excludes `ptr - ptr`
// (element-count subtraction, node->ty == ty_long) from being misread as a
// pointer offset. Callers additionally check `objsize_has_alloc` on
// *out_base; shared by the #697 inline-interior-pointer builtin-argument case
// and the #700/#701 `q = p +/- const` derived-declaration case.
bool objsize_peel_offset_chain(VirtualMachine *vm, Node *node, Obj **out_base,
                               int *out_offset) {
    Node   *p      = node;
    int64_t offset = 0;
    for (;;) {
        if (p->kind == ND_CAST) {
            p = p->lhs;
        } else if (p->kind == ND_ADD && is_const_expr(vm, p->rhs)) {
            offset += eval(vm, p->rhs);
            p       = p->lhs;
        } else if (p->kind == ND_SUB && is_const_expr(vm, p->rhs)) {
            add_type(vm, p);
            if (!(p->ty && p->ty->base))
                break;
            offset -= eval(vm, p->rhs);
            p       = p->lhs;
        } else {
            break;
        }
    }
    if (offset < INT_MIN || offset > INT_MAX)
        return false;
    if (p->kind != ND_VAR || !p->var)
        return false;
    *out_base   = p->var;
    *out_offset = (int)offset;
    return true;
}

// #642: post-parse pass resolving deferred __builtin_object_size queries on
// malloc-tracked pointers. Must run after the whole function body has been
// parsed (mirrors resolve_goto_labels) so that a reassignment or address-of
// appearing anywhere in the function — including textually after the query,
// e.g. across a loop back-edge — can still poison the query. A pointer's
// allocation size is only trusted when it is assigned exactly once (its
// declaration initializer, exempted via Obj.objsize_init_assign) and never
// has its address taken.
static void objsize_poison_scan(Node *node) {
    // Iterate the `next` chain rather than recursing on it — a function body
    // is a linked list of statements, and recursing here would blow the host
    // stack on long straight-line bodies (unlike ND_COMMA trees elsewhere in
    // the parser, which are deliberately kept balanced for this reason).
    for (; node; node = node->next) {
        switch (node->kind) {
            case ND_ASSIGN:
                // Plain `p = expr` reaches here directly with lhs == the raw
                // ND_VAR. Compound assignment (`p += x`) and `++p`/`p++`/`--p`/
                // `p--` are desugared by to_assign() into `tmp = &p, *tmp =
                // *tmp op rhs`, which is caught by the ND_ADDR case below
                // instead.
                if (node->lhs && node->lhs->kind == ND_VAR && node->lhs->var &&
                    node->lhs->var->objsize_has_alloc &&
                    node->lhs->var->objsize_init_assign != node)
                    node->lhs->var->objsize_unsafe = true;
                break;
            case ND_ADDR:
                if (node->lhs && node->lhs->kind == ND_VAR && node->lhs->var &&
                    node->lhs->var->objsize_has_alloc)
                    node->lhs->var->objsize_unsafe = true;
                break;
            default:
                break;
        }
        objsize_poison_scan(node->lhs);
        objsize_poison_scan(node->rhs);
        objsize_poison_scan(node->cond);
        objsize_poison_scan(node->then);
        objsize_poison_scan(node->els);
        objsize_poison_scan(node->init);
        objsize_poison_scan(node->inc);
        objsize_poison_scan(node->body);
        for (Node *a = node->args; a; a = a->next)
            objsize_poison_scan(a);
    }
}

// #676/#1008: walk an addressed-lvalue chain rooted at an explicit `&expr`,
// finding the local Obj whose own frame-relative storage the resulting
// LEA3 actually materializes, and mark it addr_taken (always) and
// addr_escapes (only when `escaping` is true). Interior-aware: &arr[i] and
// &s.field descend through the runtime-offset / member chain to arr / s,
// the variable whose *base* LEA3 is what op_LEA3_fn actually tags in
// stack_ptr_epochs (see #675's interior resolution, which depends on that
// base still being recorded whenever it escapes). Purely additive: if the
// chain bottoms out in something other than a plain local var (e.g. a
// global, or an address computed off an unrelated pointer parameter),
// there's no Obj to mark here and we simply do nothing -- safe, since no
// LEA3-of-this-Obj recording decision hinges on it.
static void mark_root_var(Node *n, bool escaping) {
    while (n) {
        switch (n->kind) {
            case ND_VAR:
                if (n->var) {
                    n->var->addr_taken = true;
                    if (escaping)
                        n->var->addr_escapes = true;
                }
                return;
            case ND_MEMBER:
            case ND_CAST:
            case ND_DEREF:
                n = n->lhs;
                continue;
            case ND_ADD:
            case ND_SUB:
                if (n->lhs && n->lhs->ty &&
                    (n->lhs->ty->kind == TY_PTR ||
                     n->lhs->ty->kind == TY_ARRAY)) {
                    n = n->lhs;
                } else if (n->rhs && n->rhs->ty &&
                           (n->rhs->ty->kind == TY_PTR ||
                            n->rhs->ty->kind == TY_ARRAY)) {
                    n = n->rhs;
                } else {
                    return;
                }
                continue;
            default:
                return;
        }
    }
}

// #676: thin wrapper over mark_root_var for the existing escaping call
// sites in find_and_mark_escaping_addr() below -- unchanged behaviour.
static void mark_escaping_root(Node *n) {
    mark_root_var(n, true);
}

// Walk value-transparent wrappers (cast, comma, ternary branches, chained
// assignment) from an escaping sink's operand down to the actual `&expr`
// node it carries, if any, and mark its root var escaping. Any other node
// kind reached along the way (arithmetic on the resulting pointer, a plain
// non-address expression, etc.) simply isn't a literal &-chain -- there is
// nothing to mark, so we stop. This deliberately only recognizes the common
// wrapper shapes; an `&expr` that reaches an escaping sink through some
// other wrapper this doesn't unwrap will be under-marked (left recorded
// only if some OTHER occurrence of the same var already marked it) --
// no false positive risk (see #669), but if this turns out to miss real
// code, extend the wrapper list here.
static void find_and_mark_escaping_addr(Node *n) {
    while (n) {
        switch (n->kind) {
            case ND_CAST:
                n = n->lhs;
                continue;
            case ND_COMMA:
            case ND_ASSIGN: // the *value* of `a = b` (or the tail of a comma)
                            // is its rhs
                n = n->rhs;
                continue;
            case ND_COND: // ternary: both arms can be the value actually passed
                          // on
                find_and_mark_escaping_addr(n->then);
                find_and_mark_escaping_addr(n->els);
                return;
            case ND_ADDR:
                mark_escaping_root(n->lhs);
                return;
            case ND_ADD:
            case ND_SUB:
                // #718: pointer arithmetic on a frame-local base (`buf + i`) is
                // itself an address -- an array's implicit decay already needs
                // no explicit `&`, and offsetting that decayed pointer doesn't
                // change what it points into. mark_escaping_root already knows
                // how to walk ADD/SUB to find the base; reuse it directly
                // instead of falling through to "not an address" below.
                if (n->ty && n->ty->kind == TY_PTR)
                    mark_escaping_root(n);
                return;
            default:
                // Arrays (always) and structs/unions (conservatively -- some
                // ABI paths copy them, but gen_addr's shared local-var funnel
                // can't tell at emit time) decay to their own base address with
                // no explicit `&` in the source at all. Treat one reaching an
                // escaping sink the same as an explicit &-chain rooted at the
                // same base, via the same interior-aware walk.
                if (n->ty &&
                    (n->ty->kind == TY_ARRAY || n->ty->kind == TY_STRUCT ||
                     n->ty->kind == TY_UNION))
                    mark_escaping_root(n);
                return;
        }
    }
}

// #676: post-parse pass marking which locals' addresses provably escape
// their creating frame (call argument, return operand, or stored into a
// pointer/aggregate lvalue), so op_LEA3_fn (#673) can skip recording
// vm->stack_ptr_epochs entries for addresses that never leave their own
// frame -- the overwhelming majority of `&local` sites in ordinary code
// (loop counters, in-place accumulators). Mirrors objsize_poison_scan's
// structure: iterate the statement-chain via `next`, recurse into every
// expression-tree field. Default (addr_escapes left false, from Obj's
// zero-init) is "record" -- this pass only ever adds escaping marks, never
// removes the safe default, so a pattern this scan doesn't recognize is
// merely a missed pruning opportunity, not a #673 regression.
void mark_addr_escapes(Node *node) {
    for (; node; node = node->next) {
        switch (node->kind) {
            case ND_FUNCALL:
                for (Node *a = node->args; a; a = a->next)
                    find_and_mark_escaping_addr(a);
                break;
            case ND_RETURN:
                if (node->lhs)
                    find_and_mark_escaping_addr(node->lhs);
                break;
            case ND_ASSIGN:
                // Escaping iff the destination is a pointer (or aggregate --
                // e.g. an array of pointers/structs-of-pointers via a member
                // store) lvalue; a store into a plain scalar can't retain an
                // address at all.
                if (node->lhs && node->lhs->ty &&
                    (node->lhs->ty->kind == TY_PTR ||
                     node->lhs->ty->kind == TY_ARRAY ||
                     node->lhs->ty->kind == TY_STRUCT ||
                     node->lhs->ty->kind == TY_UNION))
                    find_and_mark_escaping_addr(node->rhs);
                break;
            case ND_ADDR:
                // #1008: every explicit `&expr`, not just ones reaching a
                // proven-escaping sink, marks its root var addr_taken (but not
                // addr_escapes -- that's still decided by the cases above).
                // Consumed by codegen_expr.c's CHKI guard to stop precisely
                // tracking initialization for a local once its address is
                // taken, since a write through that address bypasses the
                // syntactic-assignment MARKI tracking relies on.
                if (node->lhs)
                    mark_root_var(node->lhs, false);
                break;
            default:
                break;
        }
        mark_addr_escapes(node->lhs);
        mark_addr_escapes(node->rhs);
        mark_addr_escapes(node->cond);
        mark_addr_escapes(node->then);
        mark_addr_escapes(node->els);
        mark_addr_escapes(node->init);
        mark_addr_escapes(node->inc);
        mark_addr_escapes(node->body);
        for (Node *a = node->args; a; a = a->next)
            mark_addr_escapes(a);
    }
}

// #836: does `var` belong to `fn`'s own locals list? Used by
// mark_nested_captures to tell "this function's own local" apart from
// "reaches an enclosing frame through the static link".
static bool var_in_fn_locals(Obj *fn, Obj *var) {
    for (Obj *v = fn->locals; v; v = v->next)
        if (v == var)
            return true;
    return false;
}

// #836: mark_addr_escapes/collect_captures_in_node's counterpart for GNU
// nested functions. A nested function's body can read/write an enclosing
// function's local directly (through the static-link chain and that local's
// stack slot) without ever taking its address -- collect_captures_in_node
// only marks is_captured for Apple block literals, so a plain nested
// function's captures went unmarked and prepare_local_promotion /
// prepare_fp_local_promotion (src/codegen.c) were free to hold such a local
// in a saved register while the nested function kept mutating the stack
// slot behind its back (#836). Any is_local ND_VAR reached from `fn`'s body
// that is not in `fn`'s own locals list must belong to some enclosing
// frame -- depth-agnostic, so a multi-level nest (main -> mid -> inner) is
// covered without walking the parent chain explicitly. Mirrors the child set
// walked by collect_promotion_candidates (src/codegen.c) so no path holding a
// captured reference is missed.
void mark_nested_captures(Obj *fn, Node *node) {
    for (; node; node = node->next) {
        if (node->kind == ND_VAR && node->var && node->var->is_local &&
            !var_in_fn_locals(fn, node->var))
            node->var->is_captured = true;

        mark_nested_captures(fn, node->lhs);
        mark_nested_captures(fn, node->rhs);
        mark_nested_captures(fn, node->cond);
        mark_nested_captures(fn, node->then);
        mark_nested_captures(fn, node->els);
        mark_nested_captures(fn, node->init);
        mark_nested_captures(fn, node->inc);
        mark_nested_captures(fn, node->body);
        mark_nested_captures(fn, node->cas_addr);
        mark_nested_captures(fn, node->cas_old);
        mark_nested_captures(fn, node->cas_new);
        for (Node *a = node->args; a; a = a->next)
            mark_nested_captures(fn, a);
    }
}

// #700/#701: resolve `var`'s effective remaining byte count `offset` bytes
// into its tracked allocation, following the objsize_derived_from chain (`q =
// p +/- k` initializers, possibly nested). Returns -1 if `var` itself is
// unsafe, or any ancestor in the chain is unsafe -- a reassignment/address-of
// anywhere in the chain must poison every var derived from it, since the
// derived var's tracked size is only sound if every ancestor held its
// originally-assigned allocation for the whole function (single-assignment,
// never address-taken; see objsize_poison_scan). `depth` bounds recursion;
// the chain is only ever as deep as nested `q = p +/- k` initializers, so
// this is just a defensive cap, not expected to be hit.
//
// `offset` is accumulated *downward* toward the root at each step (rather
// than composed back up via subtraction) so that a negative intermediate
// offset -- e.g. `q - 16` where `q = p + 64` peels to offset -16 relative to
// `q` itself, but +48 relative to the true root -- cannot be mistaken for a
// before-the-object address until the *total*, root-relative offset is known.
// Composing via subtraction at each level instead (the #700 original scheme)
// is unsound here: it only ever range-checks the final `rem`, and a
// sufficiently negative root-relative offset makes `rem` come out larger
// than the object (e.g. `root_size - (-36) = root_size + 36`), which reads as
// a *valid*, oversized remaining count instead of the conservative fallback
// a before-the-object pointer must get.
static int64_t objsize_effective_remaining(Obj *var, int64_t offset,
                                           int depth) {
    if (!var || var->objsize_unsafe || depth > 64)
        return -1;
    if (var->objsize_derived_from)
        return objsize_effective_remaining(
            var->objsize_derived_from,
            (int64_t)var->objsize_derived_offset + offset, depth + 1);
    // `var` is the true root of the chain: `offset` is now the total,
    // root-relative byte offset. Reject a before-the-object pointer here,
    // where the check is finally meaningful.
    if (offset < 0)
        return -1;
    // #701: a root can be a statically-sized array base (rather than a
    // malloc-family allocation) once the derived-tracking site below also
    // accepts an array Obj as a base.
    int64_t root_size = (var->ty && var->ty->kind == TY_ARRAY &&
                         var->ty->size > 0 && !var->objsize_has_alloc)
                            ? (int64_t)var->ty->size
                            : (int64_t)var->objsize_alloc;
    return root_size - offset;
}

void resolve_objsize_queries(VirtualMachine *vm, Node *body) {
    // Always scan, even when *this* function has no pending queries of its
    // own: a nested function or block can reassign / take the address of a
    // pointer tracked by an *enclosing* function (captured variables share
    // the same Obj — see codegen's static-link walk), and that poisoning
    // must land on the shared Obj before the enclosing function's own
    // resolve_objsize_queries call reads objsize_unsafe.
    objsize_poison_scan(body);
    for (struct ObjSizeQuery *q = vm->compiler.objsize_queries; q;
         q                      = q->next) {
        // #697/#700: subtract the interior-pointer offset (0 for a bare
        // tracked var) and follow any derived-from chain. Past-the-end or
        // unsafe (rem <= 0) leaves the node's pre-set conservative fallback
        // untouched, rather than clamping to 0 like the statically-known
        // array path (OBJSZ_REMAINING) does -- this matches the ticket's
        // requested behavior for heap interior pointers specifically.
        int64_t rem = objsize_effective_remaining(q->var, q->offset, 0);
        if (rem > 0)
            q->node->val = rem;
    }
    vm->compiler.objsize_queries = NULL;
}

// Returns true when `expr` is an integer constant expression whose value fits
// within the range of `to` without truncation.  Used to suppress -Wconversion
// false positives such as `char c = 0;` or `char c = 1 + 1;`.
bool node_int_const_fits(VirtualMachine *vm, Node *expr, Type *to) {
    if (!expr || !to || !is_integer(to))
        return false;
    if (!is_const_expr(vm, expr))
        return false;
    int64_t val = eval(vm, expr);
    if (to->is_unsigned) {
        // Unsigned destination: value must be in [0, 2^bits-1].
        uint64_t max =
            (to->size >= 8) ? UINT64_MAX : ((uint64_t)1 << (to->size * 8)) - 1;
        return (uint64_t)val <= max;
    } else {
        // Signed destination: value must be in [-(2^(bits-1)), 2^(bits-1)-1].
        int64_t min = (to->size >= 8)
                          ? INT64_MIN
                          : -(int64_t)((uint64_t)1 << (to->size * 8 - 1));
        int64_t max = (to->size >= 8)
                          ? INT64_MAX
                          : (int64_t)(((uint64_t)1 << (to->size * 8 - 1)) - 1);
        return val >= min && val <= max;
    }
}

#undef MAX_FMT_ARGS

// ---------------------------------------------------------------------
// Flow-sensitive nonnull tracking (#679, follow-up to #655; loop/switch
// precision from #689, follow-up to #687)
//
// validate_nonnull_call() below only catches literal/constant-folded null
// arguments. This post-parse pass extends that with a forward dataflow walk
// over each function body that tracks simple local pointer null-state
// through the function, catching the case named in #679:
//
//     int *p = 0;
//     f(p);          // p flows from a null initializer -- now warns
//
// Deliberately conservative to avoid false positives (matches #655's design
// and GCC/Clang -Wnonnull): under plain -Wnonnull only *provably*-null
// values warn. A pointer that is merely *maybe* null after a branch --
//     int *p = 0; if (cond) p = &x; f(p);
// -- warns separately under the opt-in -Wmaybe-nonnull (#687), which has a
// higher false-positive risk on real code and so is never folded into -Wall
// or -Wextra. No interprocedural analysis beyond the whole-TU may-return-null
// summary below (#688).
//
// Implementation: at ND_IF/ND_COND, short-circuit &&/||, ND_FOR/ND_DO, and
// ND_SWITCH the env is cloned per live branch/iteration/case and *merged*
// back at the join point (nn_join(), below) -- real dataflow, not a barrier.
// Loops use a bounded back-edge fixpoint (NN_LOOP_ITER_CAP iterations) with
// break/continue envs accumulated per loop via a jump-target stack
// (NNJumpTarget) and joined in at the right point (loop exit / loop header,
// respectively); ND_DO additionally excludes the zero-trip path from its
// exit env, since a do-while body always runs at least once. ND_SWITCH does
// a single pass with no fixpoint: each ND_CASE joins the switch's entry env
// into the fall-through env, and break envs are joined into the switch's
// exit env (plus the entry env itself, if there's no `default`, since the
// whole switch is then skippable). Constructs the fixpoint/join scheme can't
// safely model -- a user goto/label anywhere inside (a merge point from
// unknown predecessors), a computed goto, or a case label reachable from a
// loop body without an intervening switch of its own (Duff's device) -- fall
// back to the original coarse "barrier" scheme (nn_collect_assigned(), still
// below): every local assigned anywhere inside the construct is reset to
// UNKNOWN before the body walk, and the body env is discarded on exit. Also
// falls back to the barrier on non-convergence within the iteration cap, or
// on exhausting the per-function env pool or node-visit budget (NNCtx) --
// all of which only ever *lose* a warning, never manufacture one, so the
// pass stays sound (never a plain-`-Wnonnull` false positive) regardless of
// which path a given construct takes.
//
// Applied unconditionally under either -Wnonnull or -Wmaybe-nonnull (not
// gated behind the latter): NN_NULL, the only fact plain -Wnonnull consumes,
// means "null on every live path" -- nn_join() demotes anything conditional
// to NN_MAYBE -- so precise merging can only make the NN_NULL set *more*
// correct, never introduce a new one. Gating it would make -Wnonnull's own
// output depend on whether -Wmaybe-nonnull was also passed, which is a
// user-visible surprise the whole-TU summary pass avoids by construction
// (its only fact is NN_MAYBE-only, so plain -Wnonnull is untouched either
// way -- not true here, since loop/switch merging changes the NN_NULL set
// too).
//
// A label resets the whole env (a goto target is a merge point from unknown
// predecessors). Locals whose address has escaped (Obj->addr_escapes, set by
// the mark_addr_escapes() pass that already ran) or which are
// volatile-qualified are never tracked, since a reassignment through an
// escaped alias, or an asynchronous change to a volatile pointer, is
// invisible to this walk.
typedef enum { NN_UNKNOWN, NN_NULL, NN_NONNULL, NN_MAYBE } NNState;

typedef struct {
    Obj    *var;
    NNState state;
} NNEnvEntry;

// Locals lists are short; a linear-scan array keeps this pass simple (same
// spirit as restrict_derived_walk()'s DerivedCand[] scratch array).
#define NN_MAX_TRACKED 256

typedef struct {
    NNEnvEntry entries[NN_MAX_TRACKED];
    int        count;
    bool       live; // false == BOTTOM: this program point is unreachable
                     // (e.g. right after a return/break/continue). A join
                     // with a bottom env yields exactly the other side.
    bool overflow;   // an nn_env_slot() append was dropped somewhere in this
                     // env's history -- purely informational; overflow itself
                     // never causes a false positive (see nn_env_slot), but
                     // callers building a fixpoint treat it as "stop trying
                     // to converge precisely" since a dropped var can make
                     // equality checks spuriously agree.
} NNEnv;

static bool nn_trackable(Obj *v) {
    return v && v->is_local && !v->is_param && !v->addr_escapes && v->ty &&
           v->ty->kind == TY_PTR && !v->ty->is_volatile;
}

static NNState nn_env_get(NNEnv *env, Obj *v) {
    for (int i = 0; i < env->count; i++)
        if (env->entries[i].var == v)
            return env->entries[i].state;
    return NN_UNKNOWN;
}

static bool nn_env_has(const NNEnv *env, Obj *v) {
    for (int i = 0; i < env->count; i++)
        if (env->entries[i].var == v)
            return true;
    return false;
}

// Returns NULL on scratch-array overflow -- callers must treat that as "stop
// tracking this var", which only means a missed warning, never a false one.
static NNState *nn_env_slot(NNEnv *env, Obj *v) {
    for (int i = 0; i < env->count; i++)
        if (env->entries[i].var == v)
            return &env->entries[i].state;
    if (env->count < NN_MAX_TRACKED) {
        env->entries[env->count].var   = v;
        env->entries[env->count].state = NN_UNKNOWN;
        return &env->entries[env->count++].state;
    }
    env->overflow = true;
    return NULL;
}

static Node *nn_strip_cast(Node *node) {
    while (node && node->kind == ND_CAST)
        node = node->lhs;
    return node;
}

static NNState nn_state_of_expr(VirtualMachine *vm, NNEnv *env, Node *node) {
    node = nn_strip_cast(node);
    if (!node)
        return NN_UNKNOWN;
    add_type(vm, node);
    if (node->kind == ND_ADDR)
        return NN_NONNULL;
    if (is_const_expr(vm, node))
        return eval(vm, node) == 0 ? NN_NULL : NN_NONNULL;
    if (node->kind == ND_VAR && nn_trackable(node->var))
        return nn_env_get(env, node->var);
    // #688/#692: a direct call to a function flagged by the whole-TU
    // may-return-null summary (check_may_return_null_summaries()) is "maybe
    // null" evidence -- unless the summary also proved the callee returns
    // null on *every* path (always_returns_null), in which case it's
    // definite, mirroring the intra-function NN_NULL vs NN_MAYBE distinction
    // #687 makes for local variables. An indirect call (through a function
    // pointer, node->lhs->kind != ND_VAR) has no known callee and stays
    // NN_UNKNOWN, matching the "unknown callee => no evidence" rule.
    if (node->kind == ND_FUNCALL && node->lhs && node->lhs->kind == ND_VAR &&
        node->lhs->var) {
        if (node->lhs->var->always_returns_null)
            return NN_NULL;
        if (node->lhs->var->may_return_null)
            return NN_MAYBE;
    }
    return NN_UNKNOWN;
}

// Join two null-states at a control-flow merge point (branch/short-circuit
// join, #687). NN_MAYBE means "definitely null on at least one live
// predecessor path, and not definitely null on all of them" -- it is the
// only state -Wmaybe-nonnull warns on; NN_NULL still means "definitely null
// on every live path" and keeps warning under plain -Wnonnull.
//
//   X       ⊔ X       -> X        (both paths agree)
//   NULL    ⊔ NONNULL -> MAYBE    (definitely null on one path only)
//   MAYBE   ⊔ anything -> MAYBE   (already conditional, stays conditional)
//   UNKNOWN ⊔ NULL     -> MAYBE   (one path proves null; dead paths already
//                                  pruned by static_branch_value, so this
//                                  is real conditional-null evidence)
//   UNKNOWN ⊔ NONNULL  -> UNKNOWN (no null evidence from either path)
static NNState nn_join(NNState a, NNState b) {
    if (a == b)
        return a;
    if (a == NN_MAYBE || b == NN_MAYBE)
        return NN_MAYBE;
    if (a == NN_NULL || b == NN_NULL)
        return NN_MAYBE;
    return NN_UNKNOWN;
}

// Barrier: collect every trackable local assigned anywhere in `node`'s
// subtree and reset it to UNKNOWN in `env`. Used at the exit of an
// if/loop/switch construct the precise scheme below can't (or couldn't
// safely) handle, so a conditionally-taken write can't leave a stale (and
// possibly wrong) null-state behind for the fall-through path.
static void nn_collect_assigned(Node *node, NNEnv *env) {
    for (; node; node = node->next) {
        if (node->kind == ND_ASSIGN && node->lhs && node->lhs->kind == ND_VAR &&
            nn_trackable(node->lhs->var)) {
            NNState *slot = nn_env_slot(env, node->lhs->var);
            if (slot)
                *slot = NN_UNKNOWN;
        }
        nn_collect_assigned(node->lhs, env);
        nn_collect_assigned(node->rhs, env);
        nn_collect_assigned(node->cond, env);
        nn_collect_assigned(node->then, env);
        nn_collect_assigned(node->els, env);
        nn_collect_assigned(node->init, env);
        nn_collect_assigned(node->inc, env);
        nn_collect_assigned(node->body, env);
        for (Node *a = node->args; a; a = a->next)
            nn_collect_assigned(a, env);
    }
}

// Per-function context for the walk. Bundled into one struct (rather than
// threading several extra parameters through nn_walk/nn_walk_branch/etc)
// since `quiet`, the node-visit budget, the jump-target stack, and the env
// pool all need to reach the same call sites.
typedef struct NNJumpTarget NNJumpTarget;

// #689: a bounded pool of NNEnv scratch buffers, allocated in chunks from
// the function's own parser_arena entry (never freed individually -- the
// whole arena is bump-only -- but reused via nn_env_alloc/nn_env_release's
// stack discipline within one check_nonnull_flow() call). Each NNEnv is
// several KB (256 tracked-var slots), and a loop fixpoint needs roughly a
// dozen live at once per nesting level on top of nn_walk_branch's own three
// per `if` level, so keeping them off the C stack matters once loops are
// modeled precisely. Chunked (rather than one large up-front block) so a
// function with no loops/switches never pays for the pool at all.
#define NN_ENV_CHUNK 8
#define NN_ENV_MAX_CHUNKS                                                      \
    12 // 96 envs total; exhaustion is a bail-out, never a crash

typedef struct {
    VirtualMachine *vm;
    Obj            *fn;
    bool            quiet; // fixpoint iteration in progress: the two nn_check_*
                           // choke points below emit nothing while this is set
    bool exhausted;       // node-visit budget spent: nn_walk degrades to a safe
                          // give-up (UNKNOWN/live) rather than keep walking
    long          budget; // node-visit budget for the whole function
    NNJumpTarget *jumps;  // innermost-first break/continue/switch-entry stack
    NNEnv        *chunks[NN_ENV_MAX_CHUNKS];
    int           n_chunks;
    int           pool_top;
} NNCtx;

// One frame per loop or switch currently being analysed *precisely* (a
// construct running in barrier mode pushes no frame at all -- see the
// ND_GOTO handling in nn_walk for why an unmatched break/continue is exactly
// the signal that its target is such a construct). `cont_label` is NULL for
// a switch frame (a switch never redefines the continue target -- `continue`
// inside a switch continues the nearest enclosing *loop*, found by walking
// past this frame). `switch_entry` is non-NULL only for a switch frame.
struct NNJumpTarget {
    char  *brk_label;
    char  *cont_label;
    NNEnv *brk_env;      // accumulated join of every break's env this pass
    NNEnv *cont_env;     // accumulated join of every continue's env
    NNEnv *switch_entry; // the env on entry to the switch (NULL for a loop)
    NNJumpTarget *parent;
};

static NNEnv *nn_env_alloc(NNCtx *ctx) {
    int chunk_idx = ctx->pool_top / NN_ENV_CHUNK;
    int slot_idx  = ctx->pool_top % NN_ENV_CHUNK;
    if (chunk_idx >= NN_ENV_MAX_CHUNKS)
        return NULL;
    if (chunk_idx >= ctx->n_chunks) {
        ctx->chunks[chunk_idx] = arena_alloc(&ctx->vm->compiler.parser_arena,
                                             sizeof(NNEnv) * NN_ENV_CHUNK);
        ctx->n_chunks          = chunk_idx + 1;
    }
    NNEnv *e    = &ctx->chunks[chunk_idx][slot_idx];
    e->count    = 0;
    e->live     = true;
    e->overflow = false;
    ctx->pool_top++;
    return e;
}

// Stack-discipline release: rewind to a mark captured before a construct's
// own allocations, making every slot from there on reusable by the next
// sibling construct. Never actually frees arena memory (the arena doesn't
// support that) -- just moves the pool's own free-list pointer back.
static void nn_env_release(NNCtx *ctx, int mark) {
    ctx->pool_top = mark;
}

// Node-visit / env-operation budget, spent by nn_walk (per node) and by the
// O(env-size) operations below (join, equality). Nested loops multiply work
// by up to NN_LOOP_ITER_CAP+1 per level, so this -- not the per-loop
// iteration cap alone -- is what actually bounds the cost of deep nesting;
// once spent, nn_walk degrades to a safe give-up everywhere, never a crash
// or a hang.
#define NN_WALK_BUDGET 200000

static void nn_charge(NNCtx *ctx, long cost) {
    ctx->budget -= cost;
    if (ctx->budget <= 0)
        ctx->exhausted = true;
}

// Pointwise join of two full branch-end envs into `dst` (#687, extended by
// #689 to handle BOTTOM): dst[v] := join(a[v], b[v]) for every var known to
// either side, where "known to a side" that never touched v (absent from its
// entries) reads as whatever that side's snapshot already had for v -- which
// for `a`/`b` produced by `NNEnv x = *env; nn_walk(..., &x)` is exactly the
// pre-branch value, since the struct copy starts with every entry `env`
// already had. If either side is BOTTOM (unreachable -- e.g. the env right
// after a return/break/continue, or right after a switch case that was only
// ever reached by an unconditional break), the join is just the other side,
// which is what makes break/continue/return composition correct: a
// mid-construct exit's env genuinely never reaches this join point, so it
// must not weaken (or strengthen) whatever the *other* path already proved.
// dst may safely alias `a` (and thus `env`, the common caller pattern): each
// var is looked up in `a`/`b` before writing dst, and in the dst==a case the
// in-place overwrite of index i only happens after that index's value has
// already been read for the join, so no iteration reads a value another
// iteration has already clobbered.
static void nn_env_join_into(NNCtx *ctx, NNEnv *dst, const NNEnv *a,
                             const NNEnv *b) {
    nn_charge(ctx, a->count + b->count);
    if (!a->live && !b->live) {
        dst->count    = 0;
        dst->live     = false;
        dst->overflow = a->overflow || b->overflow;
        return;
    }
    if (!a->live) {
        if (dst != a)
            *dst = *b;
        else
            *dst = *b; // dst==a is impossible when a is bottom and b isn't
        return;
    }
    if (!b->live) {
        *dst = *a;
        return;
    }
    bool overflow = a->overflow || b->overflow;
    for (int i = 0; i < a->count; i++) {
        Obj    *v = a->entries[i].var;
        NNState joined =
            nn_join(a->entries[i].state, nn_env_get((NNEnv *)b, v));
        NNState *slot = nn_env_slot(dst, v);
        if (slot)
            *slot = joined; // overflow: stop tracking, never a false positive
    }
    // Vars known only to `b` (never present in `a`, so the loop above never
    // wrote them): join against NN_UNKNOWN, the implicit `a`-side state.
    for (int i = 0; i < b->count; i++) {
        Obj *v = b->entries[i].var;
        if (nn_env_has(a, v))
            continue;
        NNState *slot = nn_env_slot(dst, v);
        if (slot)
            *slot = nn_join(NN_UNKNOWN, b->entries[i].state);
    }
    dst->live     = true;
    dst->overflow = overflow || dst->overflow;
}

// Order-insensitive equality of two envs, used to detect loop-fixpoint
// convergence. Absent-from-entries reads as NN_UNKNOWN on both sides (same
// convention as nn_env_get), so two envs that differ only in which vars
// happen to have an explicit NN_UNKNOWN entry still compare equal.
static bool nn_env_equal(NNCtx *ctx, const NNEnv *a, const NNEnv *b) {
    nn_charge(ctx, a->count + b->count);
    if (a->live != b->live)
        return false;
    if (!a->live)
        return true; // both bottom
    for (int i = 0; i < a->count; i++)
        if (nn_env_get((NNEnv *)b, a->entries[i].var) != a->entries[i].state)
            return false;
    for (int i = 0; i < b->count; i++) {
        Obj *v = b->entries[i].var;
        if (nn_env_has(a, v))
            continue; // already compared above
        if (b->entries[i].state != NN_UNKNOWN)
            return false;
    }
    return true;
}

static void nn_check_call_args(NNCtx *ctx, NNEnv *env, Node *call) {
    if (ctx->quiet || !env->live)
        return;
    VirtualMachine *vm      = ctx->vm;
    Type           *func_ty = call->func_ty;
    if (!func_ty || (!func_ty->nonnull_all && !func_ty->nonnull_mask))
        return;

    Node *arg      = call->args;
    Type *param_ty = func_ty->params;
    int   idx      = 1;
    while (arg && param_ty) {
        bool marked =
            func_ty->nonnull_all
                ? (param_ty->kind == TY_PTR)
                : (idx <= 64 && (func_ty->nonnull_mask & (1ULL << (idx - 1))));
        // Skip const-null args entirely -- validate_nonnull_call() already
        // warned on those at parse time; don't double-warn.
        Node *stripped = nn_strip_cast(arg);
        // #690: route through nn_state_of_expr() rather than only handling
        // ND_VAR directly, so a call used inline as the argument
        // (handle(maybe_null())) also picks up the ND_FUNCALL case #688 added
        // there -- ND_VAR/non-trackable behaviour is unchanged since
        // nn_state_of_expr() falls back to nn_env_get()/NN_UNKNOWN the same
        // way.
        if (marked && stripped && !is_const_expr(vm, stripped)) {
            NNState state = nn_state_of_expr(vm, env, arg);
            if (state == NN_NULL && (vm->compiler.warnings & CCCC_WARN_NONNULL))
                warn_tok(vm, arg->tok, CCCC_WARN_NONNULL,
                         "null value passed to a parameter marked nonnull "
                         "(parameter %d)",
                         idx);
            else if (state == NN_MAYBE &&
                     (vm->compiler.warnings & CCCC_WARN_MAYBE_NONNULL))
                warn_tok(vm, arg->tok, CCCC_WARN_MAYBE_NONNULL,
                         "argument may be null when passed to a parameter "
                         "marked nonnull (parameter %d)",
                         idx);
        }
        arg      = arg->next;
        param_ty = param_ty->next;
        idx++;
    }
}

static void nn_check_return(NNCtx *ctx, NNEnv *env, Node *ret_expr) {
    if (ctx->quiet || !env->live)
        return;
    VirtualMachine *vm = ctx->vm;
    Obj            *fn = ctx->fn;
    if (!fn->ty->returns_nonnull)
        return;
    Node *stripped = nn_strip_cast(ret_expr);
    if (!stripped || is_const_expr(vm, stripped))
        return;
    // #690: same nn_state_of_expr() routing as nn_check_call_args() above, so
    // `return maybe_null();` (a bare ND_FUNCALL return expression) is also
    // covered, not just `int *p = maybe_null(); return p;`.
    NNState state = nn_state_of_expr(vm, env, ret_expr);
    if (state == NN_NULL && (vm->compiler.warnings & CCCC_WARN_NONNULL))
        warn_tok(vm, ret_expr->tok, CCCC_WARN_NONNULL,
                 "null value returned from function declared with "
                 "'returns_nonnull'");
    else if (state == NN_MAYBE &&
             (vm->compiler.warnings & CCCC_WARN_MAYBE_NONNULL))
        warn_tok(vm, ret_expr->tok, CCCC_WARN_MAYBE_NONNULL,
                 "value may be null when returned from function declared with "
                 "'returns_nonnull'");
}

// True if `node`'s subtree contains nothing nn_walk's precise loop/switch
// handling can't safely model: no user goto/label (a jump target from
// unknown predecessors), no computed goto, and -- when scanning a *loop*
// body (`in_switch` starts false) -- no case label reachable without first
// passing through a nested switch of its own (Duff's device: a case label
// belonging to an *enclosing* switch, reached by jumping straight into the
// middle of the loop body, which the fixpoint has no way to model since
// that entry point was never one of the header states it converged over).
// `in_switch` flips to true only for the subtree rooted at a nested
// ND_SWITCH's own body, so a case genuinely belonging to that inner switch
// doesn't trip the same check -- it's fully contained and handled by that
// switch's own (separate) precise pass.
static bool nn_precise_ok(Node *node, bool in_switch) {
    for (; node; node = node->next) {
        bool then_in_switch = in_switch;
        switch (node->kind) {
            case ND_LABEL:
                return false;
            case ND_GOTO:
                if (!node->unique_label)
                    return false; // a real `goto`, not break/continue
                break;
            case ND_GOTO_EXPR:
            case ND_LABEL_VAL:
                return false;     // computed goto / labels-as-values
            case ND_CASE:
                if (!in_switch)
                    return false; // Duff's device: case label with no owning
                                  // switch here
                break;
            case ND_SWITCH:
                then_in_switch = true; // node->then is this switch's own body
                break;
            default:
                break;
        }
        if (!nn_precise_ok(node->lhs, in_switch))
            return false;
        if (!nn_precise_ok(node->rhs, in_switch))
            return false;
        if (!nn_precise_ok(node->cond, in_switch))
            return false;
        if (!nn_precise_ok(node->then, then_in_switch))
            return false;
        if (!nn_precise_ok(node->els, in_switch))
            return false;
        if (!nn_precise_ok(node->init, in_switch))
            return false;
        if (!nn_precise_ok(node->inc, in_switch))
            return false;
        if (!nn_precise_ok(node->body, in_switch))
            return false;
        for (Node *a = node->args; a; a = a->next)
            if (!nn_precise_ok(a, in_switch))
                return false;
    }
    return true;
}

// Innermost frame whose `unique_label` matches a break/continue ND_GOTO.
// Matching is by pointer identity, not strcmp: new_unique_name() hands the
// identical string pointer to both the construct's brk_label/cont_label and
// every break/continue targeting it (see parse.c's while/do/for/switch
// construction and the break/continue statement parser).
static NNJumpTarget *nn_find_target(NNCtx *ctx, Node *g) {
    for (NNJumpTarget *t = ctx->jumps; t; t = t->parent) {
        if (t->brk_label && t->brk_label == g->unique_label)
            return t;
        if (t->cont_label && t->cont_label == g->unique_label)
            return t;
    }
    return NULL;
}

// Nearest enclosing switch frame, skipping any loop frames in between (a
// `continue` inside a switch inside a loop must find the *loop*, but a case
// label always belongs to the nearest switch regardless of what's between).
static NNJumpTarget *nn_innermost_switch(NNCtx *ctx) {
    for (NNJumpTarget *t = ctx->jumps; t; t = t->parent)
        if (t->switch_entry)
            return t;
    return NULL;
}

static void nn_walk(NNCtx *ctx, Node *node, NNEnv *env);

static void nn_walk_loop_barrier(NNCtx *ctx, Node *node, NNEnv *env) {
    if (node->cond)
        nn_walk(ctx, node->cond, env);
    // Barrier before entering the body: the loop may run 0+ times and
    // repeat, so a var the body assigns must already read as UNKNOWN on
    // entry to avoid a false positive on a later iteration or on the
    // (already-executed) first one.
    nn_collect_assigned(node->then, env);
    if (node->inc)
        nn_collect_assigned(node->inc, env);
    NNEnv body_env = *env;
    nn_walk(ctx, node->then, &body_env);
    if (node->inc)
        nn_walk(ctx, node->inc, &body_env);
}

static void nn_walk_switch_barrier(NNCtx *ctx, Node *node, NNEnv *env) {
    // Case labels make precise per-branch merging impractical here --
    // barrier the whole body instead. (Caller has already walked node->cond.)
    nn_collect_assigned(node->then, env);
    NNEnv body_env = *env;
    nn_walk(ctx, node->then, &body_env);
}

#define NN_LOOP_ITER_CAP                                                       \
    5 // lattice height (3) + a round to observe stability + slack

// Bounded back-edge fixpoint for ND_FOR/ND_DO (#689). `is_do` selects
// do-while semantics: the body always runs at least once, so (unlike
// ND_FOR/while) the pre-loop state does not flow directly into the exit env.
// Caller has already walked node->init (it runs exactly once regardless of
// which path -- precise or barrier -- ends up handling the rest) and must
// fall back to nn_walk_loop_barrier() if this returns false.
static bool nn_walk_loop_precise(NNCtx *ctx, Node *node, NNEnv *env,
                                 bool is_do) {
    if (ctx->exhausted)
        return false;
    if (!nn_precise_ok(node->then, false) ||
        (node->inc && !nn_precise_ok(node->inc, false)) ||
        (node->cond && !nn_precise_ok(node->cond, false)))
        return false;

    int    mark        = ctx->pool_top;
    NNEnv *header_in   = nn_env_alloc(ctx);
    NNEnv *cur         = nn_env_alloc(ctx);
    NNEnv *next        = nn_env_alloc(ctx);
    NNEnv *probe       = nn_env_alloc(ctx);
    NNEnv *body_out    = nn_env_alloc(ctx);
    NNEnv *back        = nn_env_alloc(ctx);
    NNEnv *brk         = nn_env_alloc(ctx);
    NNEnv *cont        = nn_env_alloc(ctx);
    NNEnv *final_env   = nn_env_alloc(ctx);
    NNEnv *exit_normal = nn_env_alloc(ctx);
    NNEnv *exit_env    = nn_env_alloc(ctx);
    if (!header_in || !cur || !next || !probe || !body_out || !back || !brk ||
        !cont || !final_env || !exit_normal || !exit_env) {
        nn_env_release(ctx, mark);
        return false;
    }

    *header_in         = *env;
    *cur               = *header_in;

    NNJumpTarget frame = {
        .brk_label    = node->brk_label,
        .cont_label   = node->cont_label,
        .brk_env      = brk,
        .cont_env     = cont,
        .switch_entry = NULL,
        .parent       = ctx->jumps,
    };
    ctx->jumps       = &frame;

    bool saved_quiet = ctx->quiet;
    ctx->quiet       = true;
    bool converged   = false;

    for (int iter = 0; iter < NN_LOOP_ITER_CAP && !ctx->exhausted; iter++) {
        brk->count     = 0;
        brk->live      = false;
        brk->overflow  = false;
        cont->count    = 0;
        cont->live     = false;
        cont->overflow = false;

        *probe         = *cur;
        if (!is_do && node->cond)
            nn_walk(ctx, node->cond, probe);
        *body_out = *probe;
        nn_walk(ctx, node->then, body_out);
        nn_env_join_into(ctx, back, body_out,
                         cont); // continue lands at inc/cond
        if (node->inc)
            nn_walk(ctx, node->inc, back);
        if (is_do && node->cond)
            nn_walk(ctx, node->cond, back);

        nn_env_join_into(ctx, next, header_in, back);
        if (ctx->exhausted || next->overflow)
            break;
        if (nn_env_equal(ctx, next, cur)) {
            converged = true;
            break;
        }
        *cur = *next;
    }

    ctx->quiet = saved_quiet;

    if (!converged) {
        ctx->jumps = frame.parent;
        nn_env_release(ctx, mark);
        return false;
    }

    // Single final reporting pass, at the converged fixpoint, with `quiet`
    // restored to whatever the caller had. This is the only walk of this
    // loop's subtree that can emit a diagnostic -- every fixpoint iteration
    // above ran fully quiet, so nothing above this point has emitted yet.
    brk->count     = 0;
    brk->live      = false;
    brk->overflow  = false;
    cont->count    = 0;
    cont->live     = false;
    cont->overflow = false;

    *final_env     = *cur;
    if (!is_do && node->cond)
        nn_walk(ctx, node->cond, final_env);
    // for/while: cond is checked *before* the body, so "cond false" (the
    // state right after walking cond, before the body runs at all) is a
    // genuine exit path -- this is the zero-trip case. A `for (;;)` with no
    // cond at all (node->cond == NULL) has no such path -- it can only ever
    // exit via a break -- so exit_normal must be BOTTOM, not *final_env,
    // or a break-only infinite loop would spuriously pick up an
    // unreachable "fell out normally" predecessor and lose precision.
    if (!is_do) {
        if (node->cond)
            *exit_normal = *final_env;
        else {
            exit_normal->count    = 0;
            exit_normal->live     = false;
            exit_normal->overflow = false;
        }
    }

    nn_walk(ctx, node->then, final_env);
    nn_env_join_into(ctx, back, final_env, cont);
    if (node->inc)
        nn_walk(ctx, node->inc, back);
    if (is_do && node->cond)
        nn_walk(ctx, node->cond, back);
    // do-while: cond is checked *after* the body, so the body always runs at
    // least once and the only exit path is "cond false" once execution has
    // already reached `back` (post body+inc+cond) -- unlike for/while, this
    // must NOT include header_in (no zero-trip path exists for a do-while).
    if (is_do)
        *exit_normal = *back;

    nn_env_join_into(ctx, exit_env, exit_normal, brk);
    *env       = *exit_env;

    ctx->jumps = frame.parent;
    nn_env_release(ctx, mark);
    return true;
}

// Single-pass (no fixpoint needed -- no back edge) precise ND_SWITCH
// handling. Caller has already walked node->cond and must fall back to
// nn_walk_switch_barrier() if this returns false.
static bool nn_walk_switch_precise(NNCtx *ctx, Node *node, NNEnv *env) {
    if (ctx->exhausted || !nn_precise_ok(node->then, true))
        return false;

    int    mark     = ctx->pool_top;
    NNEnv *entry    = nn_env_alloc(ctx);
    NNEnv *brk      = nn_env_alloc(ctx);
    NNEnv *body     = nn_env_alloc(ctx);
    NNEnv *exit_env = nn_env_alloc(ctx);
    if (!entry || !brk || !body || !exit_env) {
        nn_env_release(ctx, mark);
        return false;
    }
    *entry             = *env;
    brk->count         = 0;
    brk->live          = false;
    brk->overflow      = false;

    NNJumpTarget frame = {
        .brk_label    = node->brk_label,
        .cont_label   = NULL,
        .brk_env      = brk,
        .cont_env     = NULL,
        .switch_entry = entry,
        .parent       = ctx->jumps,
    };
    ctx->jumps     = &frame;

    body->count    = 0;
    body->live     = false; // nothing before the first case label is reachable
    body->overflow = false;
    nn_walk(ctx, node->then, body);

    nn_env_join_into(ctx, exit_env, body, brk);
    if (!node->default_case)
        // No `default`: the whole switch is itself skippable, so the
        // pre-switch entry env is also a valid predecessor of the exit.
        nn_env_join_into(ctx, exit_env, exit_env, entry);
    *env       = *exit_env;

    ctx->jumps = frame.parent;
    nn_env_release(ctx, mark);
    return true;
}

// Real per-branch merge dataflow (#687): each live branch is walked with its
// own full copy of env, then the live branches' end-states are joined back
// into `env` via nn_env_join_into(). A statically-dead branch (per
// static_branch_value) contributes nothing, matching the dead-branch
// pruning used elsewhere in the compiler (see
// test_warning_nonnull_flow_dead.c).
//
// A missing `else` is treated as an implicit empty branch -- the "skip the
// whole if" path -- which is itself live (and joins in the pre-branch state
// unchanged) unless the condition is statically always-true (bv == 1), in
// which case skipping is provably impossible.
static void nn_walk_branch(NNCtx *ctx, Node *node, NNEnv *env) {
    nn_walk(ctx, node->cond, env);

    int    bv                 = static_branch_value(ctx->vm, node->cond);
    bool   then_dead          = (bv == 0);
    bool   els_specified      = node->els != NULL;
    bool   els_dead           = els_specified && (bv == 1);
    bool   implicit_skip_live = !els_specified && bv != 1;

    int    mark               = ctx->pool_top;
    NNEnv *pre                = nn_env_alloc(ctx);
    NNEnv *then_env           = nn_env_alloc(ctx);
    NNEnv *els_env            = nn_env_alloc(ctx);
    if (!pre || !then_env || !els_env) {
        // Pool exhausted: give up precisely (rather than mis-tracking a
        // branch we can't afford to clone) and fall back to a safe reset.
        nn_env_release(ctx, mark);
        env->count = 0;
        env->live  = true;
        return;
    }
    *pre = *env; // snapshot for the implicit-skip / "b absent" case below
    bool then_live  = !then_dead;
    bool right_live = (els_specified && !els_dead) || implicit_skip_live;

    if (then_live) {
        *then_env = *env;
        nn_walk(ctx, node->then, then_env);
    }
    NNEnv *right_env = NULL;
    if (els_specified && !els_dead) {
        *els_env = *env;
        nn_walk(ctx, node->els, els_env);
        right_env = els_env;
    } else if (implicit_skip_live) {
        right_env = pre; // "skip the if" path: env unchanged
    }

    if (then_live && right_live)
        nn_env_join_into(ctx, env, then_env, right_env);
    else if (then_live)
        *env = *then_env;
    else if (right_live)
        *env = *right_env;
    // else: both sides dead -- unreachable code; env (== *pre) is already
    // correct.

    nn_env_release(ctx, mark);
}

static void nn_walk(NNCtx *ctx, Node *node, NNEnv *env) {
    for (; node; node = node->next) {
        if (ctx->exhausted) {
            env->count = 0;
            env->live  = true; // safe give-up: UNKNOWN never warns
            return;
        }
        if (--ctx->budget <= 0) {
            ctx->exhausted = true;
            env->count     = 0;
            env->live      = true;
            return;
        }
        switch (node->kind) {
            case ND_LABEL:
                // A goto label is a jump target reachable from unknown
                // predecessors -- discard all tracked state to stay sound, but
                // it *is* reachable (unlike the unreachable-code-after-a-goto
                // case below), so live must come back to true here.
                env->count = 0;
                env->live  = true;
                nn_walk(ctx, node->lhs, env);
                continue;

            case ND_CASE: {
                // #689: precise per-case merge -- join the switch's entry env
                // into the fall-through env, replacing the old "reset to
                // UNKNOWN" treatment. In barrier mode (no enclosing switch
                // frame
                // -- either this switch bailed, or nn_walk reached this case
                // via some other unmodeled path) fall back to the reset.
                NNJumpTarget *sw = nn_innermost_switch(ctx);
                if (sw)
                    nn_env_join_into(ctx, env, env, sw->switch_entry);
                else {
                    env->count = 0;
                    env->live  = true;
                }
                nn_walk(ctx, node->lhs, env);
                continue;
            }

            case ND_GOTO:
                if (node->unique_label) {
                    NNJumpTarget *t = nn_find_target(ctx, node);
                    if (t) {
                        // A break or continue whose target we're precisely
                        // tracking: accumulate this path's env into the target
                        // construct's break/continue accumulator, then this
                        // path is gone (bottom) from here on.
                        bool   is_cont = (t->cont_label &&
                                          t->cont_label == node->unique_label);
                        NNEnv *acc     = is_cont ? t->cont_env : t->brk_env;
                        nn_env_join_into(ctx, acc, acc, env);
                        env->live = false;
                    } else {
                        // Unmatched: the target construct pushed no frame,
                        // meaning it's being walked in barrier mode (it bailed
                        // out of the precise scheme -- see nn_precise_ok). We
                        // are therefore *inside* that barrier walk right now,
                        // and this break/continue does not actually exit any
                        // precise construct we're tracking -- structurally,
                        // control still falls through to whatever textually
                        // follows in the barrier-walked body. Treating it as
                        // bottom here would be unsound (it could make an
                        // unrelated outer loop's fixpoint miss a live path and
                        // manufacture a plain-nonnull false positive); a safe
                        // reset is the correct, conservative answer instead.
                        //
                        // NOTE: it's not fully established whether this branch
                        // is actually reachable. nn_walk_loop_barrier() and
                        // nn_walk_switch_barrier() both walk their body into a
                        // throwaway `body_env` copy and never write the result
                        // back into the caller's `env` -- so a break/continue
                        // inside a barrier-mode construct is already isolated
                        // by that discard, regardless of what it does to `env`
                        // here. Reaching this branch would need an unmatched
                        // break/continue whose env *does* flow forward into
                        // something live, e.g. nested inside a precise
                        // construct that is itself inside a barrier one. Kept
                        // as defensive, zero-cost insurance either way: if that
                        // isolation property above ever changes, this is still
                        // the correct (conservative, never-false-positive)
                        // answer.
                        env->count = 0;
                        env->live  = true;
                    }
                } else {
                    // A real `goto`: the enclosing construct was already
                    // rejected by nn_precise_ok for containing one, so we must
                    // be in barrier mode here. Code after this point in the
                    // same statement list is unreachable via fall-through (it's
                    // only reachable, if at all, via some ND_LABEL elsewhere,
                    // which resets env itself when reached).
                    env->count = 0;
                    env->live  = false;
                }
                continue;

            case ND_ASSIGN:
                if (node->rhs)
                    nn_walk(ctx, node->rhs, env);
                if (node->lhs && node->lhs->kind != ND_VAR)
                    nn_walk(ctx, node->lhs, env);
                if (node->lhs && node->lhs->kind == ND_VAR &&
                    nn_trackable(node->lhs->var)) {
                    NNState *slot = nn_env_slot(env, node->lhs->var);
                    if (slot)
                        *slot = nn_state_of_expr(ctx->vm, env, node->rhs);
                }
                continue;

            case ND_FUNCALL:
                for (Node *a = node->args; a; a = a->next)
                    nn_walk(ctx, a, env);
                nn_check_call_args(ctx, env, node);
                continue;

            case ND_RETURN:
                if (node->lhs) {
                    nn_walk(ctx, node->lhs, env);
                    nn_check_return(ctx, env, node->lhs);
                }
                env->live =
                    false; // control does not fall through past a return
                continue;

            case ND_IF:
            case ND_COND:
                nn_walk_branch(ctx, node, env);
                continue;

            case ND_LOGAND:
            case ND_LOGOR:
                // rhs is conditionally evaluated (short-circuit) -- same
                // clone-then-join merge as an ND_IF/ND_COND branch (#687): the
                // "rhs evaluated" path and the "short-circuited, rhs skipped"
                // path (env right after lhs, unchanged) are joined back in.
                nn_walk(ctx, node->lhs, env);
                {
                    NNEnv pre     = *env;
                    NNEnv rhs_env = *env;
                    nn_walk(ctx, node->rhs, &rhs_env);
                    nn_env_join_into(ctx, env, &rhs_env, &pre);
                }
                continue;

            case ND_FOR:
            case ND_DO:
                if (node->init)
                    nn_walk(ctx, node->init, env);
                if (nn_walk_loop_precise(ctx, node, env, node->kind == ND_DO))
                    continue;
                nn_walk_loop_barrier(ctx, node, env);
                continue;

            case ND_SWITCH:
                if (node->cond)
                    nn_walk(ctx, node->cond, env);
                if (nn_walk_switch_precise(ctx, node, env))
                    continue;
                nn_walk_switch_barrier(ctx, node, env);
                continue;

            default:
                break;
        }

        nn_walk(ctx, node->lhs, env);
        nn_walk(ctx, node->rhs, env);
        nn_walk(ctx, node->cond, env);
        nn_walk(ctx, node->then, env);
        nn_walk(ctx, node->els, env);
        nn_walk(ctx, node->init, env);
        nn_walk(ctx, node->inc, env);
        nn_walk(ctx, node->body, env);
        for (Node *a = node->args; a; a = a->next)
            nn_walk(ctx, a, env);
    }
}

// ---------------------------------------------------------------------
// Interprocedural "may return null" summaries (#688, follow-up to #687)
//
// A whole-translation-unit pass, run once after every function has been
// parsed (see the post-parse loop in parse()), that flags each
// pointer-returning function with a visible body which has a provable
// null-returning path (a `return 0;`/`return NULL;` reachable per
// static_branch_value dead-branch pruning). The fact is consumed at call
// sites by nn_state_of_expr() below, which reports NN_MAYBE for a call to
// a flagged function -- never NN_NULL, matching #688's framing that a
// callee-may-return-null fact "naturally produces a MAYBE state." This
// keeps the interprocedural extension entirely behind the opt-in
// -Wmaybe-nonnull flag; plain -Wnonnull is completely unaffected.
//
// Conservative in the safe direction only: a function is flagged solely on
// positive evidence of a literal-null return. Anything else -- no return
// statement found, a non-literal return, or (crucially) no visible body at
// all, e.g. an extern-only declaration -- leaves may_return_null false,
// which callers read as "no evidence" (NN_UNKNOWN). This is what keeps
// unknown/external callees from flooding every f(g()) call site with a
// warning, per #688's explicit constraint.
//
// Only a literal-null return is detected here, not a call to another
// flagged function (`return g();` where g may return null) -- transitive
// summaries would need a call-graph fixpoint/topological order and are
// deferred as a follow-up, same spirit as the existing loop/switch
// barriers being simpler than real fixpoint dataflow.
//
// Evaluates whether a *value-producing expression* (a return's operand)
// may be null -- as opposed to nn_returns_null_walk below, which searches
// *statement* trees for a nested `return 0;`. A ternary used as the
// return operand itself (e.g. `return cond ? &x : 0;`, the ticket's own
// example) needs this distinct expression-level check: its `then`/`els`
// arms are values to evaluate for nullness, not statement bodies to search
// for further return statements.
static bool nn_expr_may_be_null(VirtualMachine *vm, Node *expr) {
    expr = nn_strip_cast(expr);
    if (!expr)
        return false;
    if (is_const_expr(vm, expr))
        return eval(vm, expr) == 0;
    if (expr->kind == ND_COND) {
        int bv = static_branch_value(vm, expr->cond);
        if (bv != 0 && nn_expr_may_be_null(vm, expr->then))
            return true;
        if (bv != 1 && nn_expr_may_be_null(vm, expr->els))
            return true;
    }
    // #693: a return expression that is itself a direct call to an
    // already-flagged function is transitive null-returning evidence too
    // (`return other_maybe_null_fn();`). check_may_return_null_summaries()
    // runs this to a fixpoint so chains of any depth/source order converge.
    if (expr->kind == ND_FUNCALL && expr->lhs && expr->lhs->kind == ND_VAR &&
        expr->lhs->var && expr->lhs->var->may_return_null)
        return true;
    return false;
}

static bool nn_returns_null_walk(VirtualMachine *vm, Node *node) {
    for (; node; node = node->next) {
        if (node->kind == ND_IF || node->kind == ND_COND) {
            int bv = static_branch_value(vm, node->cond);
            if (bv != 0 && nn_returns_null_walk(vm, node->then))
                return true;
            if (node->els && bv != 1 && nn_returns_null_walk(vm, node->els))
                return true;
            continue; // dead branches already excluded above -- skip the
                      // generic recursion
        }
        if (node->kind == ND_RETURN && node->lhs &&
            nn_expr_may_be_null(vm, node->lhs))
            return true;
        if (nn_returns_null_walk(vm, node->lhs))
            return true;
        if (nn_returns_null_walk(vm, node->rhs))
            return true;
        if (node->kind != ND_IF && node->kind != ND_COND) {
            if (nn_returns_null_walk(vm, node->cond))
                return true;
            if (nn_returns_null_walk(vm, node->then))
                return true;
            if (nn_returns_null_walk(vm, node->els))
                return true;
        }
        if (nn_returns_null_walk(vm, node->init))
            return true;
        if (nn_returns_null_walk(vm, node->inc))
            return true;
        if (nn_returns_null_walk(vm, node->body))
            return true;
        for (Node *a = node->args; a; a = a->next)
            if (nn_returns_null_walk(vm, a))
                return true;
    }
    return false;
}

// #692: the "definitely" counterpart to nn_expr_may_be_null() above -- true
// only when every live arm of the expression is provably null (a literal, or
// a ternary whose live then/els arms are all provably null, or a direct call
// to a function already proved to always return null). Used by
// nn_all_returns_null_walk() below to check whether *every* reachable return
// in a function is null, as opposed to nn_expr_may_be_null()'s existential
// "does at least one live arm evaluate to null".
static bool nn_expr_is_null(VirtualMachine *vm, Node *expr) {
    expr = nn_strip_cast(expr);
    if (!expr)
        return false;
    if (is_const_expr(vm, expr))
        return eval(vm, expr) == 0;
    if (expr->kind == ND_COND) {
        int bv = static_branch_value(vm, expr->cond);
        if (bv == 0)
            return nn_expr_is_null(vm, expr->els);
        if (bv == 1)
            return nn_expr_is_null(vm, expr->then);
        return nn_expr_is_null(vm, expr->then) &&
               nn_expr_is_null(vm, expr->els);
    }
    if (expr->kind == ND_FUNCALL && expr->lhs && expr->lhs->kind == ND_VAR &&
        expr->lhs->var && expr->lhs->var->always_returns_null)
        return true;
    return false;
}

// #692: true when every reachable return statement in `node`'s subtree is
// provably null via nn_expr_is_null() -- the "for all" counterpart to
// nn_returns_null_walk()'s "there exists" search. Soundness relies on
// append_implicit_return() having already materialized a real `return
// (T)0;` node for any pointer-returning function that could otherwise fall
// off the end, so every live path through the function is guaranteed to hit
// an actual ND_RETURN by the time this runs -- there's no separate
// fall-off-the-end case to account for here.
static bool nn_all_returns_null_walk(VirtualMachine *vm, Node *node) {
    for (; node; node = node->next) {
        if (node->kind == ND_IF || node->kind == ND_COND) {
            int bv = static_branch_value(vm, node->cond);
            if (bv != 0 && !nn_all_returns_null_walk(vm, node->then))
                return false;
            if (node->els && bv != 1 &&
                !nn_all_returns_null_walk(vm, node->els))
                return false;
            continue; // dead branches already excluded above -- skip the
                      // generic recursion
        }
        if (node->kind == ND_RETURN) {
            if (!node->lhs || !nn_expr_is_null(vm, node->lhs))
                return false;
            continue;
        }
        if (!nn_all_returns_null_walk(vm, node->lhs))
            return false;
        if (!nn_all_returns_null_walk(vm, node->rhs))
            return false;
        if (node->kind != ND_IF && node->kind != ND_COND) {
            if (!nn_all_returns_null_walk(vm, node->cond))
                return false;
            if (!nn_all_returns_null_walk(vm, node->then))
                return false;
            if (!nn_all_returns_null_walk(vm, node->els))
                return false;
        }
        if (!nn_all_returns_null_walk(vm, node->init))
            return false;
        if (!nn_all_returns_null_walk(vm, node->inc))
            return false;
        if (!nn_all_returns_null_walk(vm, node->body))
            return false;
        for (Node *a = node->args; a; a = a->next)
            if (!nn_all_returns_null_walk(vm, a))
                return false;
    }
    return true;
}

// Entry point for the summary pass: called once from parse() after every
// top-level function has been parsed, so a caller anywhere in the
// translation unit sees a complete summary for every callee, regardless of
// source order (the #688 fix for check_nonnull_flow's forward-reference
// gap -- see the post-parse loop in parse()).
void check_may_return_null_summaries(VirtualMachine *vm) {
    if (!(vm->compiler.warnings & CCCC_WARN_MAYBE_NONNULL))
        return; // fact is only ever consumed under -Wmaybe-nonnull
    // #693/#692: iterate both facts to a fixpoint together so a transitive
    // chain (relay() whose only null-returning path is `return
    // maybe_null();`, or whose every path is `return always_null_fn();`)
    // converges regardless of how many hops deep it is or which function is
    // defined first in the translation unit. Both flags only ever flip
    // false->true, so this always terminates within at most one pass per
    // function. always_returns_null implies may_return_null (a function
    // that returns null on every path also has at least one null-returning
    // path), so set both together.
    bool changed;
    do {
        changed = false;
        for (Obj *fn = vm->compiler.globals; fn; fn = fn->next) {
            if (!(fn->is_function && fn->body && fn->ty &&
                  fn->ty->kind == TY_FUNC && fn->ty->return_ty &&
                  fn->ty->return_ty->kind == TY_PTR))
                continue;
            if (!fn->may_return_null && nn_returns_null_walk(vm, fn->body)) {
                fn->may_return_null = true;
                changed             = true;
            }
            if (!fn->always_returns_null &&
                nn_all_returns_null_walk(vm, fn->body)) {
                fn->always_returns_null = true;
                fn->may_return_null     = true;
                changed                 = true;
            }
        }
    } while (changed);
}

// Entry point: run the flow-sensitive nonnull pass over a fully-parsed
// function body. Call after mark_addr_escapes() has run (this pass relies
// on Obj->addr_escapes to exclude address-taken locals from tracking).
void check_nonnull_flow(VirtualMachine *vm, Obj *fn) {
    if (!fn || !fn->body || !fn->ty)
        return;
    if (!(vm->compiler.warnings &
          (CCCC_WARN_NONNULL | CCCC_WARN_MAYBE_NONNULL)))
        return;
    NNCtx ctx  = {0};
    ctx.vm     = vm;
    ctx.fn     = fn;
    ctx.budget = NN_WALK_BUDGET;
    NNEnv env  = {0};
    env.live   = true;
    nn_walk(&ctx, fn->body, &env);
}

// Warn on statically-provable-null arguments passed to a parameter marked
// __attribute__((nonnull)) / [[gnu::nonnull]]. Only literal/constant-folded
// null values are caught here -- no flow analysis across variables.
void validate_nonnull_call(VirtualMachine *vm, Type *func_ty, Node *args) {
    if (!func_ty->nonnull_all && !func_ty->nonnull_mask)
        return;

    Node *arg      = args;
    Type *param_ty = func_ty->params;
    int   idx      = 1;
    while (arg && param_ty) {
        bool marked =
            func_ty->nonnull_all
                ? (param_ty->kind == TY_PTR)
                : (idx <= 64 && (func_ty->nonnull_mask & (1ULL << (idx - 1))));
        if (marked && is_const_expr(vm, arg) && eval(vm, arg) == 0)
            warn_tok(vm, arg->tok, CCCC_WARN_NONNULL,
                     "null passed to a parameter marked nonnull (parameter %d)",
                     idx);
        arg      = arg->next;
        param_ty = param_ty->next;
        idx++;
    }
}

// Warn when a call to a function marked __attribute__((sentinel)) /
// __attribute__((sentinel(N))) / [[gnu::sentinel]] does not terminate its
// variadic arguments with a literal NULL (#658). `sentinel_pos` counts
// trailing non-sentinel arguments allowed before the NULL (0 = last arg),
// so the target argument is counted from the END of the call's argument
// list, unlike the format-string validator which counts from the front.
// Only a literal/constant-folded null is accepted -- a variable that
// happens to hold NULL still warns, matching GCC's syntactic check.
void validate_sentinel_call(VirtualMachine *vm, Token *tok, Type *func_ty,
                            Node *args) {
    if (!func_ty->is_sentinel)
        return;
    // #696: sentinel on a non-variadic function is misapplied; that is
    // already flagged at the declaration (check_sentinel_variadic()), so
    // don't also trip the "not enough variable arguments" guard on every
    // call -- there being no variadic args at all is the real problem, not
    // a missing NULL.
    if (!func_ty->is_variadic)
        return;

    int nargs = 0;
    for (Node *a = args; a; a = a->next)
        nargs++;
    int num_named = 0;
    for (Type *p = func_ty->params; p; p = p->next)
        num_named++;

    int target = nargs - 1 - func_ty->sentinel_pos;
    // Bound both ends: target < num_named catches sentinel_pos >= nargs
    // (too few variadic args); target >= nargs catches a negative
    // sentinel_pos (e.g. sentinel(-1)), which would otherwise walk off
    // the end of the argument list.
    if (target < num_named || target >= nargs) {
        warn_tok(vm, tok, CCCC_WARN_SENTINEL,
                 "not enough variable arguments to fit a sentinel");
        return;
    }

    Node *arg = args;
    for (int i = 0; i < target; i++)
        arg = arg->next;
    if (!(is_const_expr(vm, arg) && eval(vm, arg) == 0)) {
        warn_tok(vm, arg->tok, CCCC_WARN_SENTINEL,
                 "missing sentinel in function call");
    } else if (arg->ty->kind != TY_PTR && arg->ty->kind != TY_NULLPTR_T) {
        // #695: a literal 0 that is not pointer-typed (bare "int 0" rather
        // than NULL/(void*)0/nullptr) still warns, matching GCC's stricter
        // -Wsentinel: an untyped 0 is not guaranteed to zero-fill a
        // pointer-sized va_list slot.
        warn_tok(vm, arg->tok, CCCC_WARN_SENTINEL,
                 "missing sentinel in function call "
                 "(bare 0 is not a pointer; cast NULL / (void*)0)");
    }
}
