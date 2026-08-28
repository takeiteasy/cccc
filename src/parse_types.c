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

// Declarator and type grammar: declaration specifiers, pointers,
// declarators, array/function suffixes, struct/union/enum declarations,
// and the __attribute__/[[...]] attribute-application subsystem that
// declarator parsing drives.

#include "./parse_internal.h"

DeclKw declspec_kw(Token *tok) {
    const char *s = tok->loc;
    switch (tok->len) {
        case 3: // int
            if (s[0] == 'i' && s[1] == 'n' && s[2] == 't')
                return DK_INT;
            break;
        case 4: // auto, bool, char, enum, long, void
            switch (s[0]) {
                case 'a':
                    if (s[1] == 'u' && s[2] == 't' && s[3] == 'o')
                        return DK_AUTO;
                    break;
                // bool is only a keyword in C23; below C23 it is downgraded to
                // TK_IDENT in convert_pp_tokens so it can be used as an
                // identifier.
                case 'b':
                    if (s[1] == 'o' && s[2] == 'o' && s[3] == 'l')
                        return tok->kind == TK_KEYWORD ? DK_BOOL : DK_NONE;
                    break;
                case 'c':
                    if (s[1] == 'h' && s[2] == 'a' && s[3] == 'r')
                        return DK_CHAR;
                    break;
                case 'e':
                    if (s[1] == 'n' && s[2] == 'u' && s[3] == 'm')
                        return DK_ENUM;
                    break;
                case 'l':
                    if (s[1] == 'o' && s[2] == 'n' && s[3] == 'g')
                        return DK_LONG;
                    break;
                case 'v':
                    if (s[1] == 'o' && s[2] == 'i' && s[3] == 'd')
                        return DK_VOID;
                    break;
            }
            break;
        case 5: // _Bool, const, float, short, union
            switch (s[0]) {
                case '_':
                    if (memcmp(s + 1, "Bool", 4) == 0)
                        return DK_BOOL;
                    break;
                case 'c':
                    if (memcmp(s + 1, "onst", 4) == 0)
                        return DK_CONST;
                    break;
                case 'f':
                    if (memcmp(s + 1, "loat", 4) == 0)
                        return DK_FLOAT;
                    break;
                case 's':
                    if (memcmp(s + 1, "hort", 4) == 0)
                        return DK_SHORT;
                    break;
                case 'u':
                    if (memcmp(s + 1, "nion", 4) == 0)
                        return DK_UNION;
                    break;
            }
            break;
        case 6: // double, extern, inline, signed, static, struct, typeof
            switch (s[0]) {
                case 'd':
                    if (memcmp(s + 1, "ouble", 5) == 0)
                        return DK_DOUBLE;
                    break;
                case 'e':
                    if (memcmp(s + 1, "xtern", 5) == 0)
                        return DK_EXTERN;
                    break;
                case 'i':
                    if (memcmp(s + 1, "nline", 5) == 0)
                        return DK_INLINE;
                    break;
                case 's':
                    if (memcmp(s + 1, "igned", 5) == 0)
                        return DK_SIGNED;
                    if (memcmp(s + 1, "tatic", 5) == 0)
                        return DK_STATIC;
                    if (memcmp(s + 1, "truct", 5) == 0)
                        return DK_STRUCT;
                    break;
                case 't':
                    if (memcmp(s + 1, "ypeof", 5) == 0)
                        return DK_TYPEOF;
                    break;
            }
            break;
        case 7: // _Atomic, __block, _BitInt, typedef
            if (s[0] == '_') {
                if (s[1] == 'A' && memcmp(s + 2, "tomic", 5) == 0)
                    return DK_ATOMIC;
                if (s[1] == '_' && memcmp(s + 2, "block", 5) == 0)
                    return DK_BLOCK_VAR;
                if (s[1] == 'B' && memcmp(s + 2, "itInt", 5) == 0)
                    return DK_BITINT;
            } else if (s[0] == 't' && memcmp(s + 1, "ypedef", 6) == 0) {
                return DK_TYPEDEF;
            }
            break;
        case 8: // _Alignas, _Complex, __thread, register, restrict, unsigned,
                // volatile
            switch (s[0]) {
                case '_':
                    switch (s[1]) {
                        case 'A':
                            if (memcmp(s + 2, "lignas", 6) == 0)
                                return DK_ALIGNAS;
                            break;
                        case 'C':
                            if (memcmp(s + 2, "omplex", 6) == 0)
                                return DK_COMPLEX;
                            break;
                        case '_':
                            if (memcmp(s + 2, "thread", 6) == 0)
                                return DK_TLS;
                            break;
                    }
                    break;
                case 'r':
                    if (memcmp(s + 1, "egister", 7) == 0)
                        return DK_REGISTER;
                    if (memcmp(s + 1, "estrict", 7) == 0)
                        return DK_RESTRICT;
                    break;
                case 'u':
                    if (memcmp(s + 1, "nsigned", 7) == 0)
                        return DK_UNSIGNED;
                    break;
                case 'v':
                    if (memcmp(s + 1, "olatile", 7) == 0)
                        return DK_VOLATILE;
                    break;
            }
            // __int128 — GNU 128-bit signed integer (combines with
            // signed/unsigned)
            if (memcmp(s, "__int128", 8) == 0)
                return DK_INT128;
            break;
        case 9: // _Noreturn, constexpr
            switch (s[0]) {
                case '_':
                    if (memcmp(s + 1, "Noreturn", 8) == 0)
                        return DK_NORETURN;
                    break;
                // constexpr is only a keyword in C23; below C23 it is
                // downgraded to TK_IDENT in convert_pp_tokens so it can be used
                // as an identifier.
                case 'c':
                    if (memcmp(s + 1, "onstexpr", 8) == 0)
                        return tok->kind == TK_KEYWORD ? DK_CONSTEXPR : DK_NONE;
                    break;
            }
            break;
        case 10: // __restrict, _Imaginary, _Decimal32, _Decimal64, __int128_t
            if (s[0] == '_') {
                if (s[1] == '_' && memcmp(s + 2, "restrict", 8) == 0)
                    return DK_RESTRICT;
                if (s[1] == 'I' && memcmp(s + 2, "maginary", 8) == 0)
                    return DK_IMAGINARY;
                if (s[1] == 'D' && memcmp(s + 2, "ecimal32", 8) == 0)
                    return DK_DECIMAL32;
                if (s[1] == 'D' && memcmp(s + 2, "ecimal64", 8) == 0)
                    return DK_DECIMAL64;
                if (s[1] == '_' && memcmp(s + 2, "int128_t", 8) == 0)
                    return DK_INT128;
            }
            break;
        case 11: // _Decimal128, __uint128_t
            if (s[0] == '_' && s[1] == 'D' &&
                memcmp(s + 2, "ecimal128", 9) == 0)
                return DK_DECIMAL128;
            if (s[0] == '_' && s[1] == '_' &&
                memcmp(s + 2, "uint128_t", 9) == 0)
                return DK_INT128;
            break;
        case 12: // __restrict__, thread_local
            if (memcmp(s, "__restrict__", 12) == 0)
                return DK_RESTRICT;
            if (memcmp(s, "thread_local", 12) == 0)
                return tok->kind == TK_KEYWORD ? DK_TLS : DK_NONE;
            break;
        case 13: // _Thread_local, typeof_unqual
            switch (s[0]) {
                case '_':
                    if (memcmp(s + 1, "Thread_local", 12) == 0)
                        return DK_TLS;
                    break;
                case 't':
                    if (memcmp(s + 1, "ypeof_unqual", 12) == 0)
                        return DK_TYPEOF_UNQUAL;
                    break;
            }
            break;
    }
    return DK_NONE;
}

static Type *enum_specifier(VirtualMachine *vm, Token **rest, Token *tok);
static Type *struct_decl(VirtualMachine *vm, Token **rest, Token *tok);
static Type *typeof_specifier(VirtualMachine *vm, Token **rest, Token *tok);
static Type *typeof_unqual_specifier(VirtualMachine *vm, Token **rest,
                                     Token *tok);
static Type *union_decl(VirtualMachine *vm, Token **rest, Token *tok);

// declspec = ("void" | "_Bool" | "char" | "short" | "int" | "long"
//             | "typedef" | "static" | "extern" | "inline"
//             | "_Thread_local" | "__thread"
//             | "signed" | "unsigned"
//             | struct-decl | union-decl | typedef-name
//             | enum-specifier | typeof-specifier
//             | "const" | "volatile" | "auto" | "register" | "restrict"
//             | "__restrict" | "__restrict__" | "_Noreturn")+
//
// The order of typenames in a type-specifier doesn't matter. For
// example, `int long static` means the same as `static long int`.
// That can also be written as `static long` because you can omit
// `int` if `long` or `short` are specified. However, something like
// `char int` is not a valid type specifier. We have to accept only a
// limited combinations of the typenames.
//
// In this function, we count the number of occurrences of each typename
// while keeping the "current" type object that the typenames up
// until that point represent. When we reach a non-typename token,
// we returns the current type object.
Type *declspec(VirtualMachine *vm, Token **rest, Token *tok, VarAttr *attr) {
    Token *start = tok;

    // We use a single integer as counters for all typenames.
    // For example, bits 0 and 1 represents how many times we saw the
    // keyword "void" so far. With this, we can use a switch statement
    // as you can see below.
    enum {
        VOID      = 1 << 0,
        BOOL      = 1 << 2,
        CHAR      = 1 << 4,
        SHORT     = 1 << 6,
        INT       = 1 << 8,
        LONG      = 1 << 10,
        FLOAT     = 1 << 12,
        DOUBLE    = 1 << 14,
        OTHER     = 1 << 16,
        SIGNED    = 1 << 17,
        UNSIGNED  = 1 << 18,
        COMPLEX   = 1 << 19,
        IMAGINARY = 1 << 20,
    };

    Type *ty           = ty_int;
    int   counter      = 0;
    int   bitint_width = 0;
    bool  is_atomic    = false;
    bool  is_const     = false;
    bool  is_volatile  = false;

    while (is_typename(vm, tok) || equal(tok, "__attribute__") ||
           (equal(tok, "[") && equal(tok->next, "["))) {
        if (equal(tok, "__attribute__")) {
            tok = attribute_list(vm, tok, NULL, attr);
            continue;
        }
        if (equal(tok, "[") && equal(tok->next, "[")) {
            tok = c23_attribute_list(vm, tok, NULL, attr);
            continue;
        }

        DeclKw dk = declspec_kw(tok);
        switch (dk) {
            case DK_TYPEDEF:
            case DK_STATIC:
            case DK_EXTERN:
            case DK_INLINE:
            case DK_TLS:
            case DK_CONSTEXPR:
            case DK_BLOCK_VAR:
                if (!attr)
                    error_tok(vm, tok,
                              "storage class specifier is not allowed in this "
                              "context");
                if (dk == DK_TYPEDEF)
                    attr->is_typedef = true;
                else if (dk == DK_STATIC)
                    attr->is_static = true;
                else if (dk == DK_EXTERN)
                    attr->is_extern = true;
                else if (dk == DK_INLINE) {
                    if (vm->compiler.c_std < CCCC_STD_C99)
                        error_tok(vm, tok,
                                  "'inline' is not available before C99");
                    attr->is_inline = true;
                } else if (dk == DK_CONSTEXPR)
                    attr->is_constexpr = true;
                else if (dk == DK_BLOCK_VAR)
                    attr->is_block_var = true;
                else {
                    attr->is_tls = true;
                }
                if (attr->is_typedef && attr->is_static + attr->is_extern +
                                                attr->is_inline + attr->is_tls >
                                            1)
                    error_tok(vm, tok,
                              "typedef may not be used together with static,"
                              " extern, inline, __thread or _Thread_local");
                if (attr->is_typedef && attr->is_constexpr)
                    error_tok(
                        vm, tok,
                        "typedef may not be used together with constexpr");
                if (attr->is_block_var &&
                    (attr->is_static || attr->is_extern || attr->is_tls))
                    error_tok(vm, tok,
                              "__block may not be used together with static,"
                              " extern, __thread or _Thread_local");
                tok = tok->next;
                continue;
            case DK_CONST:
                is_const = true;
                tok      = tok->next;
                continue;
            case DK_VOLATILE:
                is_volatile = true;
                tok         = tok->next;
                continue;
            case DK_AUTO:
                if (vm->compiler.c_std >= CCCC_STD_C23) {
                    if (counter != 0)
                        error_tok(
                            vm, tok,
                            "cannot combine 'auto' with other type specifiers");
                    if (attr)
                        attr->is_auto = true;
                    ty      = ty_auto;
                    counter = OTHER;
                }
                tok = tok->next;
                continue;
            case DK_REGISTER:
            case DK_RESTRICT:
                tok = tok->next;
                continue;
            case DK_NORETURN:
                attr->is_noreturn = true;
                tok               = tok->next;
                continue;
            case DK_ATOMIC:
                warn_tok(vm, tok, CCCC_WARN_IGNORED_FEATURES,
                         "'_Atomic' is parsed but non-atomic — "
                         "loads and stores are not atomic");
                tok = tok->next;
                if (equal(tok, "(")) {
                    ty      = typename(vm, &tok, tok->next);
                    tok     = skip(vm, tok, ")");
                    counter = OTHER;
                }
                is_atomic = true;
                continue;
            case DK_ALIGNAS:
                if (!attr)
                    error_tok(vm, tok,
                              "_Alignas is not allowed in this context");
                tok = skip(vm, tok->next, "(");
                if (is_typename(vm, tok))
                    attr->align = typename(vm, &tok, tok)->align;
                else
                    attr->align = const_expr(vm, &tok, tok);
                tok = skip(vm, tok, ")");
                continue;
            case DK_BITINT: {
                // _BitInt(N) — must be C23; unsigned/signed must precede
                // _BitInt
                Token *bitint_tok = tok;
                tok               = tok->next; // consume _BitInt
                tok               = skip(vm, tok, "(");
                bitint_width      = const_expr(vm, &tok, tok);
                tok               = skip(vm, tok, ")");
                ty                = bitint_type(vm, bitint_tok, bitint_width,
                                                (bool)(counter & UNSIGNED));
                counter           = OTHER;
                continue;
            }
            case DK_INT128: {
                // GNU __int128 / __int128_t / __uint128_t, mapped onto
                // _BitInt(128).
                // __uint128_t is always unsigned; __int128 honours a preceding
                // signed/unsigned specifier; __int128_t is always signed.
                bool is_unsigned;
                if (tok->len == 11)      // __uint128_t
                    is_unsigned = true;
                else if (tok->len == 10) // __int128_t
                    is_unsigned = false;
                else                     // __int128 [+ signed/unsigned]
                    is_unsigned = (counter & UNSIGNED) != 0;
                ty      = bitint_type(vm, tok, 128, is_unsigned);
                tok     = tok->next;
                counter = OTHER;
                continue;
            }
            case DK_STRUCT:
            case DK_UNION:
            case DK_ENUM:
            case DK_TYPEOF:
            case DK_TYPEOF_UNQUAL:
            case DK_NONE:
            case DK_DECIMAL32:
            case DK_DECIMAL64:
            case DK_DECIMAL128: {
                Type *ty2 = (dk == DK_NONE) ? find_typedef(vm, tok) : NULL;
                if (counter)
                    goto declspec_done;
                if (dk == DK_STRUCT)
                    ty = struct_decl(vm, &tok, tok->next);
                else if (dk == DK_UNION)
                    ty = union_decl(vm, &tok, tok->next);
                else if (dk == DK_ENUM)
                    ty = enum_specifier(vm, &tok, tok->next);
                else if (dk == DK_TYPEOF)
                    ty = typeof_specifier(vm, &tok, tok->next);
                else if (dk == DK_TYPEOF_UNQUAL)
                    ty = typeof_unqual_specifier(vm, &tok, tok->next);
                else if (dk == DK_DECIMAL32) {
                    ty  = ty_decimal32;
                    tok = tok->next;
                } else if (dk == DK_DECIMAL64) {
                    ty  = ty_decimal64;
                    tok = tok->next;
                } else if (dk == DK_DECIMAL128) {
                    ty  = ty_decimal128;
                    tok = tok->next;
                } else {
                    VarScope *sc = find_var(vm, tok);
                    if (!vm->compiler.in_type_lookahead && sc &&
                        sc->is_deprecated)
                        warn_deprecated_use(
                            vm, tok, arena_strndup(vm, tok->loc, tok->len),
                            sc->deprecated_msg);
                    ty  = sc && sc->is_deprecated
                              ? type_after_deprecated_use(vm, ty2)
                              : ty2;
                    tok = tok->next;
                }
                counter += OTHER;
                continue;
            }
            case DK_VOID:
                counter += VOID;
                break;
            case DK_BOOL:
                counter += BOOL;
                break;
            case DK_CHAR:
                counter += CHAR;
                break;
            case DK_SHORT:
                counter += SHORT;
                break;
            case DK_INT:
                counter += INT;
                break;
            case DK_LONG:
                if ((counter & LONG) && vm->compiler.c_std < CCCC_STD_C99 &&
                    !vm->compiler.in_type_lookahead)
                    warn_tok(vm, tok, CCCC_WARN_PEDANTIC,
                             "'long long' is a C99 extension");
                counter += LONG;
                break;
            case DK_FLOAT:
                counter += FLOAT;
                break;
            case DK_DOUBLE:
                counter += DOUBLE;
                break;
            case DK_COMPLEX:
                counter += COMPLEX;
                break;
            case DK_IMAGINARY:
                counter += IMAGINARY;
                break;
            case DK_SIGNED:
                counter |= SIGNED;
                break;
            case DK_UNSIGNED:
                counter |= UNSIGNED;
                break;
            default:
                unreachable();
        }

        switch (counter) {
            case VOID:
                ty = ty_void;
                break;
            case BOOL:
                ty = ty_bool;
                break;
            case CHAR:
            case SIGNED + CHAR:
                ty = ty_char;
                break;
            case UNSIGNED + CHAR:
                ty = ty_uchar;
                break;
            case SHORT:
            case SHORT + INT:
            case SIGNED + SHORT:
            case SIGNED + SHORT + INT:
                ty = ty_short;
                break;
            case UNSIGNED + SHORT:
            case UNSIGNED + SHORT + INT:
                ty = ty_ushort;
                break;
            case INT:
            case SIGNED:
            case SIGNED + INT:
                ty = ty_int;
                break;
            case UNSIGNED:
            case UNSIGNED + INT:
                ty = ty_uint;
                break;
            case LONG:
            case LONG + INT:
            case LONG + LONG:
            case LONG + LONG + INT:
            case SIGNED + LONG:
            case SIGNED + LONG + INT:
            case SIGNED + LONG + LONG:
            case SIGNED + LONG + LONG + INT:
                ty = ty_long;
                break;
            case UNSIGNED + LONG:
            case UNSIGNED + LONG + INT:
            case UNSIGNED + LONG + LONG:
            case UNSIGNED + LONG + LONG + INT:
                ty = ty_ulong;
                break;
            case FLOAT:
                ty = ty_float;
                break;
            case FLOAT + COMPLEX:
            case FLOAT + IMAGINARY:
                ty = ty_fcomplex;
                break;
            case DOUBLE:
                ty = ty_double;
                break;
            case DOUBLE + COMPLEX:
            case DOUBLE + IMAGINARY:
            case COMPLEX:
            case IMAGINARY:
                ty = ty_dcomplex;
                break;
            case LONG + DOUBLE:
                ty = ty_ldouble;
                break;
            case LONG + DOUBLE + COMPLEX:
            case LONG + DOUBLE + IMAGINARY:
                ty = ty_ldcomplex;
                break;
            default:
                error_tok(vm, tok, "invalid type");
        }

        tok = tok->next;
    }
declspec_done:
    if (counter == 0 && !vm->compiler.in_type_lookahead)
        warn_tok(vm, start, CCCC_WARN_IMPLICIT_INT,
                 "type specifier missing, defaults to 'int'");

    if (attr && (attr->is_maybe_unused || attr->is_deprecated)) {
        ty                  = copy_type(vm, ty);
        ty->is_maybe_unused = attr->is_maybe_unused;
        ty->is_deprecated   = attr->is_deprecated;
        ty->deprecated_msg  = attr->deprecated_msg;
    }

    if (is_atomic) {
        ty            = copy_type(vm, ty);
        ty->is_atomic = true;
    }

    if (is_const) {
        ty           = copy_type(vm, ty);
        ty->is_const = true;
    }

    if (is_volatile) {
        ty              = copy_type(vm, ty);
        ty->is_volatile = true;
    }

    if (attr && attr->is_constexpr) {
        ty           = copy_type(vm, ty);
        ty->is_const = true;
    }

    *rest = tok;
    return ty;
}

