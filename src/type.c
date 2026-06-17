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

 This file was original part of chibicc by Rui Ueyama (MIT) https://github.com/rui314/chibicc
*/

#include "cccc.h"
#include "./internal.h"

// Type sizes now match standard C sizes with proper VM instruction support:
// char=1, short=2, int=4, long=8
Type *ty_void = &(Type){TY_VOID, 1, 1};
Type *ty_bool = &(Type){TY_BOOL, 1, 1};
Type *ty_nullptr_t = &(Type){TY_NULLPTR_T, 8, 8, true};

Type *ty_char = &(Type){TY_CHAR, 1, 1};
Type *ty_short = &(Type){TY_SHORT, 2, 2};
Type *ty_int = &(Type){TY_INT, 4, 4};
Type *ty_long = &(Type){TY_LONG, 8, 8};

Type *ty_uchar = &(Type){TY_CHAR, 1, 1, true};
Type *ty_ushort = &(Type){TY_SHORT, 2, 2, true};
Type *ty_uint = &(Type){TY_INT, 4, 4, true};
Type *ty_ulong = &(Type){TY_LONG, 8, 8, true};

Type *ty_float = &(Type){TY_FLOAT, 4, 4};
Type *ty_double = &(Type){TY_DOUBLE, 8, 8};
Type *ty_ldouble = &(Type){TY_LDOUBLE, 16, 16};

Type *ty_fcomplex = &(Type){.kind = TY_COMPLEX, .size = 8, .align = 4,
                             .base = &(Type){TY_FLOAT, 4, 4}};
Type *ty_dcomplex = &(Type){.kind = TY_COMPLEX, .size = 16, .align = 8,
                             .base = &(Type){TY_DOUBLE, 8, 8}};
Type *ty_ldcomplex = &(Type){.kind = TY_COMPLEX, .size = 32, .align = 16,
                              .base = &(Type){TY_LDOUBLE, 16, 16}};

// C23 decimal floating-point: correct sizes (4/8/16 bytes) but implemented as
// aliases of float/double/long double — not IEEE-754-2008 decimal encoding.
// Placeholder until a real decimal arithmetic library is available.
Type *ty_decimal32  = &(Type){.kind = TY_FLOAT,   .size = 4,  .align = 4,
                               .is_decimal = true};
Type *ty_decimal64  = &(Type){.kind = TY_DOUBLE,  .size = 8,  .align = 8,
                               .is_decimal = true};
Type *ty_decimal128 = &(Type){.kind = TY_LDOUBLE, .size = 16, .align = 16,
                               .is_decimal = true};

static Type ty_error_obj = {TY_ERROR, 0, 1};
Type *ty_error = &ty_error_obj;

// C23 auto type-inference sentinel; never reaches codegen
static Type ty_auto_obj = {TY_AUTO, 0, 0};
Type *ty_auto = &ty_auto_obj;

static Type *new_type(VirtualMachine *vm, TypeKind kind, int size, int align) {
    Type *ty = arena_alloc(&vm->compiler.parser_arena, sizeof(Type));
    memset(ty, 0, sizeof(Type));
    ty->kind = kind;
    ty->size = size;
    ty->align = align;
    return ty;
}

// C23 _BitInt(N): bit-precise integer type, N in [1,65535] (BITINT_MAXWIDTH).
// Container is the smallest of {1,2,4,8} bytes covering N bits for N<=64.
// N>64 uses multi-word (address-based) storage; see src/stdlib/wide_bitint.c.
// Values are truncated to N bits after arithmetic via mask/shift.
Type *bitint_type(VirtualMachine *vm, Token *tok, int width, bool is_unsigned) {
    if (width < 1)
        error_tok(vm, tok, "_BitInt width must be at least 1, got %d", width);
    if (!is_unsigned && width < 2)
        error_tok(vm, tok, "signed _BitInt requires at least 2 bits (sign + value), got %d", width);
    if (width > 65535)
        error_tok(vm, tok, "_BitInt width %d exceeds maximum 65535 (BITINT_MAXWIDTH)", width);

    int sz;
    if (width <= 8)       sz = 1;
    else if (width <= 16) sz = 2;
    else if (width <= 32) sz = 4;
    else if (width <= 64) sz = 8;
    else                  sz = (width + 63) / 64 * 8; // ceil to 8-byte word boundary
    int al = sz > 8 ? 8 : sz;
    Type *ty = new_type(vm, TY_BITINT, sz, al);
    ty->is_unsigned = is_unsigned;
    ty->bit_width = width;
    return ty;
}