// func-params = ("void" | param ("," param)* ("," "...")?)? ")"
// param       = declspec declarator
static Type *func_params(VirtualMachine *vm, Token **rest, Token *tok,
                         Type *ty) {
    if (equal(tok, "void") && equal(tok->next, ")")) {
        *rest = tok->next->next;
        return func_type(vm, ty);
    }

    Type  head        = {};
    Type *cur         = &head;
    bool  is_variadic = false;

    // Open a temporary prototype scope so that each parameter is visible to
    // subsequent parameters' VLA size expressions (C99 §6.7.6.3p12).
    // e.g. void f(int n, int a[n]) — 'n' must be in scope when parsing a[n].
    enter_scope(vm);

    while (!equal(tok, ")")) {
        if (cur != &head)
            tok = skip(vm, tok, ",");

        if (equal(tok, "...")) {
            is_variadic = true;
            tok         = tok->next;
            skip(vm, tok, ")");
            break;
        }

        VarAttr attr = {};
        Type   *ty2  = declspec(vm, &tok, tok, &attr);
        ty2          = declarator(vm, &tok, tok, ty2);
        ty2          = apply_var_attrs_to_type(vm, ty2, &attr);
        if (has_custom_attrs(ty2, &attr))
            error_tok(vm, ty2->name ? ty2->name : tok,
                      "custom attributes are only supported on file-scope "
                      "declarations");

        Token *name = ty2->name;

        if (ty2->kind == TY_ARRAY) {
            // "array of T" is converted to "pointer to T" only in the parameter
            // context. For example, *argv[] is converted to **argv by this.
            // Qualifiers inside [...] apply to the resulting pointer per C99
            // §6.7.6.3p7.
            int  saved_static_min  = ty2->static_min;
            bool saved_is_const    = ty2->is_const;
            bool saved_is_volatile = ty2->is_volatile;
            bool saved_is_restrict = ty2->is_restrict;
            ty2                    = pointer_to(vm, ty2->base);
            ty2->name              = name;
            ty2->static_min        = saved_static_min;
            ty2->is_const          = saved_is_const;
            ty2->is_volatile       = saved_is_volatile;
            ty2->is_restrict       = saved_is_restrict;
        } else if (ty2->kind == TY_VLA) {
            // VLA parameters also adjust to pointer-to-element (C99
            // §6.7.6.3p7). Qualifiers from the brackets transfer to the
            // resulting pointer.
            bool saved_is_const    = ty2->is_const;
            bool saved_is_volatile = ty2->is_volatile;
            bool saved_is_restrict = ty2->is_restrict;
            ty2                    = pointer_to(vm, ty2->base);
            ty2->name              = name;
            ty2->is_const          = saved_is_const;
            ty2->is_volatile       = saved_is_volatile;
            ty2->is_restrict       = saved_is_restrict;
        } else if (ty2->kind == TY_FUNC) {
            // Likewise, a function is converted to a pointer to a function
            // only in the parameter context.
            ty2       = pointer_to(vm, ty2);
            ty2->name = name;
        }

        // Register this parameter in the prototype scope so subsequent
        // parameters can reference it in VLA dimension expressions.
        if (name) {
            Obj *dummy = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
            memset(dummy, 0, sizeof(Obj));
            dummy->ty                                 = ty2;
            dummy->align                              = ty2->align;
            dummy->is_local                           = true;
            push_scope(vm, name->loc, name->len)->var = dummy;
        }

        cur = cur->next = copy_type(vm, ty2);
    }

    leave_scope(vm);

    if (cur == &head) {
        if (vm->compiler.c_std < CCCC_STD_C23) {
            is_variadic = true;
            if (!vm->compiler.in_type_lookahead)
                warn_tok(vm, tok, CCCC_WARN_STRICT_PROTOTYPES,
                         "function declaration is not a prototype");
        }
        // C23: empty () is a prototype accepting no args, identical to (void).
    }

    ty              = func_type(vm, ty);
    ty->params      = head.next;
    ty->is_variadic = is_variadic;
    *rest           = tok->next;
    return ty;
}

static Type *type_suffix(VirtualMachine *vm, Token **rest, Token *tok,
                         Type *ty);

// array-dimensions = ("static" | "restrict" | "const" | "volatile" |
// "_Atomic")* const-expr? "]" type-suffix
static Type *array_dimensions(VirtualMachine *vm, Token **rest, Token *tok,
                              Type *ty) {
    bool saw_static   = false;
    bool saw_const    = false;
    bool saw_volatile = false;
    bool saw_restrict = false;
    while (equal(tok, "static") || equal(tok, "restrict") ||
           equal(tok, "const") || equal(tok, "volatile") ||
           equal(tok, "_Atomic")) {
        if (equal(tok, "static"))
            saw_static = true;
        if (equal(tok, "const"))
            saw_const = true;
        if (equal(tok, "volatile"))
            saw_volatile = true;
        if (equal(tok, "restrict"))
            saw_restrict = true;
        tok = tok->next;
    }

    if (equal(tok, "]")) {
        ty        = type_suffix(vm, rest, tok->next, ty);
        Type *arr = array_of(vm, ty, -1);
        if (saw_const)
            arr->is_const = true;
        if (saw_volatile)
            arr->is_volatile = true;
        if (saw_restrict)
            arr->is_restrict = true;
        return arr;
    }

    Token *expr_tok = tok;
    Node  *expr     = conditional(vm, &tok, tok);
    tok             = skip(vm, tok, "]");
    ty              = type_suffix(vm, rest, tok, ty);

    if (ty->kind == TY_VLA || !is_const_expr(vm, expr)) {
        if (vm->compiler.c_std < CCCC_STD_C99)
            warn_tok(vm, expr_tok, CCCC_WARN_PEDANTIC,
                     "variable-length arrays are a C99 extension");
        return vla_of(vm, ty, expr);
    }
    // #1095: capture layout provenance from the *unfolded* node before
    // array_of()'s own eval(vm, expr) discards it -- see
    // node_layout_const()'s own comment (parse_analysis.c). Serialization-
    // only: arr->array_len below is still the plain folded int, unchanged.
    Type *layout_ty       = NULL;
    bool  layout_is_align = false;
    node_layout_const(expr, &layout_ty, &layout_is_align);
    Type *arr                      = array_of(vm, ty, eval(vm, expr));
    arr->array_len_layout_ty       = layout_ty;
    arr->array_len_layout_is_align = layout_is_align;
    if (saw_static)
        arr->static_min = arr->array_len;
    if (saw_const)
        arr->is_const = true;
    if (saw_volatile)
        arr->is_volatile = true;
    if (saw_restrict)
        arr->is_restrict = true;
    return arr;
}

// type-suffix = "(" func-params
//             | "[" array-dimensions
//             | ε
static Type *type_suffix(VirtualMachine *vm, Token **rest, Token *tok,
                         Type *ty) {
    if (equal(tok, "("))
        return func_params(vm, rest, tok->next, ty);

    if (equal(tok, "[") && !equal(tok->next, "["))
        return array_dimensions(vm, rest, tok->next, ty);

    *rest = tok;
    return ty;
}

static bool is_asm_label_tok(Token *tok) {
    return equal(tok, "asm") || equal(tok, "__asm") || equal(tok, "__asm__");
}

Token *asm_label(VirtualMachine *vm, Token *tok, char **label) {
    if (!is_asm_label_tok(tok))
        return tok;

    tok = skip(vm, tok->next, "(");
    if (tok->kind != TK_STR || !tok->ty || !tok->ty->base ||
        tok->ty->base->kind != TY_CHAR)
        error_tok(vm, tok, "expected string literal in asm label");
    if (label)
        *label = arena_strdup(vm, tok->str);
    return skip(vm, tok->next, ")");
}

// pointers = ("*" ("const" | "volatile" | "restrict" |
//                   checked-pointer-attribute)*)*
static Type *pointers(VirtualMachine *vm, Token **rest, Token *tok, Type *ty) {
    while (consume(vm, &tok, tok, "*")) {
        ty = pointer_to(vm, ty);

        // Checked-pointer attributes (#770/#482-484) attach here, in the
        // same grammar position as const/volatile/restrict below -- the only
        // spelling that unambiguously qualifies the pointer just built
        // rather than its pointee (see the CheckedKind comment in cccc.h).
        // Both dispatchers no-op (return tok unchanged) when nothing of
        // theirs is present, so calling them unconditionally is cheap; the
        // recognized names are only handled here since this is the only
        // call site that passes a TY_PTR ty, and apply_checked_ptr_attr()
        // errors on any other position.
        tok = attribute_list(vm, tok, ty, NULL);
        tok = c23_attribute_list(vm, tok, ty, NULL);

        // A bounds form (count/byte_count/bounds) only makes sense paired
        // with a checked kind (array/ntarray); checked here rather than in
        // apply_checked_ptr_attr() itself so attribute order within the
        // bracket list doesn't matter (`[[cccc::count(n), cccc::array]]` is
        // as valid as the more natural `[[cccc::array, cccc::count(n)]]`).
        if (ty->checked_bounds_form != CB_NONE) {
            if (ty->checked_kind == CHECKED_NONE)
                error_tok(vm, tok,
                          "a bounds declaration (count/byte_count/bounds) "
                          "requires the pointer to also be declared "
                          "[[cccc::array]] or [[cccc::ntarray]]");
            if (ty->checked_kind == CHECKED_SINGLE)
                error_tok(vm, tok,
                          "a [[cccc::single]] pointer cannot have a bounds "
                          "declaration -- it always refers to exactly one "
                          "object");
        }

        // Handle const/volatile qualification on the pointer itself
        // Example: "int *const p" makes the pointer const, not the pointee
        // Example: "int *volatile p" makes the pointer volatile
        while (equal(tok, "const") || equal(tok, "volatile") ||
               equal(tok, "restrict") || equal(tok, "__restrict") ||
               equal(tok, "__restrict__")) {
            if (equal(tok, "const")) {
                ty           = copy_type(vm, ty);
                ty->is_const = true;
            } else if (equal(tok, "volatile")) {
                ty              = copy_type(vm, ty);
                ty->is_volatile = true;
            } else {
                ty              = copy_type(vm, ty);
                ty->is_restrict = true;
            }
            tok = tok->next;
        }
    }
    *rest = tok;
    return ty;
}

static void inherit_semantic_attrs(Type *dst, Type *src);

// declarator = attribute? pointers ("(" ident ")" | "(" declarator ")" | ident)
// type-suffix attribute?
Type *declarator(VirtualMachine *vm, Token **rest, Token *tok, Type *ty) {
    // Handle __attribute__ before declarator
    VarAttr prefix_attr = {};
    tok                 = attribute_list(vm, tok, NULL, &prefix_attr);
    tok                 = c23_attribute_list(vm, tok, NULL, &prefix_attr);
    append_custom_attr_list(&ty->custom_attrs, prefix_attr.custom_attrs);
    ty = apply_var_attrs_to_type(vm, ty, &prefix_attr);

    ty = pointers(vm, &tok, tok, ty);

    // Handle block type: int (^name)(params)
    // The ^ indicates this is a block type, not a function pointer
    if (equal(tok, "(") && equal(tok->next, "^")) {
        // Token *start = tok;
        tok             = tok->next->next; // Skip '(' and '^'

        Token *name     = NULL;
        Token *name_pos = tok;

        if (tok->kind == TK_IDENT) {
            name = tok;
            tok  = tok->next;
        }

        tok = skip(vm, tok, ")");

        // Now parse the parameter list: (params)
        // This creates the function signature that the block will have
        Type *func_ty = type_suffix(vm, rest, tok, ty);

        // Create a block type instead of function pointer
        Type *block_ty = block_type(vm, func_ty->return_ty, func_ty->params);
        inherit_semantic_attrs(block_ty, ty);
        block_ty->name     = name;
        block_ty->name_pos = name_pos;

        return block_ty;
    }

    if (equal(tok, "(")) {
        Token *start = tok;
        Type   dummy = {};
        declarator(vm, &tok, start->next, &dummy);
        tok   = skip(vm, tok, ")");
        ty    = type_suffix(vm, rest, tok, ty);
        *rest = asm_label(vm, *rest, &ty->asm_label);
        return declarator(vm, &tok, start->next, ty);
    }

    Token *name     = NULL;
    Token *name_pos = tok;

    if (tok->kind == TK_IDENT) {
        name = tok;
        tok  = tok->next;
    }

    Type *inner_ty = ty;
    ty             = type_suffix(vm, rest, tok, ty);
    inherit_semantic_attrs(ty, inner_ty);

    // Propagate noreturn from prefix __attribute__ to function type
    if (prefix_attr.is_noreturn && ty->kind == TY_FUNC)
        ty->is_noreturn = true;

    // Propagate nodiscard from prefix [[nodiscard]] to function type
    if (prefix_attr.is_nodiscard && ty->kind == TY_FUNC) {
        ty->is_nodiscard = true;
        if (prefix_attr.nodiscard_msg)
            ty->nodiscard_msg = prefix_attr.nodiscard_msg;
    }

    // Handle __attribute__ after declarator
    VarAttr suffix_attr = {};
    tok                 = attribute_list(vm, *rest, NULL, &suffix_attr);
    tok                 = c23_attribute_list(vm, tok, NULL, &suffix_attr);
    append_custom_attr_list(&ty->custom_attrs, suffix_attr.custom_attrs);
    ty = apply_var_attrs_to_type(vm, ty, &suffix_attr);

    // #1160: __attribute__((aligned(N))) / [[gnu::aligned(N)]] in
    // declarator-suffix position (`int b __attribute__((aligned(16)));`) --
    // apply_var_attrs_to_type() above doesn't know about gnu_align, so
    // apply it here. copy_type() is what stops it from leaking onto a
    // sibling declarator sharing this basety (`int b
    // __attribute__((aligned(16))), c;` -- gcc-16 verified `c` stays at its
    // natural offset).
    if (suffix_attr.gnu_align) {
        ty = copy_type(vm, ty);
        if (suffix_attr.gnu_align > ty->decl_align)
            ty->decl_align = suffix_attr.gnu_align;
    }

    tok          = asm_label(vm, tok, &ty->asm_label);

    ty->name     = name;
    ty->name_pos = name_pos;
    *rest        = tok;
    return ty;
}

// abstract-declarator = attribute? pointers ("(" abstract-declarator ")")?
// type-suffix attribute?
Type *abstract_declarator(VirtualMachine *vm, Token **rest, Token *tok,
                          Type *ty) {
    // Handle __attribute__ before abstract declarator
    VarAttr prefix_attr = {};
    tok                 = attribute_list(vm, tok, NULL, &prefix_attr);
    tok                 = c23_attribute_list(vm, tok, NULL, &prefix_attr);
    ty                  = apply_var_attrs_to_type(vm, ty, &prefix_attr);

    ty                  = pointers(vm, &tok, tok, ty);

    // Handle block type: int (^)(params) in abstract declarators (for casts)
    if (equal(tok, "(") && equal(tok->next, "^")) {
        tok = tok->next->next; // Skip '(' and '^'
        tok = skip(vm, tok, ")");

        // Parse the parameter list
        Type *func_ty = type_suffix(vm, rest, tok, ty);

        // Create a block type
        return block_type(vm, func_ty->return_ty, func_ty->params);
    }

    if (equal(tok, "(")) {
        Token *start = tok;
        Type   dummy = {};
        abstract_declarator(vm, &tok, start->next, &dummy);
        tok = skip(vm, tok, ")");
        ty  = type_suffix(vm, rest, tok, ty);
        return abstract_declarator(vm, &tok, start->next, ty);
    }

    return type_suffix(vm, rest, tok, ty);
}

// type-name = declspec abstract-declarator
Type *typename(VirtualMachine *vm, Token **rest, Token *tok) {
    Type *ty = declspec(vm, &tok, tok, NULL);
    return abstract_declarator(vm, rest, tok, ty);
}

bool is_end(Token *tok) {
    return equal(tok, "}") || (equal(tok, ",") && equal(tok->next, "}"));
}

bool consume_end(Token **rest, Token *tok) {
    if (equal(tok, "}")) {
        *rest = tok->next;
        return true;
    }

    if (equal(tok, ",") && equal(tok->next, "}")) {
        *rest = tok->next->next;
        return true;
    }

    return false;
}

static bool same_optional_name(Token *a, Token *b) {
    if (!a || !b)
        return a == b;
    return a->len == b->len && strncmp(a->loc, b->loc, a->len) == 0;
}

static bool same_type_exact(Type *a, Type *b) {
    if (a == b)
        return true;
    if (!a || !b || a->kind != b->kind || a->is_unsigned != b->is_unsigned ||
        a->is_atomic != b->is_atomic || a->is_const != b->is_const ||
        a->is_volatile != b->is_volatile || a->is_restrict != b->is_restrict ||
        a->checked_kind != b->checked_kind)
        return false;

    switch (a->kind) {
        case TY_PTR:
            return same_type_exact(a->base, b->base);
        case TY_ARRAY:
            return a->array_len == b->array_len &&
                   same_type_exact(a->base, b->base);
        case TY_FUNC: {
            if (!same_type_exact(a->return_ty, b->return_ty) ||
                a->is_variadic != b->is_variadic)
                return false;
            Type *pa = a->params;
            Type *pb = b->params;
            for (; pa && pb; pa = pa->next, pb = pb->next)
                if (!same_type_exact(pa, pb))
                    return false;
            return !pa && !pb;
        }
        case TY_STRUCT:
        case TY_UNION:
        case TY_ENUM:
            return a == b ||
                   (a->name && b->name && same_optional_name(a->name, b->name));
        case TY_COMPLEX:
            return same_type_exact(a->base, b->base);
        case TY_BITINT:
            return a->bit_width == b->bit_width;
        default:
            return true;
    }
}

static bool same_member_shape(Member *a, Member *b) {
    if (!same_optional_name(a->name, b->name) || a->align != b->align ||
        a->is_bitfield != b->is_bitfield || a->bit_width != b->bit_width ||
        !same_type_exact(a->ty, b->ty))
        return false;
    return true;
}

static bool same_struct_members(Type *a, Type *b) {
    Member *ma = a->members;
    Member *mb = b->members;
    for (; ma && mb; ma = ma->next, mb = mb->next)
        if (!same_member_shape(ma, mb))
            return false;
    return !ma && !mb;
}

static Member *find_union_member_by_name(Type *ty, Member *needle) {
    for (Member *mem = ty->members; mem; mem = mem->next) {
        if (!needle->name || !mem->name) {
            if (needle->name == mem->name)
                return mem;
            continue;
        }
        if (same_optional_name(needle->name, mem->name))
            return mem;
    }
    return NULL;
}

static bool same_union_members(Type *a, Type *b) {
    int count_a = 0;
    int count_b = 0;
    for (Member *ma = a->members; ma; ma = ma->next) {
        count_a++;
        Member *mb = find_union_member_by_name(b, ma);
        if (!mb || !same_member_shape(ma, mb))
            return false;
    }
    for (Member *mb = b->members; mb; mb = mb->next)
        count_b++;
    return count_a == count_b;
}

static EnumConstant *find_enum_constant(Type *ty, char *name) {
    for (EnumConstant *ec = ty->enum_constants; ec; ec = ec->next)
        if (!strcmp(ec->name, name))
            return ec;
    return NULL;
}

static bool same_enum_constants(Type *a, Type *b) {
    int count_a = 0;
    int count_b = 0;
    for (EnumConstant *ea = a->enum_constants; ea; ea = ea->next) {
        count_a++;
        EnumConstant *eb = find_enum_constant(b, ea->name);
        if (!eb || eb->value != ea->value)
            return false;
    }
    for (EnumConstant *eb = b->enum_constants; eb; eb = eb->next)
        count_b++;
    return count_a == count_b;
}

static bool compatible_tag_redeclaration(Type *old, Type *new) {
    if (!old || !new || old->kind != new->kind)
        return false;
    if (old->kind == TY_ENUM) {
        if (!same_type_exact(old->enum_base_type, new->enum_base_type))
            return false;
        return same_enum_constants(old, new);
    }
    if (old->kind == TY_STRUCT)
        return same_struct_members(old, new);
    if (old->kind == TY_UNION)
        return same_union_members(old, new);
    return false;
}

static Type *install_tag_definition(VirtualMachine *vm, Token *tag, Type *ty,
                                    char *kind_name) {
    if (!tag)
        return ty;

    Type *existing = find_tag_in_current_scope(vm, tag);
    if (!existing) {
        push_tag_scope(vm, tag, ty);
        // #1010: this call site is only reached from struct_decl/union_decl/
        // enum_specifier's `{ ... }` (definition) path -- a bare forward
        // reference (`struct Foo;`) never calls install_tag_definition at
        // all (see struct_union_decl/enum_specifier's own push_tag_scope
        // calls). Mark the record push_tag_scope just created as a real
        // definition so serialize_type.c's find_tag_name_for_provenance() can
        // prefer it over an unrelated forward declaration recorded later.
        mark_last_type_name_as_definition(vm, ty);
        return ty;
    }

    if (existing->kind != ty->kind)
        error_tok(vm, tag, "tag redeclared as different kind");

    if (existing->size < 0 ||
        (existing->kind == TY_ENUM && !existing->enum_constants &&
         ty->enum_constants)) {
        *existing = *ty;
        // #906: completing a forward-declared tag (e.g. `typedef struct
        // Alpha Alpha;` in a header, `struct Alpha { ... };` in the primary
        // file) must re-record the tag with the *definition's* declaration
        // token. The forward declaration's record -- created by
        // push_tag_scope with the header token -- has from_include true, so
        // serialize_type.c's #891 filter (serialize_type_defs_for_owner) would
        // suppress the standalone definition from -m/-c=native output on the
        // assumption the re-emitted #include supplies the header. Provenance
        // belongs to the definition, not the first mention (the same rule
        // Type.struct_tag was added for in #892/#897).
        record_type_name(vm, existing, tag->loc, tag->len, true, tag);
        mark_last_type_name_as_definition(vm, existing); // #1010
        return existing;
    }

    if (vm->compiler.c_std < CCCC_STD_C23)
        error_tok(vm, tag, "redefinition of %s '%.*s'", kind_name, tag->len,
                  tag->loc);

    if (!compatible_tag_redeclaration(existing, ty))
        error_tok(vm, tag, "incompatible redeclaration of %s '%.*s'", kind_name,
                  tag->len, tag->loc);

    return existing;
}