bool is_integer(Type *ty) {
    if (!ty) return false;
    TypeKind k = ty->kind;
    return k == TY_BOOL || k == TY_CHAR || k == TY_SHORT ||
    k == TY_INT  || k == TY_LONG || k == TY_ENUM || k == TY_BITINT;
}

bool is_flonum(Type *ty) {
    if (!ty) return false;
    return ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE ||
    ty->kind == TY_LDOUBLE;
}

bool is_complex(Type *ty) {
    if (!ty) return false;
    return ty->kind == TY_COMPLEX;
}

bool is_numeric(Type *ty) {
    if (!ty) return false;
    return is_integer(ty) || is_flonum(ty) || is_complex(ty);
}

bool is_error_type(Type *ty) {
    return ty && ty->kind == TY_ERROR;
}

bool is_compatible(Type *t1, Type *t2) {
    if (t1 == t2)
        return true;

    if (t1->origin)
        return is_compatible(t1->origin, t2);

    if (t2->origin)
        return is_compatible(t1, t2->origin);

    if (t1->kind != t2->kind)
        return false;

    switch (t1->kind) {
        case TY_CHAR:
        case TY_SHORT:
        case TY_INT:
        case TY_LONG:
            return t1->is_unsigned == t2->is_unsigned;
        case TY_FLOAT:
        case TY_DOUBLE:
        case TY_LDOUBLE:
            return true;
        case TY_COMPLEX:
            return is_compatible(t1->base, t2->base);
        case TY_PTR:
            return is_compatible(t1->base, t2->base);
        case TY_FUNC: {
            if (!is_compatible(t1->return_ty, t2->return_ty))
                return false;
            if (t1->is_variadic != t2->is_variadic)
                return false;

            Type *p1 = t1->params;
            Type *p2 = t2->params;
            for (; p1 && p2; p1 = p1->next, p2 = p2->next)
                if (!is_compatible(p1, p2))
                    return false;
            return p1 == NULL && p2 == NULL;
        }
        case TY_ARRAY:
            if (!is_compatible(t1->base, t2->base))
                return false;
            return t1->array_len < 0 && t2->array_len < 0 &&
            t1->array_len == t2->array_len;
        case TY_BITINT:
            return t1->is_unsigned == t2->is_unsigned &&
                   t1->bit_width == t2->bit_width;
        case TY_NULLPTR_T:
            return true;
        default:
            return false;
    }
}

Type *copy_type(VirtualMachine *vm, Type *ty) {
    Type *ret = arena_alloc(&vm->compiler.parser_arena, sizeof(Type));
    *ret = *ty;
    ret->origin = ty;
    // Note: is_const is preserved through memcpy (*ret = *ty)
    return ret;
}

Type *pointer_to(VirtualMachine *vm, Type *base) {
    Type *ty = new_type(vm, TY_PTR, 8, 8);
    ty->base = base;
    ty->is_unsigned = true;
    return ty;
}

Type *func_type(VirtualMachine *vm, Type *return_ty) {
    // The C spec disallows sizeof(<function type>), but
    // GCC allows that and the expression is evaluated to 1.
    Type *ty = new_type(vm, TY_FUNC, 1, 1);
    ty->return_ty = return_ty;
    return ty;
}