// enum-specifier = ident? (":" typename)? "{" enum-list? "}"
//                | ident (":" typename)?
//
// enum-list      = ident ("=" num)? ("," ident ("=" num)?)* ","?
//
// C23: optional ": integer-type" specifies the underlying type.
// C23: "enum tag : underlying-type ;" is a forward declaration (complete type).
static Type *enum_specifier(VirtualMachine *vm, Token **rest, Token *tok) {
    Type *ty = enum_type(vm);

    // Read a tag.
    Token *tag = NULL;
    if (tok->kind == TK_IDENT) {
        tag          = tok;
        ty->name     = tag;
        ty->name_pos = tag;
        ty->enum_tag = tag;
        tok          = tok->next;
    }

    // C23: optional underlying type `: integer-type`.
    bool parse_enum_base = equal(tok, ":");
    // #1223: inside a _Generic association the `:` after a bare `enum Tag`
    // is the association colon, not an underlying-type introducer -- an
    // enum *reference* may not restate its underlying type there (gcc/clang:
    // "'enum' underlying type may not be specified here"). A braced
    // *definition* (`enum Tag : T { ... }`) is still fine -- the `{`
    // disambiguates. Distinguish by scanning past the prospective base type
    // for a `{` (definition, keep parsing the base) vs a `:` or association
    // terminator (reference -- leave the colon for the association parse; a
    // second `:` is the diagnosed error).
    if (parse_enum_base && vm->compiler.in_generic_assoc) {
        if (!is_typename(vm, tok->next)) {
            // bare `enum Tag :` -- the colon is the association's; leave it
            // unconsumed. (A value expression can never start with a
            // typename token, so this can't misfire on `enum G: (expr)`.)
            parse_enum_base = false;
        } else {
            Token *scan = tok->next;
            while (scan && !equal(scan, "{") && !equal(scan, ":") &&
                   !equal(scan, ";") && !equal(scan, ")") && !equal(scan, ","))
                scan = scan->next;
            if (!scan || !equal(scan, "{")) {
                if (scan && equal(scan, ":"))
                    error_tok(
                        vm, tok,
                        "'enum' underlying type may not be specified here");
                parse_enum_base = false;
            }
        }
    }
    if (parse_enum_base) {
        tok             = tok->next;
        Token *base_tok = tok;
        Type  *base_ty  = typename(vm, &tok, tok);
        if (!is_integer(base_ty) || base_ty->kind == TY_BOOL ||
            base_ty->kind == TY_ENUM)
            error_tok(vm, base_tok,
                      "enum underlying type must be a non-bool integer type");
        ty->enum_base_type = base_ty;
        ty->size           = base_ty->size;
        ty->align          = base_ty->align;
        ty->is_unsigned    = base_ty->is_unsigned;
    }

    if (tag && !equal(tok, "{")) {
        Type *existing = find_tag(vm, tag);
        if (existing) {
            if (existing->kind != TY_ENUM)
                error_tok(vm, tag, "not an enum tag");
            if (existing->is_deprecated)
                warn_deprecated_use(vm, tag, get_ident(vm, tag),
                                    existing->deprecated_msg);
            *rest = tok;
            return existing->is_deprecated
                       ? type_after_deprecated_use(vm, existing)
                       : existing;
        }
        // Forward declaration requires an underlying type (C23 §6.7.2.2)
        if (!ty->enum_base_type)
            error_tok(vm, tag,
                      "enum forward declaration requires an underlying type");
        push_tag_scope(vm, tag, ty);
        *rest = tok;
        return ty;
    }

    Type *existing_tag = tag ? find_tag_in_current_scope(vm, tag) : NULL;
    if (existing_tag && existing_tag->kind != TY_ENUM)
        error_tok(vm, tag, "not an enum tag");

    tok = skip(vm, tok, "{");

    // Read an enum-list.
    int                  i         = 0;
    int64_t              val       = 0;
    struct EnumConstant *enum_tail = NULL;
    // #1175: widest/narrowest enumerator values seen, used below to select
    // ty's underlying type when the enum has no C23 `: type` of its own.
    int64_t  min_val    = 0;
    uint64_t max_uval   = 0;
    bool     any_neg    = false;
    bool     first_enum = true;
    // #1095: the previous iteration's own VarScope (NULL before the first,
    // and for a duplicate re-declaration that reused an existing one) --
    // kept around so an auto-incrementing enumerator that turns out to
    // depend on the previous one's value can retroactively clear that
    // previous one's layout provenance too, see the had_eq check below.
    VarScope *prev_sc = NULL;
    while (!consume_end(rest, tok)) {
        if (i++ > 0)
            tok = skip(vm, tok, ",");

        char *name        = get_ident(vm, tok);
        int   name_len    = tok->len;
        tok               = tok->next;

        VarAttr enum_attr = {};
        tok               = attribute_list(vm, tok, NULL, &enum_attr);
        tok               = c23_attribute_list(vm, tok, NULL, &enum_attr);

        // #1095: reset every iteration -- an enumerator with no `= expr`
        // (auto-incrementing off the previous `val`) never carries layout
        // provenance of its own, regardless of what the previous
        // enumerator's `=` may have set.
        bool  had_eq              = equal(tok, "=");
        Type *val_layout_ty       = NULL;
        bool  val_layout_is_align = false;
        if (had_eq)
            val = const_expr_layout(vm, &tok, tok->next, &val_layout_ty,
                                    &val_layout_is_align);

        // #1095: this enumerator has no `=` of its own, so its value is
        // CCCC's own folded `<previous val> + 1` -- if the *previous*
        // enumerator was slated to re-materialize as sizeof/_Alignof, its
        // host-time value would then disagree with this one's still-folded
        // successor (`enum { N = sizeof(struct statfs), M };` -- M stays a
        // plain literal derived from the GUEST's sizeof, but N would print
        // the HOST's real sizeof, so `M == N + 1` could go false). Not a
        // hypothetical: caught by this exact repro during implementation.
        // Clear the previous one's provenance instead of re-materializing
        // it, the same "leave the inconsistent case folded" rule array
        // dimensions apply to an initialized global (see
        // SerializeContext.allow_layout_dims's own comment) -- both the
        // EnumConstant (the body) and its VarScope (every USE) must agree.
        if (!had_eq && enum_tail && enum_tail->layout_ty) {
            enum_tail->layout_ty       = NULL;
            enum_tail->layout_is_align = false;
            if (prev_sc) {
                prev_sc->enum_layout_ty       = NULL;
                prev_sc->enum_layout_is_align = false;
            }
        }

        bool      duplicate_from_same_enum = false;
        VarScope *old_sc = find_var_in_current_scope(vm, name, name_len);
        if (old_sc && old_sc->enum_ty) {
            EnumConstant *old_ec =
                existing_tag ? find_enum_constant(existing_tag, name) : NULL;
            duplicate_from_same_enum = old_ec && old_ec->value == val &&
                                       old_sc->enum_ty == existing_tag;
            if (!duplicate_from_same_enum)
                error_tok(vm, tok, "redeclaration of enumerator '%s'", name);
        }

        VarScope *this_sc = old_sc;
        if (!duplicate_from_same_enum) {
            VarScope *sc             = push_scope(vm, name, name_len);
            sc->enum_ty              = ty;
            sc->enum_val             = val;
            sc->enum_layout_ty       = val_layout_ty; // #1095
            sc->enum_layout_is_align = val_layout_is_align;
            sc->is_deprecated        = enum_attr.is_deprecated;
            sc->deprecated_msg       = enum_attr.deprecated_msg;
            this_sc                  = sc;
        }
        prev_sc = this_sc;

        // Store enum constant in Type structure for code emission
        struct EnumConstant *ec = arena_alloc(&vm->compiler.parser_arena,
                                              sizeof(struct EnumConstant));
        memset(ec, 0, sizeof(struct EnumConstant));
        ec->name            = name;
        ec->value           = val;
        ec->layout_ty       = val_layout_ty; // #1095
        ec->layout_is_align = val_layout_is_align;
        ec->next            = NULL;

        if (enum_tail) {
            enum_tail->next = ec;
        } else {
            ty->enum_constants = ec;
        }
        enum_tail = ec;

        // #1175: track this enumerator's value for underlying-type
        // selection below, regardless of whether it came from `=` or
        // auto-increment -- an auto-incremented value can itself overflow
        // int32 (`enum { A = 0xFFFFFFFF, B };`).
        if (first_enum || val < min_val)
            min_val = val;
        if (val < 0)
            any_neg = true;
        else if (first_enum || (uint64_t)val > max_uval)
            max_uval = (uint64_t)val;
        first_enum = false;

        val++;
    }

    // #1175/#1205: select the underlying type from the enumerator values
    // actually seen, matching gcc-16/clang -- C23 6.7.2.2 requires the type
    // be able to represent every enumerator, and both reference compilers
    // widen past `int` for this as an extension predating C23. Only when the
    // enum has no explicit `enum E : type` of its own (ty->enum_base_type
    // set above, which must keep winning unconditionally).
    //
    // #1205: an all-non-negative enum that fits `int` is `unsigned int` on
    // gcc-16/clang too -- verified directly, not just the >INT32_MAX case
    // #1175 already handled. C17/C23 6.7.2.2p3 still gives each *enumerator
    // identifier* type `int` even when the enum's own compatible type is
    // `unsigned int` (unlike a fixed `: type` base, which the enumerators
    // take on too) -- ty->enum_members_are_int records that split so the
    // enumerator-use site (parse_postfix.c) can type references `int` while
    // this Type itself is unsigned. Verified: `enum G { G1 = 1 };
    // (enum G)-1 < 0` is false and `-1 < G1` is true on both gcc-16 and
    // clang, `-std=c17` and `-std=c23` alike.
    // #1205: a completed body with no `: type` of its own, following a
    // forward declaration that HAD one (`enum E : int; enum E { A = 1 };`),
    // is ill-formed C23 (6.7.2.2p4 requires a redeclaration to repeat the
    // fixed type) -- both gcc-16 and clang reject it outright, verified
    // directly. CCCC doesn't diagnose that (a pre-existing, wider gap, not
    // this fix's scope), but it must not let this selection silently widen
    // or unsign past what the forward declaration already fixed -- adopt
    // the existing tag's base instead of guessing from the enumerator
    // values, so `install_tag_definition`'s own wholesale `*existing = *ty`
    // overwrite below can't drop a real fixed base #1205 never asked to
    // touch.
    if (existing_tag && existing_tag->enum_base_type) {
        ty->enum_base_type = existing_tag->enum_base_type;
        ty->size           = existing_tag->enum_base_type->size;
        ty->align          = existing_tag->enum_base_type->align;
        ty->is_unsigned    = existing_tag->enum_base_type->is_unsigned;
    } else if (!ty->enum_base_type && !first_enum) {
        if (any_neg) {
            // Signed: needs to hold min_val and max_uval both.
            if (min_val >= INT32_MIN && max_uval <= INT32_MAX) {
                // int -- ty already defaults to this (4/4, signed).
            } else {
                ty->size  = 8;
                ty->align = 8;
            }
        } else if (max_uval > UINT32_MAX) {
            ty->size        = 8;
            ty->align       = 8;
            ty->is_unsigned = true;
        } else {
            // Non-negative and fits `int` (<= INT32_MAX) or `unsigned int`
            // (<= UINT32_MAX) either way -- both stay 4/4 unsigned; #1205's
            // fix is folding the <= INT32_MAX case into this arm instead of
            // leaving it plain signed `int`.
            ty->is_unsigned = true;
            if (max_uval <= INT32_MAX)
                ty->enum_members_are_int = true; // #1205
        }
    }

    return install_tag_definition(vm, tag, ty, "enum");
}

// typeof-specifier = "(" (expr | typename) ")"
static Type *typeof_specifier(VirtualMachine *vm, Token **rest, Token *tok) {
    tok = skip(vm, tok, "(");

    Type *ty;
    if (is_typename(vm, tok)) {
        ty = typename(vm, &tok, tok);
    } else {
        Node *node = expr(vm, &tok, tok);
        add_type(vm, node);
        ty = node->ty;
    }
    *rest = skip(vm, tok, ")");
    return ty;
}

// typeof_unqual - C23 version of typeof that removes qualifiers
static Type *typeof_unqual_specifier(VirtualMachine *vm, Token **rest,
                                     Token *tok) {
    Type *ty = typeof_specifier(vm, rest, tok);
    // Copy the type to avoid mutating the original
    ty = copy_type(vm, ty);
    // Remove all qualifiers
    ty->is_const    = false;
    ty->is_volatile = false;
    return ty;
}

// C23 auto type inference: given an initializer expression type, return the
// deduced type (array-to-pointer decay, function-to-pointer decay already done
// by add_type for ND_VAR, then strip top-level qualifiers like typeof_unqual).
Type *auto_deduced_type(VirtualMachine *vm, Type *ty) {
    if (ty->kind == TY_ARRAY)
        ty = pointer_to(vm, ty->base);
    if (ty->is_const || ty->is_volatile || ty->is_restrict) {
        ty              = copy_type(vm, ty);
        ty->is_const    = false;
        ty->is_volatile = false;
        ty->is_restrict = false;
    }
    return ty;
}

// Walk the declarator result type down to the ty_auto sentinel counting TY_PTR
// hops.  Returns the depth (0 for plain `auto x`), or -1 if a non-PTR type
// other than ty_auto is encountered (e.g. array declarator).
int count_auto_ptr_depth(Type *ty) {
    int depth = 0;
    while (ty != ty_auto) {
        if (ty->kind != TY_PTR)
            return -1;
        depth++;
        ty = ty->base;
    }
    return depth;
}

// Count how many TY_PTR layers are at the top of a type.
int count_ptr_depth(Type *ty) {
    int depth = 0;
    while (ty->kind == TY_PTR) {
        depth++;
        ty = ty->base;
    }
    return depth;
}

// Get size for a type (no adjustment needed - types are already correct)
int get_vm_size(Type *ty) {
    return ty->size;
}

// #1163: the *explicit* alignment a single declarator requests, or 0 if it
// carries no alignment attribute at all. This is effective_decl_align()
// below minus the "fall back to the type's own natural alignment" step --
// splitting it out lets callers (struct/union layout, under `packed`) tell
// an explicit request apart from a member's ordinary natural alignment,
// which `effective_decl_align()` alone can't do: an explicit `aligned(4)`
// on an `int` resolves to the same value (4) as no request at all, but GCC
// still honors the explicit one under `packed` (verified against gcc-16)
// while the implicit one is exactly what `packed` suppresses.
//
// `attr->align` (an _Alignas *assignment*) can request less than the type's
// natural alignment; C17 6.7.5p4 forbids that outright ("cannot reduce
// alignment"), matching gcc/clang, so it's rejected here via error_tok
// rather than silently accepted the way #1160 originally left it.
int explicit_decl_align(VirtualMachine *vm, Token *tok, Type *ty,
                        VarAttr *attr) {
    int align = 0;
    if (attr && attr->align) {
        if (attr->align < ty->align)
            error_tok(vm, tok,
                      "requested alignment is less than minimum alignment "
                      "of %d for type",
                      ty->align);
        align = attr->align;
    }
    if (attr && attr->gnu_align > align)
        align = attr->gnu_align;
    if (ty->decl_align > align)
        align = ty->decl_align;
    return align;
}

// #1160: resolve the alignment a single declarator actually requests, once
// _Alignas (declspec, `attr->align`), GNU aligned(N) (declspec
// `attr->gnu_align`, or declarator-suffix `ty->decl_align`) have all been
// parsed. `ty->align` itself -- the type's own natural alignment -- is
// deliberately left out of explicit_decl_align()'s max() by name only to
// make the base term explicit; it's exactly what `attr->align ? ... :
// mem->ty->align` already fell back to before #1160, so behavior for a
// declarator carrying no attribute at all is unchanged.
int effective_decl_align(VirtualMachine *vm, Token *tok, Type *ty,
                         VarAttr *attr) {
    int explicit_align = explicit_decl_align(vm, tok, ty, attr);
    return explicit_align > ty->align ? explicit_align : ty->align;
}

// struct-members = (declspec declarator (","  declarator)* ";")*
static void struct_members(VirtualMachine *vm, Token **rest, Token *tok,
                           Type *ty) {
    Member  head = {};
    Member *cur  = &head;
    int     idx  = 0;

    while (!equal(tok, "}")) {
        // #1173: a `#pragma pack` between two members of one struct changes
        // GCC's per-member alignment mid-declaration, a form this
        // implementation doesn't support -- reject it explicitly rather than
        // silently applying the struct's opening pack value to every member
        // (or vice versa), which would just be a narrower reincarnation of
        // the original "accepted, not honoured" bug.
        if (tok->pack_align != ty->pack_align)
            error_tok(vm, tok,
                      "#pragma pack changed inside a struct/union body is "
                      "not supported -- move it outside the declaration");
        VarAttr attr   = {};
        Type   *basety = declspec(vm, &tok, tok, &attr);
        if (has_custom_attrs(basety, &attr))
            error_tok(vm, tok,
                      "custom attributes are only supported on file-scope "
                      "declarations");
        bool first = true;

        // Anonymous struct member
        Token *anon_tok = tok;
        if ((basety->kind == TY_STRUCT || basety->kind == TY_UNION) &&
            consume(vm, &tok, tok, ";")) {
            if (vm->compiler.c_std < CCCC_STD_C11)
                warn_tok(vm, anon_tok, CCCC_WARN_PEDANTIC,
                         "anonymous structs/unions are a C11 extension");
            Member *mem =
                arena_alloc(&vm->compiler.parser_arena, sizeof(Member));
            memset(mem, 0, sizeof(Member));
            mem->ty  = basety;
            mem->idx = idx++;
            mem->explicit_align =
                explicit_decl_align(vm, anon_tok, mem->ty, &attr);
            mem->align = mem->explicit_align > mem->ty->align
                             ? mem->explicit_align
                             : mem->ty->align;
            cur = cur->next = mem;
            continue;
        }

        // Regular struct members
        while (!consume(vm, &tok, tok, ";")) {
            if (!first)
                tok = skip(vm, tok, ",");
            first = false;

            Member *mem =
                arena_alloc(&vm->compiler.parser_arena, sizeof(Member));
            memset(mem, 0, sizeof(Member));
            mem->ty = declarator(vm, &tok, tok, basety);
            if (has_custom_attrs(mem->ty, NULL))
                error_tok(vm, mem->name ? mem->name : tok,
                          "custom attributes are only supported on file-scope "
                          "declarations");
            mem->name           = mem->ty->name;
            mem->idx            = idx++;
            mem->explicit_align = explicit_decl_align(
                vm, mem->name ? mem->name : tok, mem->ty, &attr);
            mem->align = mem->explicit_align > mem->ty->align
                             ? mem->explicit_align
                             : mem->ty->align;

            if (consume(vm, &tok, tok, ":")) {
                mem->is_bitfield = true;
                mem->bit_width   = const_expr(vm, &tok, tok);
                if (mem->bit_width < 0)
                    error_tok(vm, tok, "negative bit-field width");
                if (mem->bit_width > mem->ty->size * CHAR_BIT)
                    error_tok(vm, tok, "bit-field width exceeds its type");

                // #1165: a trailing __attribute__(...) after the bit-width
                // (`int b : 5 __attribute__((aligned(16)));`) -- gcc/clang
                // both parse this and *honor* aligned(N) on a bit-field
                // (verified against gcc-16/clang: it aligns the bit-field's
                // storage unit and raises the aggregate's own alignment,
                // surviving `packed` exactly like #1163's non-bit-field
                // case). Only [[...]] (C23 attribute syntax) is left
                // unparsed here -- gcc's own constant-expression parser
                // grabs the leading `[` at this position too, so cccc's
                // pre-existing syntax error there matches gcc's rejection
                // and is deliberately not "fixed".
                Token  *bf_tok  = tok;
                VarAttr bf_attr = {};
                tok             = attribute_list(vm, tok, NULL, &bf_attr);
                if (has_custom_attrs(NULL, &bf_attr))
                    error_tok(vm, mem->name ? mem->name : bf_tok,
                              "custom attributes are only supported on "
                              "file-scope declarations");

                // _Alignas/alignas on a bit-field is rejected outright by
                // gcc ("alignment specified for bit-field") -- cccc must
                // reject it too, whether it came from the declspec (before
                // the ':') or, in principle, from this suffix position.
                if (attr.align || bf_attr.align) {
                    if (mem->name)
                        error_tok(vm, mem->name,
                                  "alignment specified for bit-field '%.*s'",
                                  mem->name->len, mem->name->loc);
                    else
                        error_tok(vm, bf_tok,
                                  "alignment specified for bit-field");
                }

                if (bf_attr.gnu_align) {
                    // Same reason as declarator()'s suffix-attribute
                    // handling above: copy_type() stops the alignment
                    // leaking onto a sibling bit-field sharing this
                    // declarator's basety (`int b : 5
                    // __attribute__((aligned(16))), d : 3;`).
                    mem->ty = copy_type(vm, mem->ty);
                    if (bf_attr.gnu_align > mem->ty->decl_align)
                        mem->ty->decl_align = bf_attr.gnu_align;
                    mem->explicit_align = explicit_decl_align(
                        vm, mem->name ? mem->name : bf_tok, mem->ty, &attr);
                    mem->align = mem->explicit_align > mem->ty->align
                                     ? mem->explicit_align
                                     : mem->ty->align;
                }

                append_custom_attr_list(&mem->ty->custom_attrs,
                                        bf_attr.custom_attrs);
                mem->ty = apply_var_attrs_to_type(vm, mem->ty, &bf_attr);
            }

            cur = cur->next = mem;
        }
    }

    // If the last element is an array of incomplete type, it's
    // called a "flexible array member". It should behave as if
    // if were a zero-sized array.
    if (cur != &head && cur->ty->kind == TY_ARRAY && cur->ty->array_len < 0) {
        if (vm->compiler.c_std < CCCC_STD_C99)
            error_tok(vm, cur->name ? cur->name : *rest,
                      "flexible array members are not available before C99");
        cur->ty         = array_of(vm, cur->ty->base, 0);
        ty->is_flexible = true;
    }

    *rest       = tok->next;
    ty->members = head.next;
    resolve_member_checked_bounds(vm, ty->members);
}

static void apply_semantic_attr(Type *ty, VarAttr *attr, Token *tok,
                                bool unused, bool deprecated, bool nodiscard,
                                char *message) {
    if (ty) {
        ty->is_maybe_unused |= unused;
        ty->is_deprecated   |= deprecated;
        ty->is_nodiscard    |= nodiscard;
        if (message) {
            if (deprecated)
                ty->deprecated_msg = message;
            if (nodiscard)
                ty->nodiscard_msg = message;
        }
    }
    if (attr) {
        attr->is_maybe_unused |= unused;
        attr->is_deprecated   |= deprecated;
        attr->is_nodiscard    |= nodiscard;
        if (message) {
            if (deprecated)
                attr->deprecated_msg = message;
            if (nodiscard)
                attr->nodiscard_msg = message;
        }
        if (unused || deprecated || nodiscard)
            attr->attribute_tok = tok;
    }
}

Type *apply_var_attrs_to_type(VirtualMachine *vm, Type *ty, VarAttr *attr) {
    if (!attr ||
        (!attr->is_maybe_unused && !attr->is_deprecated && !attr->is_noreturn &&
         !attr->is_nodiscard && !attr->is_pure && !attr->is_func_const &&
         !attr->format_style && !attr->cleanup_fn && !attr->attr_error_msg &&
         !attr->attr_warning_msg && !attr->nonnull_all && !attr->nonnull_mask &&
         !attr->returns_nonnull && !attr->is_constructor &&
         !attr->is_destructor && !attr->is_sentinel && !attr->alloc_size_idx &&
         !attr->is_malloc && !attr->has_vector_size))
        return ty;

    // __attribute__((vector_size(N))) rewrites the whole type (base scalar
    // -> TY_VECTOR), so handle it before the generic copy_type()+field-merge
    // below, which assumes `ty` keeps its original kind.
    if (attr->has_vector_size) {
        int  bytes = attr->vector_size_bytes;
        bool elem_size_ok =
            ty->size == 1 || ty->size == 2 || ty->size == 4 || ty->size == 8;
        if ((!is_integer(ty) && !is_flonum(ty)) || !elem_size_ok)
            error_tok(vm, attr->vector_size_tok,
                      "'vector_size' attribute applies only to 1/2/4/8-byte "
                      "integer or floating-point scalar types");
        else if (bytes <= 0 || bytes % ty->size != 0)
            error_tok(vm, attr->vector_size_tok,
                      "vector_size %d is not a positive multiple of the "
                      "element size (%d)",
                      bytes, ty->size);
        else if (bytes != 16 && bytes != 32 && bytes != 64)
            error_tok(vm, attr->vector_size_tok,
                      "vector_size %d is not supported: only 16-, 32-, or "
                      "64-byte (128/256/512-bit) vectors are currently "
                      "supported",
                      bytes);
        else
            ty = vector_of(vm, ty, bytes);
    }

    ty = copy_type(vm, ty);
    apply_semantic_attr(ty, NULL, attr->attribute_tok, attr->is_maybe_unused,
                        attr->is_deprecated, attr->is_nodiscard,
                        attr->deprecated_msg ? attr->deprecated_msg
                                             : attr->nodiscard_msg);
    if (attr->is_noreturn && ty->kind == TY_FUNC)
        ty->is_noreturn = true;
    if (attr->is_pure && ty->kind == TY_FUNC)
        ty->is_pure = true;
    if (attr->is_func_const && ty->kind == TY_FUNC)
        ty->is_func_const = true;
    if (attr->format_style && ty->kind == TY_FUNC) {
        ty->format_style         = attr->format_style;
        ty->format_string_index  = attr->format_string_index;
        ty->format_fmt_first_arg = attr->format_fmt_first_arg;
    }
    // Transport cleanup_fn through the type so new_var() can pick it up.
    // (cleanup is a variable attribute, not a real type attribute.)
    if (attr->cleanup_fn)
        ty->cleanup_fn = attr->cleanup_fn;
    if (attr->attr_error_msg && ty->kind == TY_FUNC)
        ty->attr_error_msg = attr->attr_error_msg;
    if (attr->attr_warning_msg && ty->kind == TY_FUNC)
        ty->attr_warning_msg = attr->attr_warning_msg;
    if (ty->kind == TY_FUNC) {
        if (attr->nonnull_all)
            ty->nonnull_all = true;
        ty->nonnull_mask |= attr->nonnull_mask;
        if (attr->returns_nonnull)
            ty->returns_nonnull = true;
        if (attr->is_sentinel) {
            ty->is_sentinel  = true;
            ty->sentinel_pos = attr->sentinel_pos;
        }
        if (attr->alloc_size_idx) {
            ty->alloc_size_idx  = attr->alloc_size_idx;
            ty->alloc_size_idx2 = attr->alloc_size_idx2;
        }
        if (attr->is_malloc)
            ty->is_malloc = true;
        if (attr->is_constructor) {
            ty->is_constructor = true;
            ty->init_priority  = attr->init_priority;
        }
        if (attr->is_destructor) {
            ty->is_destructor = true;
            ty->init_priority = attr->init_priority;
        }
    }
    return ty;
}

static void inherit_semantic_attrs(Type *dst, Type *src) {
    if (!dst || !src)
        return;
    dst->is_maybe_unused |= src->is_maybe_unused;
    dst->is_deprecated   |= src->is_deprecated;
    dst->is_nodiscard    |= src->is_nodiscard;
    dst->is_noreturn     |= src->is_noreturn;
    dst->is_pure         |= src->is_pure;
    dst->is_func_const   |= src->is_func_const;
    if (!dst->deprecated_msg)
        dst->deprecated_msg = src->deprecated_msg;
    if (!dst->nodiscard_msg)
        dst->nodiscard_msg = src->nodiscard_msg;
    if (!dst->attr_error_msg)
        dst->attr_error_msg = src->attr_error_msg;
    if (!dst->attr_warning_msg)
        dst->attr_warning_msg = src->attr_warning_msg;
    if (src->format_style) {
        dst->format_style         = src->format_style;
        dst->format_string_index  = src->format_string_index;
        dst->format_fmt_first_arg = src->format_fmt_first_arg;
    }
    dst->nonnull_all     |= src->nonnull_all;
    dst->nonnull_mask    |= src->nonnull_mask;
    dst->returns_nonnull |= src->returns_nonnull;
    if (src->is_sentinel) {
        dst->is_sentinel  = true;
        dst->sentinel_pos = src->sentinel_pos;
    }
    if (src->alloc_size_idx) {
        dst->alloc_size_idx  = src->alloc_size_idx;
        dst->alloc_size_idx2 = src->alloc_size_idx2;
    }
    dst->is_malloc |= src->is_malloc;
    if (src->is_constructor) {
        dst->is_constructor = true;
        dst->init_priority  = src->init_priority;
    }
    if (src->is_destructor) {
        dst->is_destructor = true;
        dst->init_priority = src->init_priority;
    }
}

// Parse and discard an optimize attribute argument: (N) where N is an integer
// 0-4, or ("ON") where the string is "O0".."O4" or "-O0".."-O4" (GCC form).
// The VM has no optimiser, so the attribute has no effect -- it is still
// syntax-checked (a malformed argument is an error) and then ignored, for
// compatibility with GCC/Clang sources that carry it. Returns the token after
// the closing ')'.
static Token *parse_optimize_attr(VirtualMachine *vm, Token *tok, Type *ty,
                                  VarAttr *attr) {
    (void)ty;
    (void)attr;
    Token *attr_tok = tok;
    tok             = skip(vm, tok, "(");

    if (tok->kind == TK_NUM || tok->kind == TK_PP_NUM) {
        long long val;
        if (tok->kind == TK_NUM) {
            val = tok->val;
        } else {
            char *ep = NULL;
            val      = strtoll(tok->loc, &ep, 10);
            if (ep == tok->loc)
                error_tok(vm, tok,
                          "optimize level must be an integer 0-4 (got '%.*s')",
                          tok->len, tok->loc);
        }
        if (val < 0 || val > 4)
            error_tok(vm, tok,
                      "optimize level must be an integer 0-4 (got %lld)", val);
        tok = tok->next;
    } else if (tok->kind == TK_STR) {
        const char *s = tok->str;
        if (*s == '-')
            s++;
        if (*s != 'O' && *s != 'o')
            error_tok(
                vm, tok,
                "optimize string must be 'O0'–'O4' or '-O0'–'-O4' (got '%s')",
                tok->str);
        s++;
        if (*s < '0' || *s > '4' || *(s + 1) != '\0')
            error_tok(
                vm, tok,
                "optimize string must be 'O0'–'O4' or '-O0'–'-O4' (got '%s')",
                tok->str);
        tok = tok->next;
    } else {
        error_tok(vm, tok,
                  "optimize attribute expects an integer 0-4 or a string "
                  "like \"O2\" or \"-O2\"");
    }

    if (!vm->compiler.in_type_lookahead)
        warn_tok(vm, attr_tok, CCCC_WARN_IGNORED_FEATURES,
                 "'optimize' attribute ignored: the VM has no optimiser (use "
                 "-c=native for host -O)");

    tok = skip(vm, tok, ")");
    return tok;
}