Type *array_of(VirtualMachine *vm, Type *base, int len) {
    int sz;
    if (len > 0 && __builtin_mul_overflow(base->size, len, &sz))
        error("array size overflow: element size %d times length %d exceeds INT_MAX", base->size, len);
    else
        sz = base->size * len;
    Type *ty = new_type(vm, TY_ARRAY, sz, base->align);
    ty->base = base;
    ty->array_len = len;
    return ty;
}

Type *vla_of(VirtualMachine *vm, Type *base, Node *len) {
    Type *ty = new_type(vm, TY_VLA, 8, 8);
    ty->base = base;
    ty->vla_len = len;
    return ty;
}

Type *enum_type(VirtualMachine *vm) {
    return new_type(vm, TY_ENUM, 4, 4);  // enums are int-sized (4 bytes)
}

Type *struct_type(VirtualMachine *vm) {
    return new_type(vm, TY_STRUCT, 0, 1);
}

Type *union_type(VirtualMachine *vm) {
    return new_type(vm, TY_UNION, 0, 1);
}

Type *block_type(VirtualMachine *vm, Type *return_ty, Type *params) {
    Type *ty = new_type(vm, TY_BLOCK, 8, 8);  // Block pointers are 8 bytes
    ty->return_ty = return_ty;
    ty->params = params;
    return ty;
}

Type *complex_type_for(VirtualMachine *vm, Type *base) {
    (void)vm;
    if (!base || base->kind == TY_DOUBLE)
        return ty_dcomplex;
    if (base->kind == TY_FLOAT)
        return ty_fcomplex;
    if (base->kind == TY_LDOUBLE)
        return ty_ldcomplex;
    return ty_dcomplex;
}

// Integer promotion: Convert types smaller than int to int (C99 6.3.1.1)
// char, short, and bit-fields promote to int if all values fit, else unsigned int
static Type *integer_promotion(Type *ty) {
    // Don't promote error types or NULL
    if (!ty || ty->kind == TY_ERROR)
        return ty;

    if (!is_integer(ty))
        return ty;

    // C23: _BitInt types are exempt from integer promotion (C23 6.3.1.1p2)
    if (ty->kind == TY_BITINT)
        return ty;

    // Types smaller than int promote to int
    if (ty->size < 4) {
        // If it's unsigned and all values don't fit in int, promote to unsigned int
        // But for char and short, int can hold all values of unsigned char/short
        // Only need unsigned int if original type was already unsigned AND larger than what int can hold
        // In our case, unsigned short max (65535) fits in int, so always promote to int
        return ty_int;
    }

    return ty;
}

// Integer conversion rank (C99 6.3.1.1): long > int > short > char
// Approximation for C23 _BitInt: rank = bit_width, sufficient for N<=64.
static int get_integer_rank(Type *ty) {
    switch (ty->kind) {
        case TY_LONG:  return 64;
        case TY_INT:   return 32;
        case TY_SHORT: return 16;
        case TY_CHAR:  return 8;
        case TY_BOOL:  return 1;
        case TY_ENUM:
            if (ty->enum_base_type)
                return get_integer_rank(ty->enum_base_type);
            return 32;  // default: int rank
        case TY_BITINT: return ty->bit_width;
        default: return -1;
    }
}