// attribute = ("__attribute__" "(" "(" attribute-list ")" ")")*
Token *attribute_list(VirtualMachine *vm, Token *tok, Type *ty, VarAttr *attr) {
    while (consume(vm, &tok, tok, "__attribute__")) {
        tok        = skip(vm, tok, "(");
        tok        = skip(vm, tok, "(");

        bool first = true;

        while (!consume(vm, &tok, tok, ")")) {
            if (!first)
                tok = skip(vm, tok, ",");
            first           = false;

            Token *attr_tok = tok;

            // Checked-pointer attributes (#770/#482-484): __attribute__((
            // single/array/ntarray/count(n)/byte_count(n)/bounds(lo,hi)));
            // only meaningful right after '*' in pointers(), which is the
            // only call site that passes a TY_PTR ty -- see
            // apply_checked_ptr_attr()'s comment.
            if (is_attr_name(tok, "single") || is_attr_name(tok, "array") ||
                is_attr_name(tok, "ntarray") || is_attr_name(tok, "count") ||
                is_attr_name(tok, "byte_count") ||
                is_attr_name(tok, "bounds")) {
                const char *name = is_attr_name(tok, "single")    ? "single"
                                   : is_attr_name(tok, "array")   ? "array"
                                   : is_attr_name(tok, "ntarray") ? "ntarray"
                                   : is_attr_name(tok, "count")   ? "count"
                                   : is_attr_name(tok, "byte_count")
                                       ? "byte_count"
                                       : "bounds";
                tok = apply_checked_ptr_attr(vm, attr_tok, tok->next, ty, name);
                continue;
            }

            // Handle packed attribute
            if (consume(vm, &tok, tok, "packed")) {
                if (ty)
                    ty->is_packed = true;
                continue;
            }

            // Handle designated_init attribute: requires all initializers of
            // this struct type to use designated (.field = value) syntax (#659)
            if (consume(vm, &tok, tok, "designated_init")) {
                if (ty)
                    ty->designated_init = true;
                continue;
            }

            // Handle vector_size attribute: __attribute__((vector_size(N)))
            // rewrites the base scalar type into a TY_VECTOR of N bytes
            // (tracker #72). This only makes sense in declarator-suffix
            // position (e.g. `typedef float v4sf
            // __attribute__((vector_size(16)))`), which routes types through
            // VarAttr -> apply_var_attrs_to_type (ty is NULL here); there is no
            // meaningful struct/union-body use, so the `ty`-direct path is
            // intentionally not handled.
            if (consume(vm, &tok, tok, "vector_size")) {
                tok       = skip(vm, tok, "(");
                int bytes = const_expr(vm, &tok, tok);
                tok       = skip(vm, tok, ")");
                if (attr) {
                    attr->has_vector_size   = true;
                    attr->vector_size_bytes = bytes;
                    attr->vector_size_tok   = attr_tok;
                } else if (ty) {
                    warn_tok(vm, attr_tok, CCCC_WARN_ATTRIBUTES,
                             "'vector_size' ignored in this context");
                }
                continue;
            }

            // Handle aligned attribute. Two independent write targets:
            // `ty` for the pre-tag/pointer-suffix position (aligns the type
            // itself, unchanged since before #1160), and `attr->gnu_align`
            // for declspec position (#1160) -- a GNU aligned(N) can only
            // ever *raise* alignment (only `packed` lowers it), so this is a
            // floor via max(), never an assignment; see
            // effective_decl_align().
            if (consume(vm, &tok, tok, "aligned")) {
                // Bare `aligned` (no argument) requests the target's
                // maximum useful/natural alignment, which is 16 on every
                // ABI this compiler targets (macOS/Linux, aarch64/x86_64).
                int align = 16;
                if (equal(tok, "(")) {
                    tok   = skip(vm, tok, "(");
                    align = const_expr(vm, &tok, tok);
                    tok   = skip(vm, tok, ")");
                }
                if (ty)
                    ty->align = align;
                if (attr && align > attr->gnu_align)
                    attr->gnu_align = align;
                continue;
            }

            bool unused     = is_attr_name(tok, "unused");
            bool deprecated = is_attr_name(tok, "deprecated");
            if (unused || deprecated) {
                tok           = tok->next;
                char *message = NULL;
                if (equal(tok, "(")) {
                    int depth = 1;
                    tok       = tok->next;
                    if (deprecated && tok->kind == TK_STR)
                        message = tok->str;
                    while (depth > 0) {
                        if (equal(tok, "("))
                            depth++;
                        else if (equal(tok, ")"))
                            depth--;
                        tok = tok->next;
                    }
                }
                apply_semantic_attr(ty, attr, attr_tok, unused, deprecated,
                                    false, message);
                continue;
            }

            // Handle format attribute: __attribute__((format(printf, fmt_idx,
            // first_arg)))
            if (is_attr_name(tok, "format")) {
                tok = tok->next;
                if (equal(tok, "(")) {
                    tok       = tok->next;
                    int style = 0;
                    // Accept GCC/Clang alternate spellings.  strftime, os_log
                    // and unknown variants are accepted silently (style=0 = no
                    // validation).
                    if (equal(tok, "printf") || equal(tok, "__printf__") ||
                        equal(tok, "gnu_printf") || equal(tok, "printf0") ||
                        equal(tok, "__printf0__"))
                        style = 1;
                    else if (equal(tok, "scanf") || equal(tok, "__scanf__") ||
                             equal(tok, "gnu_scanf"))
                        style = 2;
                    else if (!equal(tok, "strftime") &&
                             !equal(tok, "__strftime__") &&
                             !equal(tok, "os_log") && !equal(tok, "__os_log__"))
                        error_tok(
                            vm, tok,
                            "expected 'printf' or 'scanf' in format attribute");
                    tok           = tok->next;
                    tok           = skip(vm, tok, ",");
                    int fmt_idx   = const_expr(vm, &tok, tok);
                    tok           = skip(vm, tok, ",");
                    int first_arg = const_expr(vm, &tok, tok);
                    tok           = skip(vm, tok, ")");
                    if (ty && ty->kind == TY_FUNC) {
                        ty->format_style         = style;
                        ty->format_string_index  = fmt_idx;
                        ty->format_fmt_first_arg = first_arg;
                    }
                    if (attr) {
                        attr->format_style         = style;
                        attr->format_string_index  = fmt_idx;
                        attr->format_fmt_first_arg = first_arg;
                    }
                }
                continue;
            }

            // Handle nonnull attribute: __attribute__((nonnull)) or
            // __attribute__((nonnull(1,3))). Bare form marks every pointer
            // parameter non-null; the indexed form marks specific 1-based
            // argument positions.
            if (is_attr_name(tok, "nonnull")) {
                tok           = tok->next;
                bool     all  = true;
                uint64_t mask = 0;
                if (equal(tok, "(")) {
                    tok = tok->next;
                    all = false;
                    if (!equal(tok, ")")) {
                        for (;;) {
                            int idx = const_expr(vm, &tok, tok);
                            if (idx >= 1 && idx <= 64)
                                mask |= (1ULL << (idx - 1));
                            if (!consume(vm, &tok, tok, ","))
                                break;
                        }
                    }
                    tok = skip(vm, tok, ")");
                }
                if (ty && ty->kind == TY_FUNC) {
                    if (all)
                        ty->nonnull_all = true;
                    ty->nonnull_mask |= mask;
                }
                if (attr) {
                    if (all)
                        attr->nonnull_all = true;
                    attr->nonnull_mask |= mask;
                }
                continue;
            }

            // Handle returns_nonnull attribute
            if (is_attr_name(tok, "returns_nonnull")) {
                tok = tok->next;
                if (ty && ty->kind == TY_FUNC)
                    ty->returns_nonnull = true;
                if (attr)
                    attr->returns_nonnull = true;
                continue;
            }

            // Handle sentinel attribute: __attribute__((sentinel)) requires the
            // last variadic argument to be a literal NULL;
            // __attribute__((sentinel(N))) allows N trailing non-sentinel args
            // before the NULL (#658).
            if (is_attr_name(tok, "sentinel")) {
                tok     = tok->next;
                int pos = 0;
                if (equal(tok, "(")) {
                    tok = tok->next;
                    pos = const_expr(vm, &tok, tok);
                    tok = skip(vm, tok, ")");
                }
                if (ty && ty->kind == TY_FUNC) {
                    ty->is_sentinel  = true;
                    ty->sentinel_pos = pos;
                }
                if (attr) {
                    attr->is_sentinel  = true;
                    attr->sentinel_pos = pos;
                }
                continue;
            }

            // __attribute__((alloc_size(n))) /
            // __attribute__((alloc_size(n,m))): 1-based argument index(es)
            // whose (product of) value(s) is the byte size of the object
            // returned by this allocator-shaped function. Consulted by
            // objsize_alloc_from_call (#649) to generalize #642's hardcoded
            // malloc-family name matching to any annotated function.
            if (is_attr_name(tok, "alloc_size")) {
                tok      = tok->next;
                int idx1 = 0, idx2 = 0;
                if (equal(tok, "(")) {
                    tok  = tok->next;
                    idx1 = const_expr(vm, &tok, tok);
                    if (consume(vm, &tok, tok, ","))
                        idx2 = const_expr(vm, &tok, tok);
                    tok = skip(vm, tok, ")");
                }
                if (ty && ty->kind == TY_FUNC) {
                    ty->alloc_size_idx  = idx1;
                    ty->alloc_size_idx2 = idx2;
                }
                if (attr) {
                    attr->alloc_size_idx  = idx1;
                    attr->alloc_size_idx2 = idx2;
                }
                continue;
            }

            // __attribute__((malloc)): the function returns a freshly
            // allocated, non-aliasing pointer. Informational only in CCCC for
            // now -- not wired to nonnull inference (malloc can return NULL) or
            // to any aliasing optimization; see #649 followup.
            if (is_attr_name(tok, "malloc")) {
                tok = tok->next;
                if (ty && ty->kind == TY_FUNC)
                    ty->is_malloc = true;
                if (attr)
                    attr->is_malloc = true;
                continue;
            }

            // Handle noreturn attribute
            if (is_attr_name(tok, "noreturn")) {
                tok = tok->next;
                if (ty)
                    ty->is_noreturn = true;
                if (attr)
                    attr->is_noreturn = true;
                continue;
            }

            if (is_attr_name(tok, "pure")) {
                tok = tok->next;
                if (ty)
                    ty->is_pure = true;
                if (attr)
                    attr->is_pure = true;
                continue;
            }

            if (is_attr_name(tok, "const")) {
                tok = tok->next;
                if (ty)
                    ty->is_func_const = true;
                if (attr)
                    attr->is_func_const = true;
                continue;
            }

            // GNU equivalent of [[nodiscard]]: warn if return value is
            // discarded
            if (is_attr_name(tok, "warn_unused_result")) {
                tok = tok->next;
                apply_semantic_attr(ty, attr, attr_tok, false, false, true,
                                    NULL);
                continue;
            }

            // __attribute__((error("msg"))): if this function is called (and
            // the call is not eliminated by dead-code suppression), emit a
            // compile-time error. DCE-aware: the diagnostic is suppressed when
            // the call site is inside a statically-dead branch (dead_code_depth
            // > 0).  See static_branch_value().
            if (is_attr_name(tok, "error")) {
                tok           = tok->next;
                char *message = NULL;
                if (equal(tok, "(")) {
                    tok = tok->next;
                    if (tok->kind == TK_STR)
                        message = tok->str;
                    // Skip to closing paren
                    int depth = 1;
                    while (depth > 0) {
                        if (equal(tok, "("))
                            depth++;
                        else if (equal(tok, ")"))
                            depth--;
                        tok = tok->next;
                    }
                }
                if (!vm->compiler.in_type_lookahead) {
                    if (ty)
                        ty->attr_error_msg = message ? message : "";
                    if (attr)
                        attr->attr_error_msg = message ? message : "";
                    vm->compiler.saw_diag_attr = true;
                }
                continue;
            }

            // __attribute__((warning("msg"))): emit a compile-time warning when
            // called. DCE-aware: suppressed inside statically-dead branches,
            // same as error above.
            if (is_attr_name(tok, "warning")) {
                tok           = tok->next;
                char *message = NULL;
                if (equal(tok, "(")) {
                    tok = tok->next;
                    if (tok->kind == TK_STR)
                        message = tok->str;
                    int depth = 1;
                    while (depth > 0) {
                        if (equal(tok, "("))
                            depth++;
                        else if (equal(tok, ")"))
                            depth--;
                        tok = tok->next;
                    }
                }
                if (!vm->compiler.in_type_lookahead) {
                    if (ty)
                        ty->attr_warning_msg = message ? message : "";
                    if (attr)
                        attr->attr_warning_msg = message ? message : "";
                    vm->compiler.saw_diag_attr = true;
                }
                continue;
            }

            // Handle optimize attribute: __attribute__((optimize("O2"))) or
            // optimize(2)
            if (is_attr_name(tok, "optimize")) {
                tok = tok->next;
                tok = parse_optimize_attr(vm, tok, ty, attr);
                continue;
            }

            // Handle __attribute__((cleanup(fn))) — scope-exit callback
            if (is_attr_name(tok, "cleanup")) {
                tok = tok->next;
                tok = skip(vm, tok, "(");
                if (tok->kind != TK_IDENT)
                    error_tok(vm, tok,
                              "expected function name in cleanup attribute");
                Token *fn_tok = tok;
                tok           = tok->next;
                tok           = skip(vm, tok, ")");
                if (!vm->compiler.in_type_lookahead && attr) {
                    VarScope *sc = find_var(vm, fn_tok);
                    if (!sc || !sc->var || !sc->var->is_function)
                        error_tok(vm, fn_tok,
                                  "cleanup argument '%.*s' is not a function",
                                  fn_tok->len, fn_tok->loc);
                    attr->cleanup_fn  = sc->var;
                    attr->cleanup_tok = fn_tok;
                }
                continue;
            }

            // Handle __attribute__((constructor[(priority)]))
            // __attribute__((destructor[(priority)]))
            {
                bool is_ctor = is_attr_name(tok, "constructor");
                bool is_dtor = !is_ctor && is_attr_name(tok, "destructor");
                if (is_ctor || is_dtor) {
                    tok          = tok->next;
                    int priority = CCCC_NO_INIT_PRIORITY;
                    if (equal(tok, "(")) {
                        tok      = skip(vm, tok, "(");
                        priority = const_expr(vm, &tok, tok);
                        tok      = skip(vm, tok, ")");
                    }
                    if (ty) {
                        if (is_ctor) {
                            ty->is_constructor = true;
                        } else {
                            ty->is_destructor = true;
                        }
                        ty->init_priority = priority;
                    }
                    if (attr) {
                        if (is_ctor) {
                            attr->is_constructor = true;
                        } else {
                            attr->is_destructor = true;
                        }
                        attr->init_priority = priority;
                    }
                    continue;
                }
            }

            if (find_attribute_macro(vm, tok)) {
                Token *name_tok = tok;
                tok             = tok->next;
                Node *args      = NULL;
                int   arg_count = 0;
                tok = parse_custom_attr_args(vm, tok, &args, &arg_count);
                if (!vm->compiler.in_type_lookahead) {
                    if (attr)
                        append_custom_attr(vm, &attr->custom_attrs, name_tok,
                                           args, arg_count);
                    else if (ty)
                        append_custom_attr(vm, &ty->custom_attrs, name_tok,
                                           args, arg_count);
                    else
                        error_tok(vm, name_tok,
                                  "custom attribute '%.*s' is not valid here",
                                  name_tok->len, name_tok->loc);
                }
                continue;
            }

            // Handle all other attributes - just consume and ignore them
            if (tok->kind == TK_IDENT) {
                Token *name_tok = tok;
                tok             = tok->next;
                warn_tok(vm, name_tok, CCCC_WARN_ATTRIBUTES,
                         "unknown attribute '%.*s' ignored", name_tok->len,
                         name_tok->loc);

                // Handle attributes with parameters: attr(args...)
                if (equal(tok, "(")) {
                    int depth = 1;
                    tok       = tok->next;
                    // Skip all tokens until matching closing paren
                    while (depth > 0) {
                        if (equal(tok, "("))
                            depth++;
                        else if (equal(tok, ")"))
                            depth--;
                        tok = tok->next;
                    }
                }
                continue;
            }

            // If we hit something unexpected, just skip it
            tok = tok->next;
        }

        tok = skip(vm, tok, ")");
    }

    return tok;
}