// Usual arithmetic conversions (C99 6.3.1.8)
static Type *get_common_type(VirtualMachine *vm, Type *ty1, Type *ty2) {
    // Handle error types - propagate error
    if (!ty1 || !ty2 || ty1->kind == TY_ERROR || ty2->kind == TY_ERROR)
        return ty_error;

    if (is_complex(ty1) || is_complex(ty2)) {
        Type *base1 = is_complex(ty1) ? ty1->base : ty1;
        Type *base2 = is_complex(ty2) ? ty2->base : ty2;
        return complex_type_for(vm, get_common_type(vm, base1, base2));
    }

    // C23 nullptr_t vs. pointer (or function, which decays to a pointer):
    // the result is the pointer type, e.g. `ptr == nullptr`.
    if (ty1->kind == TY_NULLPTR_T && (ty2->base || ty2->kind == TY_FUNC))
        return ty2->base ? pointer_to(vm, ty2->base) : pointer_to(vm, ty2);
    if (ty2->kind == TY_NULLPTR_T && (ty1->base || ty1->kind == TY_FUNC))
        return ty1->base ? pointer_to(vm, ty1->base) : pointer_to(vm, ty1);

    // Handle pointer arithmetic
    if (ty1->base)
        return pointer_to(vm, ty1->base);

    // Handle function pointers
    if (ty1->kind == TY_FUNC)
        return pointer_to(vm, ty1);
    if (ty2->kind == TY_FUNC)
        return pointer_to(vm, ty2);

    // Step 1: If either operand has type long double, the other is converted to long double
    if (ty1->kind == TY_LDOUBLE || ty2->kind == TY_LDOUBLE)
        return ty_ldouble;

    // Step 2: Otherwise, if either operand has type double, the other is converted to double
    if (ty1->kind == TY_DOUBLE || ty2->kind == TY_DOUBLE)
        return ty_double;

    // Step 3: Otherwise, if either operand has type float, the other is converted to float
    if (ty1->kind == TY_FLOAT || ty2->kind == TY_FLOAT)
        return ty_float;

    // Step 4: Otherwise, integer promotions are performed on both operands
    ty1 = integer_promotion(ty1);
    ty2 = integer_promotion(ty2);

    // Step 5: If both operands have the same type, no further conversion is needed.
    // For _BitInt, also require matching bit_width (different widths go to step 6).
    if (ty1->kind == ty2->kind && ty1->is_unsigned == ty2->is_unsigned &&
        (ty1->kind != TY_BITINT || ty1->bit_width == ty2->bit_width))
        return ty1;

    // Step 6: If both operands have signed integer types or both have unsigned integer types,
    // the operand with lesser integer conversion rank is converted to the type of the operand with greater rank
    if (ty1->is_unsigned == ty2->is_unsigned) {
        return (get_integer_rank(ty1) >= get_integer_rank(ty2)) ? ty1 : ty2;
    }

    // Step 7: Otherwise, if the type of the operand with unsigned integer type has rank greater than
    // or equal to the rank of the type of the other operand, the operand with signed integer type
    // is converted to the type of the operand with unsigned integer type
    Type *unsigned_ty = ty1->is_unsigned ? ty1 : ty2;
    Type *signed_ty = ty1->is_unsigned ? ty2 : ty1;

    if (get_integer_rank(unsigned_ty) >= get_integer_rank(signed_ty))
        return unsigned_ty;

    // Step 8: Otherwise, if the type of the operand with signed integer type can represent all
    // values of the type of the operand with unsigned integer type, the operand with unsigned
    // integer type is converted to the type of the operand with signed integer type
    if (signed_ty->size > unsigned_ty->size)
        return signed_ty;

    // Step 9: Otherwise, both operands are converted to the unsigned integer type corresponding
    // to the type of the operand with signed integer type
    Type *result = copy_type(vm, signed_ty);
    result->is_unsigned = true;
    return result;
}

// For many binary operators, we implicitly promote operands so that
// both operands have the same type. Any integral type smaller than
// int is always promoted to int. If the type of one operand is larger
// than the other's (e.g. "long" vs. "int"), the smaller operand will
// be promoted to match with the other.
//
// This operation is called the "usual arithmetic conversion".
static void usual_arith_conv(VirtualMachine *vm, Node **lhs, Node **rhs) {
    Type *ty = get_common_type(vm, (*lhs)->ty, (*rhs)->ty);
    // Skip casting if we have error types - they propagate automatically
    if (ty->kind == TY_ERROR)
        return;
    *lhs = new_cast(vm, *lhs, ty);
    *rhs = new_cast(vm, *rhs, ty);
}

// Emit a -Wconversion / -Wsign-conversion / -Wfloat-conversion warning if the
// implicit conversion from expr->ty to `to` is lossy.  Call this BEFORE
// new_cast() so expr->ty is still the source type.
//
// Categories:
//   -Wconversion      integer -> narrower integer (by size)
//   -Wsign-conversion integer -> same-or-narrower integer with differing signedness
//   -Wfloat-conversion float -> integer, integer -> float, or float -> narrower float
//
// Integer cases are suppressed when the source is a constant that fits in the
// destination type (e.g. `char c = 0;` is silent).
void warn_implicit_conversion(VirtualMachine *vm, Node *expr, Type *to, Token *tok) {
    if (!vm || !expr || !to || !expr->ty)
        return;
    Type *from = expr->ty;
    // Skip error types and identical types.
    if (from->kind == TY_ERROR || to->kind == TY_ERROR)
        return;

    // Check for discarded qualifiers in pointer assignments (e.g. const char* -> char*).
    // This must come before the early return for same-kind types below.
    if (from->kind == TY_PTR && to->kind == TY_PTR) {
        Type *from_base = from->base;
        Type *to_base = to->base;
        if (from_base && to_base) {
            char buf[128];
            buf[0] = '\0';
            if (from_base->is_const && !to_base->is_const)
                strcat(buf, "'const'");
            if (from_base->is_volatile && !to_base->is_volatile) {
                if (buf[0]) strcat(buf, ", ");
                strcat(buf, "'volatile'");
            }
            if (from_base->is_restrict && !to_base->is_restrict) {
                if (buf[0]) strcat(buf, ", ");
                strcat(buf, "'restrict'");
            }
            if (buf[0]) {
                int count = (from_base->is_const && !to_base->is_const) +
                            (from_base->is_volatile && !to_base->is_volatile) +
                            (from_base->is_restrict && !to_base->is_restrict);
                warn_tok(vm, tok, CCCC_WARN_DISCARDED_QUALIFIERS,
                         "assignment discards %s qualifier%s from pointer target type",
                         buf, count > 1 ? "s" : "");
            }
        }
        // -Wincompatible-pointer-types: non-void pointee type mismatch
        if ((vm->compiler.warnings & CCCC_WARN_INCOMPATIBLE_POINTER_TYPES) &&
            from_base && to_base &&
            from_base->kind != TY_VOID && to_base->kind != TY_VOID &&
            !is_compatible(from_base, to_base))
            warn_tok(vm, tok, CCCC_WARN_INCOMPATIBLE_POINTER_TYPES,
                     "incompatible pointer types");
        return;
    }

    if (from->kind == to->kind && from->is_unsigned == to->is_unsigned)
        return;

    if (is_integer(from) && is_integer(to)) {
        // Constant-fit suppression: if the value is known to fit, stay silent.
        if (node_int_const_fits(vm, expr, to))
            return;
        if (from->size > to->size) {
            // Narrowing: integer -> smaller integer.
            warn_tok(vm, tok, CCCC_WARN_CONVERSION,
                     "implicit conversion loses integer precision: %s%s to %s%s",
                     from->is_unsigned ? "unsigned " : "",
                     (from->kind == TY_LONG) ? "long" :
                     (from->kind == TY_INT)  ? "int"  :
                     (from->kind == TY_SHORT) ? "short" : "char",
                     to->is_unsigned ? "unsigned " : "",
                     (to->kind == TY_LONG)  ? "long"  :
                     (to->kind == TY_INT)   ? "int"   :
                     (to->kind == TY_SHORT) ? "short" : "char");
        } else if (from->is_unsigned != to->is_unsigned) {
            // Same-size (or widening) but signedness change.
            warn_tok(vm, tok, CCCC_WARN_SIGN_CONVERSION,
                     "implicit conversion changes signedness");
        }
        return;
    }

    if (is_flonum(from) || is_flonum(to)) {
        if (is_integer(to)) {
            // float -> integer (value always truncated).
            warn_tok(vm, tok, CCCC_WARN_FLOAT_CONVERSION,
                     "implicit conversion from floating-point to integer");
        } else if (is_integer(from)) {
            // integer -> float (may lose precision for large integers).
            warn_tok(vm, tok, CCCC_WARN_FLOAT_CONVERSION,
                     "implicit conversion from integer to floating-point");
        } else if (is_flonum(from) && is_flonum(to) && from->size > to->size) {
            // float narrowing: double -> float, long double -> double/float.
            warn_tok(vm, tok, CCCC_WARN_FLOAT_CONVERSION,
                     "implicit conversion loses floating-point precision");
        }
    }
}