// c23-attribute = ("[[" attribute-list "]]")*
// #1160: `allow_ty_align` distinguishes the two struct_union_decl call
// sites that pass a real `ty` -- verified against gcc-16: GNU-syntax
// `struct S { ... } __attribute__((aligned(16)));` after the closing brace
// *does* raise the struct's alignment, but the C23 spelling
// `struct S { ... } [[gnu::aligned(16)]];` in that same trailing position
// does not (silently ignored; `[[gnu::packed]]` there gets an actual
// -Wattributes warning from gcc). Every other call site either passes
// ty==NULL (declspec/declarator/label position, where `attr` is the real
// target) or is the struct/union pre-tag position and pointer-suffix
// position, both of which gcc *does* honor -- so those pass true.
Token *c23_attribute_list_ex(VirtualMachine *vm, Token *tok, Type *ty,
                             VarAttr *attr, bool allow_ty_align) {
    while (equal(tok, "[") && equal(tok->next, "[")) {
        if (vm->compiler.c_std < CCCC_STD_C23 &&
            !vm->compiler.in_type_lookahead)
            warn_tok(vm, tok, CCCC_WARN_PEDANTIC,
                     "'[[...]]' attributes are a C23 extension");
        tok        = tok->next->next; // Skip [[

        bool first = true;

        while (!equal(tok, "]")) {
            if (!first)
                tok = skip(vm, tok, ",");
            first = false;

            // Parse attribute name
            if (tok->kind != TK_IDENT)
                error_tok(vm, tok, "expected attribute name");

            Token *attr_tok    = tok;
            Token *name_tok    = tok;
            bool   cccc_scoped = false;
            bool   gnu_scoped  = false;
            if (tok->next && equal(tok->next, ":") && tok->next->next &&
                equal(tok->next->next, ":") && tok->next->next->next &&
                (tok->next->next->next->kind == TK_IDENT ||
                 tok->next->next->next->kind == TK_KEYWORD)) {
                if (equal(tok, "cccc")) {
                    cccc_scoped = true;
                } else if (equal(tok, "gnu")) {
                    gnu_scoped = true;
                }
                if (cccc_scoped || gnu_scoped) {
                    name_tok = tok->next->next->next;
                    tok      = name_tok;
                }
            }

            bool unused              = equal(name_tok, "maybe_unused");
            bool deprecated          = equal(name_tok, "deprecated");
            bool is_noreturn_attr    = equal(name_tok, "noreturn");
            bool is_nodiscard_attr   = equal(name_tok, "nodiscard");
            bool is_fallthrough_attr = equal(name_tok, "fallthrough");
            bool is_no_unique_address_attr =
                equal(name_tok, "no_unique_address");
            bool is_pure_attr            = equal(name_tok, "pure");
            bool is_func_const_attr      = equal(name_tok, "const");
            bool is_optimize_attr        = equal(name_tok, "optimize");
            bool is_designated_init_attr = equal(name_tok, "designated_init");
            bool is_checked_ptr_attr =
                equal(name_tok, "single") || equal(name_tok, "array") ||
                equal(name_tok, "ntarray") || equal(name_tok, "count") ||
                equal(name_tok, "byte_count") || equal(name_tok, "bounds");
            tok = tok->next;

            // Checked-pointer attributes (#770/#482-484): [[cccc::single]] /
            // [[cccc::array]] / [[cccc::ntarray]] / [[cccc::count(n)]] /
            // [[cccc::byte_count(n)]] / [[cccc::bounds(lo, hi)]]; only
            // meaningful right after '*' in pointers() -- see
            // apply_checked_ptr_attr()'s comment.
            if (is_checked_ptr_attr) {
                const char *name = equal(name_tok, "single")    ? "single"
                                   : equal(name_tok, "array")   ? "array"
                                   : equal(name_tok, "ntarray") ? "ntarray"
                                   : equal(name_tok, "count")   ? "count"
                                   : equal(name_tok, "byte_count")
                                       ? "byte_count"
                                       : "bounds";
                tok = apply_checked_ptr_attr(vm, attr_tok, tok, ty, name);
                continue;
            }

            // Optimize attribute has mandatory args: [[cccc::optimize(2)]] or
            // ("O2")
            if (is_optimize_attr) {
                if (!equal(tok, "("))
                    error_tok(vm, attr_tok,
                              "optimize attribute requires a level argument, "
                              "e.g. [[cccc::optimize(2)]] or "
                              "[[cccc::optimize(\"O2\")]]");
                tok = parse_optimize_attr(vm, tok, ty, attr);
                continue;
            }

            // [[gnu::cleanup(fn)]] — scope-exit callback
            if (gnu_scoped && equal(name_tok, "cleanup")) {
                tok = skip(vm, tok, "(");
                if (tok->kind != TK_IDENT)
                    error_tok(vm, tok,
                              "expected function name in cleanup attribute");
                Token *fn_tok = tok;
                tok           = tok->next;
                tok           = skip(vm, tok, ")");
                if (!vm->compiler.in_type_lookahead && attr) {
                    VarScope *sc = find_var(vm, fn_tok);
                    if (!sc || !sc->var || !sc->var->is_function)
                        error_tok(vm, fn_tok,
                                  "cleanup argument '%.*s' is not a function",
                                  fn_tok->len, fn_tok->loc);
                    attr->cleanup_fn  = sc->var;
                    attr->cleanup_tok = fn_tok;
                }
                continue;
            }

            // [[gnu::packed]] (#1160) -- C23 spelling of
            // __attribute__((packed)); see attribute_list()'s "packed" case
            // for the ty->is_packed semantics this mirrors.
            if (gnu_scoped && equal(name_tok, "packed")) {
                if (ty && allow_ty_align)
                    ty->is_packed = true;
                continue;
            }

            // [[gnu::aligned]] / [[gnu::aligned(N)]] (#1160) -- C23 spelling
            // of __attribute__((aligned(N))); see attribute_list()'s
            // "aligned" case for the ty/attr->gnu_align split this mirrors,
            // and c23_attribute_list_ex()'s own comment for why
            // `allow_ty_align` exists.
            if (gnu_scoped && equal(name_tok, "aligned")) {
                int align = 16; // bare form: maximum useful alignment
                if (equal(tok, "(")) {
                    tok   = skip(vm, tok, "(");
                    align = const_expr(vm, &tok, tok);
                    tok   = skip(vm, tok, ")");
                }
                if (ty && allow_ty_align)
                    ty->align = align;
                if (attr && align > attr->gnu_align)
                    attr->gnu_align = align;
                continue;
            }

            // [[gnu::nonnull]] / [[gnu::nonnull(1,3)]] /
            // [[gnu::returns_nonnull]]
            if (equal(name_tok, "nonnull") ||
                equal(name_tok, "returns_nonnull")) {
                bool     is_returns = equal(name_tok, "returns_nonnull");
                bool     all        = true;
                uint64_t mask       = 0;
                if (!is_returns && equal(tok, "(")) {
                    tok = tok->next;
                    all = false;
                    if (!equal(tok, ")")) {
                        for (;;) {
                            int idx = const_expr(vm, &tok, tok);
                            if (idx >= 1 && idx <= 64)
                                mask |= (1ULL << (idx - 1));
                            if (!consume(vm, &tok, tok, ","))
                                break;
                        }
                    }
                    tok = skip(vm, tok, ")");
                }
                if (is_returns) {
                    if (ty && ty->kind == TY_FUNC)
                        ty->returns_nonnull = true;
                    if (attr)
                        attr->returns_nonnull = true;
                } else {
                    if (ty && ty->kind == TY_FUNC) {
                        if (all)
                            ty->nonnull_all = true;
                        ty->nonnull_mask |= mask;
                    }
                    if (attr) {
                        if (all)
                            attr->nonnull_all = true;
                        attr->nonnull_mask |= mask;
                    }
                }
                continue;
            }

            // [[gnu::sentinel]] / [[gnu::sentinel(N)]] (#658)
            if (equal(name_tok, "sentinel")) {
                int pos = 0;
                if (equal(tok, "(")) {
                    tok = tok->next;
                    pos = const_expr(vm, &tok, tok);
                    tok = skip(vm, tok, ")");
                }
                if (ty && ty->kind == TY_FUNC) {
                    ty->is_sentinel  = true;
                    ty->sentinel_pos = pos;
                }
                if (attr) {
                    attr->is_sentinel  = true;
                    attr->sentinel_pos = pos;
                }
                continue;
            }

            // [[gnu::alloc_size(n)]] / [[gnu::alloc_size(n,m)]] (#649)
            if (equal(name_tok, "alloc_size")) {
                int idx1 = 0, idx2 = 0;
                if (equal(tok, "(")) {
                    tok  = tok->next;
                    idx1 = const_expr(vm, &tok, tok);
                    if (consume(vm, &tok, tok, ","))
                        idx2 = const_expr(vm, &tok, tok);
                    tok = skip(vm, tok, ")");
                }
                if (ty && ty->kind == TY_FUNC) {
                    ty->alloc_size_idx  = idx1;
                    ty->alloc_size_idx2 = idx2;
                }
                if (attr) {
                    attr->alloc_size_idx  = idx1;
                    attr->alloc_size_idx2 = idx2;
                }
                continue;
            }

            // [[gnu::vector_size(N)]] (tracker #72) -- same semantics as the
            // GNU-syntax handler in attribute_list(); only meaningful in
            // declarator-suffix position (attr non-NULL).
            if (gnu_scoped && equal(name_tok, "vector_size")) {
                tok       = skip(vm, tok, "(");
                int bytes = const_expr(vm, &tok, tok);
                tok       = skip(vm, tok, ")");
                if (attr) {
                    attr->has_vector_size   = true;
                    attr->vector_size_bytes = bytes;
                    attr->vector_size_tok   = attr_tok;
                } else if (ty) {
                    warn_tok(vm, attr_tok, CCCC_WARN_ATTRIBUTES,
                             "'vector_size' ignored in this context");
                }
                continue;
            }

            // [[gnu::malloc]] (#649) -- informational, see the GNU-syntax
            // handler above for the full rationale.
            if (equal(name_tok, "malloc")) {
                if (ty && ty->kind == TY_FUNC)
                    ty->is_malloc = true;
                if (attr)
                    attr->is_malloc = true;
                continue;
            }

            // [[gnu::constructor]] / [[gnu::constructor(101)]]
            // [[gnu::destructor]] / [[gnu::destructor(101)]]
            if (gnu_scoped && (equal(name_tok, "constructor") ||
                               equal(name_tok, "destructor"))) {
                bool is_ctor  = equal(name_tok, "constructor");
                int  priority = CCCC_NO_INIT_PRIORITY;
                if (equal(tok, "(")) {
                    tok      = skip(vm, tok, "(");
                    priority = const_expr(vm, &tok, tok);
                    tok      = skip(vm, tok, ")");
                }
                if (ty && ty->kind == TY_FUNC) {
                    if (is_ctor)
                        ty->is_constructor = true;
                    else
                        ty->is_destructor = true;
                    ty->init_priority = priority;
                }
                if (attr) {
                    if (is_ctor)
                        attr->is_constructor = true;
                    else
                        attr->is_destructor = true;
                    attr->init_priority = priority;
                }
                continue;
            }

            char *message = NULL;
            if (equal(tok, "(")) {
                if (find_attribute_macro(vm, name_tok)) {
                    Node *args      = NULL;
                    int   arg_count = 0;
                    tok = parse_custom_attr_args(vm, tok, &args, &arg_count);
                    if (!vm->compiler.in_type_lookahead) {
                        if (attr)
                            append_custom_attr(vm, &attr->custom_attrs,
                                               name_tok, args, arg_count);
                        else if (ty)
                            append_custom_attr(vm, &ty->custom_attrs, name_tok,
                                               args, arg_count);
                        else
                            error_tok(
                                vm, name_tok,
                                "custom attribute '%.*s' is not valid here",
                                name_tok->len, name_tok->loc);
                    }
                    continue;
                } else {
                    int depth = 1;
                    tok       = tok->next;
                    if ((deprecated || is_nodiscard_attr) &&
                        tok->kind == TK_STR)
                        message = tok->str;
                    while (depth > 0) {
                        if (equal(tok, "("))
                            depth++;
                        else if (equal(tok, ")"))
                            depth--;
                        tok = tok->next;
                    }
                }
            }
            if (find_attribute_macro(vm, name_tok)) {
                if (!vm->compiler.in_type_lookahead) {
                    if (attr)
                        append_custom_attr(vm, &attr->custom_attrs, name_tok,
                                           NULL, 0);
                    else if (ty)
                        append_custom_attr(vm, &ty->custom_attrs, name_tok,
                                           NULL, 0);
                    else
                        error_tok(vm, name_tok,
                                  "custom attribute '%.*s' is not valid here",
                                  name_tok->len, name_tok->loc);
                }
                continue;
            }
            if (is_noreturn_attr) {
                if (ty)
                    ty->is_noreturn = true;
                if (attr)
                    attr->is_noreturn = true;
            } else if (is_nodiscard_attr) {
                if (ty)
                    ty->is_nodiscard = true;
                if (attr) {
                    attr->is_nodiscard = true;
                    if (message)
                        attr->nodiscard_msg = message;
                    attr->attribute_tok = attr_tok;
                }
            } else if (is_fallthrough_attr) {
                if (attr)
                    attr->is_fallthrough = true;
            } else if (is_no_unique_address_attr) {
                // Parsed but VM optimisations deferred to a future ticket
            } else if (is_pure_attr) {
                if (ty)
                    ty->is_pure = true;
                if (attr)
                    attr->is_pure = true;
            } else if (is_func_const_attr) {
                if (ty)
                    ty->is_func_const = true;
                if (attr)
                    attr->is_func_const = true;
            } else if (is_designated_init_attr) {
                if (ty)
                    ty->designated_init = true;
            } else if (!unused && !deprecated) {
                warn_tok(vm, attr_tok, CCCC_WARN_ATTRIBUTES,
                         "unknown attribute '%.*s' ignored",
                         (cccc_scoped || gnu_scoped) ? name_tok->len
                                                     : attr_tok->len,
                         (cccc_scoped || gnu_scoped) ? name_tok->loc
                                                     : attr_tok->loc);
                apply_semantic_attr(ty, attr, attr_tok, unused, deprecated,
                                    false, NULL);
            } else {
                apply_semantic_attr(ty, attr, attr_tok, unused, deprecated,
                                    false, message);
            }
        }

        tok = skip(vm, tok, "]");
        tok = skip(vm, tok, "]");
    }

    return tok;
}

Token *c23_attribute_list(VirtualMachine *vm, Token *tok, Type *ty,
                          VarAttr *attr) {
    return c23_attribute_list_ex(vm, tok, ty, attr, /*allow_ty_align=*/true);
}

// struct-union-decl = attribute? ident? ("{" struct-members)?
//
// is_definition reports whether this call parsed a `{ ... }` member-list
// (a genuine definition) as opposed to a bare reference/forward-declaration
// to an existing or not-yet-defined tag. struct_decl/union_decl use it to
// avoid re-running install_tag_definition on every *reference* to a tag,
// which used to (a) redundantly recompute member offsets on an already-
// complete type and (b) call install_tag_definition with `ty->name` -- for
// the reference branch below `ty` is a *fresh, throwaway* Type whose ->name
// was never installed anywhere, but for the same-named "install into
// caller's scope" bug that mattered here, the real hazard is: the returned
// `ty2` is the canonical, shared Type for the tag, and declarator()
// overwrites *its* ->name with each declarator's own identifier (the
// hazard #892's Type.struct_tag field was added to survive). Running
// install_tag_definition again after such an overwrite used `ty->name`
// (by then some unrelated parameter/variable name) as the tag, silently
// re-registering the type under the wrong name in the *referencing
// function's* scope -- see #897.
static Type *struct_union_decl(VirtualMachine *vm, Token **rest, Token *tok,
                               TypeKind kind, bool *is_definition) {
    *is_definition = false;
    Type *ty       = struct_type(vm);
    ty->kind       = kind;
    // #1173: the #pragma pack(N) alignment cap in effect at the
    // struct/union keyword, stamped by the preprocessor onto every token
    // (src/preprocess.c). `tok` here is the token immediately after that
    // keyword; no pragma can appear between the keyword and this token, so
    // its stamp is identical to the keyword's own.
    ty->pack_align = tok->pack_align;
    tok            = attribute_list(vm, tok, ty, NULL);
    tok            = c23_attribute_list(vm, tok, ty, NULL);

    // Read a tag.
    Token *tag = NULL;
    if (tok->kind == TK_IDENT) {
        tag            = tok;
        ty->name       = tag;
        ty->name_pos   = tag;
        ty->struct_tag = tag; // #892: survives declarator name-overwrite
        tok            = tok->next;
    }

    if (tag && !equal(tok, "{")) {
        *rest     = tok;

        Type *ty2 = find_tag(vm, tag);
        if (ty2) {
            if (ty2->kind != kind)
                error_tok(vm, tag, "tag redeclared as different kind");
            if (ty2->is_deprecated)
                warn_deprecated_use(vm, tag, get_ident(vm, tag),
                                    ty2->deprecated_msg);
            return ty2->is_deprecated ? type_after_deprecated_use(vm, ty2)
                                      : ty2;
        }

        ty->size = -1;
        push_tag_scope(vm, tag, ty);
        return ty;
    }

    tok = skip(vm, tok, "{");

    // Construct a struct object.
    struct_members(vm, &tok, tok, ty);
    tok = attribute_list(vm, tok, ty, NULL);
    // #1160: unlike the GNU spelling (still handled above), gcc does not
    // apply a trailing [[gnu::aligned(N)]]/[[gnu::packed]] here -- see
    // c23_attribute_list_ex()'s comment.
    *rest          = c23_attribute_list_ex(vm, tok, ty, NULL,
                                           /*allow_ty_align=*/false);
    *is_definition = true;
    return ty;
}