// Check for a signed/unsigned comparison mismatch BEFORE usual_arith_conv
// normalises signedness away.  Both operands must be integers after promotion.
// Constant non-negative operands are exempted (e.g. `x < 5` is quiet).
static void check_sign_compare(VirtualMachine *vm, Node *lhs, Node *rhs, Token *tok) {
    if (!is_integer(lhs->ty) || !is_integer(rhs->ty))
        return;
    // Apply integer promotion to reflect what the comparison actually uses.
    bool lhs_unsigned = integer_promotion(lhs->ty)->is_unsigned;
    bool rhs_unsigned = integer_promotion(rhs->ty)->is_unsigned;
    if (lhs_unsigned == rhs_unsigned)
        return;
    // Suppress when one operand is a non-negative integer constant.
    if (lhs->kind == ND_NUM && lhs->val >= 0)
        return;
    if (rhs->kind == ND_NUM && rhs->val >= 0)
        return;
    warn_tok(vm, tok, CCCC_WARN_SIGN_COMPARE,
             "comparison of integers with different signs");
}

static bool lhs_targets_initializing_var(Node *node, Obj *var) {
    if (!node || !var)
        return false;
    switch (node->kind) {
        case ND_VAR:
        case ND_VLA_PTR:
            return node->var == var;
        case ND_MEMBER:
        case ND_DEREF:
            return lhs_targets_initializing_var(node->lhs, var);
        case ND_ADD:
        case ND_SUB:
            return lhs_targets_initializing_var(node->lhs, var) ||
                   lhs_targets_initializing_var(node->rhs, var);
        case ND_CAST:
            return lhs_targets_initializing_var(node->lhs, var);
        default:
            return false;
    }
}