// #1176 follow-up (#1211): whether an *unnamed* bit-field contributes
// its declared type's alignment (and, for a union, its size) to the
// enclosing aggregate is not a gcc-vs-clang split as #1176 originally
// concluded -- it is the real AAPCS64 rule, true on AArch64 everywhere
// *except* Darwin. Measured directly (gcc-16/clang, both `struct{char c;
// int:0; char d;}` and `struct{char c; int:3; char d;}`, both compilers):
// x86_64 (any OS) and macOS/arm64 clang all agree at 5/1 and 3/1
// (unnamed bit-fields contribute nothing); Linux/aarch64 (both compiler
// families) gives 8/4 and 4/4 (they contribute, matching a *named*
// bit-field of the same width exactly). macOS/arm64 gcc-16 alone gives
// 8/4 and 4/4 too -- gcc simply never implemented Apple's AAPCS64
// deviation there (`targetm.align_anon_bitfields()` is unconditionally
// true for the whole AArch64 target in gcc, Darwin included), so it is
// the one WONT_FIX outlier, not a second axis (tools/testing/__init__.py's
// NATIVE_SKIP_TESTS_GCC_MACOS quarantines it). Since -c=native shells out
// to the real host `cc` (clang on Darwin, gcc-or-clang elsewhere) and this
// project's own reference target is that host, follow the true ABI rather
// than either compiler family uniformly. A *named* bit-field of any width
// is unaffected on every target (verified 4/4 everywhere) and is not
// gated by this.
#if defined(__aarch64__) && !defined(__APPLE__)
#define CCCC_ALIGN_ANON_BITFIELDS 1
#else
#define CCCC_ALIGN_ANON_BITFIELDS 0
#endif

// struct-decl = struct-union-decl
static Type *struct_decl(VirtualMachine *vm, Token **rest, Token *tok) {
    bool  is_definition = false;
    Type *ty = struct_union_decl(vm, rest, tok, TY_STRUCT, &is_definition);

    if (ty->size < 0)
        return ty;

    // A bare reference to an already-complete tag (not a `{ ... }`
    // definition) -- nothing new to install. Re-running the block below
    // would both needlessly recompute member offsets and re-install the
    // tag under whatever name `ty->name` (the *shared* canonical type's
    // name field, mutated by every declarator that has since reused it)
    // currently happens to hold; see the struct_union_decl comment (#897).
    if (!is_definition)
        return ty;

    // Assign offsets within the struct to members.
    int bits = 0;

    for (Member *mem = ty->members; mem; mem = mem->next) {
        if (mem->is_bitfield && mem->bit_width == 0) {
            // Zero-width anonymous bitfield has a special meaning: it forces
            // the next member onto a fresh storage unit -- this part is
            // universal, unconditional. Whether it *also* raises the
            // struct's own alignment to its declared type's is the
            // CCCC_ALIGN_ANON_BITFIELDS arch split above (see its comment):
            // true under real AAPCS64, false on x86_64 and Darwin/arm64,
            // and (unlike a plain unnamed member, #1127) this survives BOTH
            // `packed` and `#pragma pack(N)` wherever it applies at all
            // (`#pragma pack(1) struct{char c; int:0; char d;}` is 8/4 on
            // Linux/aarch64, not capped to 1/1 or 2/1).
            bits = align_to(bits, mem->ty->size * 8);
#if CCCC_ALIGN_ANON_BITFIELDS
            if (ty->align < mem->align)
                ty->align = mem->align;
#endif
        } else if (mem->is_bitfield) {
            int sz = mem->ty->size;

            // #1165: an explicit aligned(N) (struct_members(),
            // parse_types.c) on a bit-field aligns its *storage unit* --
            // verified against gcc-16/clang: `int b : 5
            // __attribute__((aligned(16)))` starts b's unit at the next
            // 16-byte boundary, same as an ordinary member's explicit
            // alignment (#1163). mem->explicit_align is 0 for every
            // ordinary bit-field (struct_members() rejects _Alignas on a
            // bit-field outright, and only a suffix aligned(N) sets it), so
            // this is a no-op for the common case.
            if (mem->explicit_align)
                bits = align_to(bits, mem->explicit_align * 8);

            if (bits / (sz * 8) != (bits + mem->bit_width - 1) / (sz * 8))
                bits = align_to(bits, sz * 8);

            mem->offset      = align_down(bits / 8, sz);
            mem->bit_offset  = bits % (sz * 8);
            bits            += mem->bit_width;

            // #1127: clang/gcc round a bitfield struct's size/alignment up to
            // cover the declared member type's own storage unit -- an
            // "int f : 5" reserves a full 4-byte int-sized slot, not just the
            // 5 bits actually used, even though the standard treats this as
            // implementation-defined rather than mandatory. cccc's member
            // *offsets* and bit packing above are already correct; only this
            // struct-level rounding was missing, silently under-allocating
            // every bitfield struct relative to every real-world ABI this
            // project targets (confirmed: `-c=native`/`-m` then emits a
            // `sizeof`-folded `malloc()` call too small for the struct the
            // host compiler itself lays out, a real heap overflow, not a
            // cosmetic-only mismatch).
            //
            // #1127: a nonzero-width bit-field's declared type's natural
            // alignment contributes to the struct's own alignment, same as
            // an ordinary member -- true for a *named* bit-field on every
            // target. For an *unnamed* one, this is the same
            // CCCC_ALIGN_ANON_BITFIELDS arch split as the width-0 arm above
            // (#1176 follow-up): `struct{char c; int:3; char d;}` is 4/4
            // under real AAPCS64 (matching a named `int x:3` exactly) and
            // 3/1 everywhere else (x86_64, Darwin/arm64 clang) -- verified
            // both compiler families agree on both sides. Suppressed by
            // `packed` and capped by `#pragma pack(N)` the same way as an
            // ordinary member, wherever it contributes at all (verified:
            // under `#pragma pack(1)` the struct is 3/1 even on Linux/
            // aarch64). Width-0 is NOT the same rule -- it contributes
            // unconditionally on the targets where it contributes at all,
            // even under `packed`/`pack(N)`; see the width-0 arm above.
            //
            // #1165: an *explicit* aligned(N) on a bit-field always raises
            // ty->align and survives `packed` (like #1163's non-bit-field
            // case) -- for a *named* bit-field, on every target (verified:
            // `struct { char a; int x : 5 __attribute__((aligned(16))); int
            // c; }` is 32/16 under both gcc and clang, every arch). For an
            // *unnamed* one it is gated exactly like the implicit case
            // above: `struct { char a; int : 5 __attribute__((aligned(16)));
            // int c; }` is 32/16 only under real AAPCS64 (and macOS/arm64
            // gcc-16's WONT_FIX outlier), 24/4 everywhere else -- verified
            // directly, both compiler families, all three architectures.
            int contributed = mem->explicit_align ? mem->explicit_align
                              : !ty->is_packed    ? mem->align
                                                  : 0;
            // #1173: unlike `packed` (which an explicit aligned(N) always
            // overrides, #1165 above), #pragma pack(N) caps EVERY
            // contribution -- explicit or implicit -- at N. Verified
            // directly against gcc-16/clang: `#pragma pack(2) struct { char
            // a; int b : 5 __attribute__((aligned(16))); int c; }` has
            // align 2, not 16; the same member under plain
            // __attribute__((packed)) instead raises align to 16.
            if (!ty->is_packed && ty->pack_align &&
                ty->pack_align < contributed)
                contributed = ty->pack_align;
            if (ty->align < contributed &&
                (mem->name || CCCC_ALIGN_ANON_BITFIELDS))
                ty->align = contributed;
        } else {
            // #1163: `packed` only suppresses a member's *implicit* (natural)
            // alignment -- an *explicit* aligned(N)/_Alignas(N) request on
            // the member's own declarator still applies (verified against
            // gcc-16/clang: __attribute__((packed)) struct { char a; int b
            // __attribute__((aligned(16))); } places b at offset 16, not 1).
            // mem->align alone can't distinguish the two cases (an explicit
            // request equal to the member's natural alignment looks
            // identical to no request at all), so use mem->explicit_align
            // under packed instead.
            int mem_align;
            if (ty->is_packed) {
                mem_align = mem->explicit_align ? mem->explicit_align : 1;
            } else {
                mem_align = mem->align;
                // #1173: unlike `packed` just above (which an explicit
                // aligned(N)/_Alignas(N) always overrides), #pragma
                // pack(N) caps EVERY contribution at N, explicit included
                // -- verified directly against gcc-16/clang: `#pragma
                // pack(2) struct { char c; int a __attribute__((aligned(
                // 16))); }` places `a` at offset 2 and has align 2, not
                // 16/16 the way plain __attribute__((packed)) would.
                if (ty->pack_align && ty->pack_align < mem_align)
                    mem_align = ty->pack_align;
            }

            // #1164: this align_to() must run even for a flexible array
            // member. It used to be skipped for one ("FAMs don't get
            // padding *before* them"), but that conflated two different
            // things: a FAM's *implicit* natural-alignment padding (which
            // real structs do skip, since a FAM never has storage of its
            // own to pad up to) versus simply rounding `bits` up to a whole
            // byte when it's sitting mid-byte after a bit-field run --
            // which every member, FAM or not, still needs. Skipping the
            // whole call left a mid-byte `bits` for align_down()/`/ 8`
            // below to truncate instead of round up, overlapping the FAM
            // with the bit-field run's own storage. Verified against
            // gcc-16: `struct __attribute__((packed)) { char a; int b : 5;
            // int c; }` (not even a FAM) places `c` at offset 2, and
            // `struct { char a; int b : 5; long c[]; }` places its FAM `c`
            // at offset 8 -- cccc previously computed 1 for both. Since a
            // FAM's mem_align already always feeds ty->align below
            // regardless of this branch, rounding `bits` up here for a FAM
            // is exactly "round to a whole byte" (mem_align 1 under
            // packed) or "round to natural alignment" (unpacked) -- not new
            // padding *before* the FAM's declared position, just correcting
            // the byte truncation. sizeof is unaffected either way: it was
            // already computed from ty->align, not from this align_to()
            // call.
            bits         = align_to(bits, mem_align * 8);
            mem->offset  = bits / 8;
            bits        += mem->ty->size * 8;

            // Update struct alignment (including for flexible arrays, for final
            // size padding)
            if (ty->align < mem_align)
                ty->align = mem_align;
        }
    }

    ty->size = align_to(bits, ty->align * 8) / 8;
    return install_tag_definition(vm, ty->struct_tag, ty, "struct");
}

// union-decl = struct-union-decl
static Type *union_decl(VirtualMachine *vm, Token **rest, Token *tok) {
    bool  is_definition = false;
    Type *ty = struct_union_decl(vm, rest, tok, TY_UNION, &is_definition);

    if (ty->size < 0)
        return ty;

    // See the matching comment in struct_decl above (#897).
    if (!is_definition)
        return ty;

    // If union, we don't have to assign offsets because they
    // are already initialized to zero. We need to compute the
    // alignment and the size though.
    //
    // #1176 follow-up: an *unnamed* bit-field member (width-0 or not) must
    // not feed either ty->align or ty->size here except under the same
    // CCCC_ALIGN_ANON_BITFIELDS arch split as struct_decl's bit-field arms
    // above -- verified directly: `union{char c; int:0;}` and `union{char
    // c; int:3;}` are both 1/1 on x86_64/Darwin-arm64-clang (neither
    // contributes) and 4/4 under real AAPCS64 and macOS/arm64 gcc-16's
    // WONT_FIX outlier (both do, same as an ordinary `int` member would).
    // A *named* bit-field member, and every non-bit-field member, is
    // unaffected on every target and keeps contributing unconditionally
    // below.
    //
    // #1163: unlike struct_decl above, this loop previously had no
    // is_packed check at all, so `packed` had no effect on a union's own
    // alignment. Match struct_decl's rule: a member's *implicit* alignment
    // is suppressed under packed, but an *explicit* aligned(N)/_Alignas(N)
    // request still applies (verified against gcc-16/clang).
    for (Member *mem = ty->members; mem; mem = mem->next) {
        if (mem->is_bitfield && !mem->name && !CCCC_ALIGN_ANON_BITFIELDS)
            continue;
        int mem_align;
        if (ty->is_packed) {
            mem_align = mem->explicit_align ? mem->explicit_align : 1;
        } else {
            mem_align = mem->align;
            // #1173: unlike `packed` above, #pragma pack(N) caps every
            // contribution -- explicit included -- at N (verified against
            // gcc-16/clang; see the matching comment in struct_decl).
            if (ty->pack_align && ty->pack_align < mem_align)
                mem_align = ty->pack_align;
        }
        if (ty->align < mem_align)
            ty->align = mem_align;
        if (ty->size < mem->ty->size)
            ty->size = mem->ty->size;
    }
    ty->size = align_to(ty->size, ty->align);
    return install_tag_definition(vm, ty->struct_tag, ty, "union");
}

// Find a struct member by name.
Member *get_struct_member(Type *ty, Token *tok) {
    for (Member *mem = ty->members; mem; mem = mem->next) {
        // Anonymous struct member
        if ((mem->ty->kind == TY_STRUCT || mem->ty->kind == TY_UNION) &&
            !mem->name) {
            if (get_struct_member(mem->ty, tok))
                return mem;
            continue;
        }

        // Pre-existing bug (unrelated to #1164/#1165, found and fixed while
        // testing them): an unnamed bit-field (`int : 5;`) is neither named
        // nor an anonymous struct/union, so it fell through to the "regular
        // struct member" branch below and dereferenced a NULL mem->name --
        // a crash on *any* member lookup that had to scan past it, not just
        // one that (mis)matched it. Skip it like the width-0 case already
        // implicitly is (it's pure padding, never a lookup target).
        if (!mem->name)
            continue;

        // Regular struct member
        if (mem->name->len == tok->len &&
            !strncmp(mem->name->loc, tok->loc, tok->len))
            return mem;
    }
    return NULL;
}