void add_type(VirtualMachine *vm, Node *node) {
    if (!node || (node->ty && node->kind != ND_COMPLEX))
        return;

    add_type(vm, node->lhs);
    add_type(vm, node->rhs);
    add_type(vm, node->cond);
    add_type(vm, node->then);
    add_type(vm, node->els);
    add_type(vm, node->init);
    add_type(vm, node->inc);

    for (Node *n = node->body; n; n = n->next)
        add_type(vm, n);
    for (Node *n2 = node->args; n2; n2 = n2->next)
        add_type(vm, n2);

    // Propagate error type from operands - prevents cascading errors
    if ((node->lhs && node->lhs->ty && is_error_type(node->lhs->ty)) ||
        (node->rhs && node->rhs->ty && is_error_type(node->rhs->ty)) ||
        (node->cond && node->cond->ty && is_error_type(node->cond->ty))) {
        node->ty = ty_error;
        return;
    }

    switch (node->kind) {
        case ND_NUM:
            // Parser already sets the correct type from token (ty_double for 3.14, etc.)
            // Don't override it here!
            return;
        case ND_COMPLEX:
            if (node->val == 0) {
                node->ty = complex_type_for(vm, node->ty ? node->ty->base : ty_double);
                if (node->lhs)
                    node->lhs = new_cast(vm, node->lhs, node->ty->base);
                if (node->rhs)
                    node->rhs = new_cast(vm, node->rhs, node->ty->base);
            } else if (node->val == 1 || node->val == 2) {
                if (!is_complex(node->lhs->ty))
                    node->lhs = new_cast(vm, node->lhs, complex_type_for(vm, node->ty));
            } else if (node->val == 3) {
                if (!is_complex(node->lhs->ty))
                    node->lhs = new_cast(vm, node->lhs, node->ty);
            }
            return;
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
        case ND_MOD:
        case ND_BITAND:
        case ND_BITOR:
        case ND_BITXOR:
            usual_arith_conv(vm, &node->lhs, &node->rhs);
            node->ty = node->lhs->ty;
            return;
        case ND_NEG: {
            Type *ty = get_common_type(vm, ty_int, node->lhs->ty);
            node->lhs = new_cast(vm, node->lhs, ty);
            node->ty = ty;
            return;
        }
        case ND_ASSIGN:
            if (node->lhs->ty->kind == TY_ARRAY) {
                if (vm->collect_errors && error_tok_recover(vm, node->lhs->tok,
                                                             "not an lvalue")) {
                    node->ty = ty_error;
                    return;
                }
                error_tok(vm, node->lhs->tok, "not an lvalue");
            }
            // Check for const-correctness
            // Allow initialization (when initializing_var is set and matches)
            bool is_initialization =
                lhs_targets_initializing_var(node->lhs, vm->compiler.initializing_var);

            if (node->lhs->ty->is_const && !is_initialization) {
                if (vm->collect_errors && error_tok_recover(vm, node->lhs->tok,
                                                             "cannot assign to const-qualified variable")) {
                    node->ty = ty_error;
                    return;
                }
                error_tok(vm, node->lhs->tok, "cannot assign to const-qualified variable");
            }
            if (node->lhs->ty->kind != TY_STRUCT && node->lhs->ty->kind != TY_UNION) {
                warn_implicit_conversion(vm, node->rhs, node->lhs->ty, node->lhs->tok);
                node->rhs = new_cast(vm, node->rhs, node->lhs->ty);
            }
            node->ty = node->lhs->ty;
            return;
        case ND_EQ:
        case ND_NE:
            if (is_complex(node->lhs->ty) || is_complex(node->rhs->ty)) {
                usual_arith_conv(vm, &node->lhs, &node->rhs);
                node->ty = ty_int;
                return;
            }
            check_sign_compare(vm, node->lhs, node->rhs, node->tok);
            usual_arith_conv(vm, &node->lhs, &node->rhs);
            node->ty = ty_int;
            return;
        case ND_LT:
        case ND_LE:
            if (is_complex(node->lhs->ty) || is_complex(node->rhs->ty))
                error_tok(vm, node->tok, "ordered comparison of complex values is not supported");
            check_sign_compare(vm, node->lhs, node->rhs, node->tok);
            usual_arith_conv(vm, &node->lhs, &node->rhs);
            node->ty = ty_int;
            return;
        case ND_FUNCALL:
            node->ty = node->func_ty->return_ty;
            return;
        case ND_NOT:
        case ND_LOGOR:
        case ND_LOGAND:
            node->ty = ty_int;
            return;
        case ND_BITNOT:
        case ND_SHL:
        case ND_SHR:
            node->ty = node->lhs->ty;
            return;
        case ND_VAR:
        case ND_VLA_PTR:
            node->ty = node->var->ty;
            // Function-to-pointer decay: when a function name is used as a value,
            // it decays to a pointer to that function
            if (node->var->ty->kind == TY_FUNC) {
                node->ty = pointer_to(vm, node->var->ty);
            }
            return;
        case ND_COND:
            if (node->then->ty->kind == TY_VOID || node->els->ty->kind == TY_VOID) {
                node->ty = ty_void;
            } else {
                usual_arith_conv(vm, &node->then, &node->els);
                node->ty = node->then->ty;
            }
            return;
        case ND_COMMA:
            node->ty = node->rhs->ty;
            return;
        case ND_MEMBER:
            node->ty = node->member->ty;
            // If the struct/union is const, propagate const to member access
            if (node->lhs && node->lhs->ty && node->lhs->ty->is_const) {
                node->ty = copy_type(vm, node->ty);
                node->ty->is_const = true;
            }
            return;
        case ND_ADDR: {
            Type *ty = node->lhs->ty;
            if (ty->kind == TY_ARRAY)
                node->ty = pointer_to(vm, ty->base);
            else
                node->ty = pointer_to(vm, ty);
            return;
        }
        case ND_DEREF:
            if (!node->lhs->ty->base) {
                if (vm->collect_errors && error_tok_recover(vm, node->tok,
                                                             "invalid pointer dereference")) {
                    node->ty = ty_error;
                    return;
                }
                error_tok(vm, node->tok, "invalid pointer dereference");
            }
            if (node->lhs->ty->base->kind == TY_VOID) {
                if (vm->collect_errors && error_tok_recover(vm, node->tok,
                                                             "dereferencing a void pointer")) {
                    node->ty = ty_error;
                    return;
                }
                error_tok(vm, node->tok, "dereferencing a void pointer");
            }

            // Dereferencing preserves the const-ness of the pointee
            node->ty = node->lhs->ty->base;
            return;
        case ND_STMT_EXPR:
            if (node->body) {
                Node *stmt = node->body;
                while (stmt->next)
                    stmt = stmt->next;
                if (stmt->kind == ND_EXPR_STMT) {
                    node->ty = stmt->lhs->ty;
                    return;
                }
            }
            error_tok(vm, node->tok, "statement expression returning void is not supported");
            return;
        case ND_LABEL_VAL:
            node->ty = pointer_to(vm, ty_void);
            return;
        case ND_CAS:
            add_type(vm, node->cas_addr);
            add_type(vm, node->cas_old);
            add_type(vm, node->cas_new);
            node->ty = ty_bool;

            if (node->cas_addr->ty->kind != TY_PTR)
                error_tok(vm, node->cas_addr->tok, "pointer expected");
            if (node->cas_old->ty->kind != TY_PTR)
                error_tok(vm, node->cas_old->tok, "pointer expected");
            return;
        case ND_EXCH:
            if (node->lhs->ty->kind != TY_PTR)
                error_tok(vm, node->lhs->tok, "pointer expected");
            node->ty = node->lhs->ty->base;
            return;
        case ND_ALOAD:
            add_type(vm, node->lhs);
            if (node->lhs->ty->kind != TY_PTR)
                error_tok(vm, node->lhs->tok, "__builtin_atomic_load: pointer expected");
            node->ty = node->lhs->ty->base;
            return;
        case ND_ASTORE:
            add_type(vm, node->lhs);
            add_type(vm, node->rhs);
            if (node->lhs->ty->kind != TY_PTR)
                error_tok(vm, node->lhs->tok, "__builtin_atomic_store: pointer expected");
            node->ty = node->lhs->ty->base;
            return;
        case ND_BLOCK_LITERAL:
            // Block literal type is already set during parsing (TY_BLOCK)
            // If not set, infer from block_fn
            if (!node->ty && node->block_fn && node->block_fn->ty) {
                node->ty = block_type(vm, node->block_fn->ty->return_ty,
                                      node->block_fn->ty->params);
            }
            return;
        case ND_BLOCK_CALL:
            // Block call returns the block's return type
            add_type(vm, node->lhs);
            if (node->lhs->ty && node->lhs->ty->kind == TY_BLOCK) {
                node->ty = node->lhs->ty->return_ty ? node->lhs->ty->return_ty : ty_void;
            } else {
                node->ty = ty_int;  // Fallback
            }
            return;
        default:
            return;
    }
}
