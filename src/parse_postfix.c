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

// Postfix, unary, and primary expressions: struct/array/function-call
// postfix operators, _Generic, format-string validation, the backtick
// quasi-quote lexer, and primary() itself.

#include "./parse_internal.h"

static Node *postfix(VirtualMachine *vm, Token **rest, Token *tok);

// unary = ("+" | "-" | "*" | "&" | "!" | "~") cast
//       | ("++" | "--") unary
//       | "&&" ident
//       | "^" block-literal  (Apple blocks extension)
//       | postfix
Node *unary(VirtualMachine *vm, Token **rest, Token *tok) {
    // Apple blocks: ^{ ... } or ^(params){ ... } or ^returntype(params){ ... }
    if (equal(tok, "^") && tok->next &&
        (equal(tok->next, "{") || equal(tok->next, "(") ||
         is_typename(vm, tok->next))) {
        return block_literal(vm, rest, tok);
    }

    if (equal(tok, "+"))
        return cast(vm, rest, tok->next);

    if (equal(tok, "-"))
        return new_unary(vm, ND_NEG, cast(vm, rest, tok->next), tok);

    if (equal(tok, "&")) {
        Node *lhs = cast(vm, rest, tok->next);
        add_type(vm, lhs);
        if (lhs->kind == ND_MEMBER && !is_error_type(lhs->ty) && lhs->member &&
            lhs->member->is_bitfield) {
            if (vm->collect_errors &&
                error_tok_recover(vm, tok, "cannot take address of bitfield")) {
                // Return the member itself as an error placeholder
                return lhs;
            }
            error_tok(vm, tok, "cannot take address of bitfield");
        }
        return new_unary(vm, ND_ADDR, lhs, tok);
    }

    if (equal(tok, "*")) {
        // [https://www.sigbus.info/n1570#6.5.3.2p4] This is an oddity
        // in the C spec, but dereferencing a function shouldn't do
        // anything. If foo is a function, `*foo`, `**foo` or `*****foo`
        // are all equivalent to just `foo`.
        Node *node = cast(vm, rest, tok->next);
        add_type(vm, node);
        if (node->ty->kind == TY_FUNC)
            return node;
        Node *deref = new_unary(vm, ND_DEREF, node, tok);
        set_checked_deref_bounds(vm, deref, node, tok);
        return deref;
    }

    if (equal(tok, "!"))
        return new_unary(vm, ND_NOT, cast(vm, rest, tok->next), tok);

    if (equal(tok, "~"))
        return new_unary(vm, ND_BITNOT, cast(vm, rest, tok->next), tok);

    // Read ++i as i+=1
    if (equal(tok, "++"))
        return to_assign(vm, new_add(vm, unary(vm, rest, tok->next),
                                     new_num(vm, 1, tok), tok));

    // Read --i as i-=1
    if (equal(tok, "--"))
        return to_assign(vm, new_sub(vm, unary(vm, rest, tok->next),
                                     new_num(vm, 1, tok), tok));

    // [GNU] labels-as-values
    if (equal(tok, "&&")) {
        Node *node         = new_node(vm, ND_LABEL_VAL, tok);
        node->label        = get_ident(vm, tok->next);
        node->goto_next    = vm->compiler.gotos;
        vm->compiler.gotos = node;
        *rest              = tok->next->next;
        return node;
    }

    return postfix(vm, rest, tok);
}

// Create a node representing a struct member access, such as foo.bar
// where foo is a struct and bar is a member name.
//
// C has a feature called "anonymous struct" which allows a struct to
// have another unnamed struct as a member like this:
//
//   struct { struct { int a; }; int b; } x;
//
// The members of an anonymous struct belong to the outer struct's
// member namespace. Therefore, in the above example, you can access
// member "a" of the anonymous struct as "x.a".
//
// This function takes care of anonymous structs.
static Node *struct_ref(VirtualMachine *vm, Node *node, Token *tok) {
    add_type(vm, node);

    // If the base expression has error type, propagate it
    if (node->ty && is_error_type(node->ty)) {
        Node *err_node = new_node(vm, ND_MEMBER, tok);
        err_node->ty   = ty_error;
        return err_node;
    }

    if (node->ty->kind != TY_STRUCT && node->ty->kind != TY_UNION) {
        if (vm->collect_errors &&
            error_tok_recover(vm, node->tok, "not a struct nor a union")) {
            Node *err_node = new_node(vm, ND_MEMBER, tok);
            err_node->ty   = ty_error;
            return err_node;
        }
        error_tok(vm, node->tok, "not a struct nor a union");
    }

    Type *ty = node->ty;

    // A qualified copy (e.g. "const struct S *") made while the tag S was
    // still incomplete keeps its own empty member list; the canonical tag it
    // was copied from (origin) is completed in place when S is later defined.
    // Follow the origin chain to reach that completed definition so member
    // lookup succeeds.  The const-ness of the access is taken from the
    // original node->lhs->ty in add_type, so dropping qualifiers here is safe.
    while (ty->size < 0 && ty->origin &&
           (ty->origin->kind == TY_STRUCT || ty->origin->kind == TY_UNION))
        ty = ty->origin;

    for (;;) {
        Member *mem = get_struct_member(ty, tok);
        if (!mem) {
            if (vm->collect_errors &&
                error_tok_recover(vm, tok, "no such member '%.*s'", tok->len,
                                  tok->loc)) {
                Node *err_node = new_node(vm, ND_MEMBER, tok);
                err_node->ty   = ty_error;
                return err_node;
            }
            error_tok(vm, tok, "no such member '%.*s'", tok->len, tok->loc);
        }
        node         = new_unary(vm, ND_MEMBER, node, tok);
        node->member = mem;
        if (mem->name)
            break;
        ty = mem->ty;
    }
    return node;
}

// Convert A++ to `(typeof A)((A += 1) - 1)`
static Node *new_inc_dec(VirtualMachine *vm, Node *node, Token *tok,
                         int addend) {
    add_type(vm, node);
    return new_cast(
        vm,
        new_add(vm,
                to_assign(vm, new_add(vm, node, new_num(vm, addend, tok), tok)),
                new_num(vm, -addend, tok), tok),
        node->ty);
}

static Type *compound_literal_type(VirtualMachine *vm, Token **rest, Token *tok,
                                   VarAttr *attr) {
    bool saw_register = false;
    bool saw_auto     = false;
    for (Token *p = tok; !equal(p, ")") && p->kind != TK_EOF; p = p->next) {
        DeclKw dk = declspec_kw(p);
        if (dk == DK_REGISTER)
            saw_register = true;
        else if (dk == DK_AUTO)
            saw_auto = true;
    }

    Type *ty = declspec(vm, &tok, tok, attr);

    if (saw_auto || attr->is_typedef || attr->is_extern || attr->is_inline ||
        attr->is_block_var)
        error_tok(vm, tok, "invalid storage class in compound literal");
    if ((saw_register || attr->is_static || attr->is_tls ||
         attr->is_constexpr) &&
        vm->compiler.c_std < CCCC_STD_C23)
        error_tok(vm, tok,
                  "compound literal storage classes are only available in C23");

    ty = abstract_declarator(vm, rest, tok, ty);
    if (attr->is_constexpr) {
        ty           = copy_type(vm, ty);
        ty->is_const = true;
    }
    return ty;
}

bool is_compound_literal_head(VirtualMachine *vm, Token *tok) {
    if (!equal(tok, "(") || !is_typename(vm, tok->next))
        return false;

    int depth = 1;
    for (Token *p = tok->next; p && p->kind != TK_EOF; p = p->next) {
        if (equal(p, "("))
            depth++;
        else if (equal(p, ")")) {
            depth--;
            if (depth == 0)
                return equal(p->next, "{");
        }
    }
    return false;
}

static Node *funcall(VirtualMachine *vm, Token **rest, Token *tok, Node *fn);
static Node *primary(VirtualMachine *vm, Token **rest, Token *tok);

// postfix = "(" type-name ")" "{" initializer-list "}"
//         = ident "(" func-args ")" postfix-tail*
//         | primary postfix-tail*
//
// postfix-tail = "[" expr "]"
//              | "(" func-args ")"
//              | "." ident
//              | "->" ident
//              | "++"
//              | "--"
static Node *postfix(VirtualMachine *vm, Token **rest, Token *tok) {
    Node *node;

    if (is_compound_literal_head(vm, tok)) {
        // Compound literal
        Token *start = tok;
        if (vm->compiler.c_std < CCCC_STD_C99)
            warn_tok(vm, start, CCCC_WARN_PEDANTIC,
                     "compound literals are a C99 extension");
        VarAttr attr = {};
        Type   *ty   = compound_literal_type(vm, &tok, tok->next, &attr);
        tok          = skip(vm, tok, ")");

        // A compound literal used inside a global/static initializer must
        // itself resolve to a constant, even when it has no explicit
        // storage-class specifier and lexical scope isn't file scope (e.g.
        // `static struct P b = (struct P){5,6};` inside a function) --
        // in_const_gvar_init forces the anonymous-constant-global path here
        // instead of materializing a real (nonsensical) auto-storage local
        // (#720).
        if (vm->compiler.scope->next == NULL || attr.is_static ||
            attr.is_constexpr || attr.is_tls ||
            vm->compiler.in_const_gvar_init) {
            Obj *var                 = new_anon_gvar(vm, ty);
            var->is_constexpr        = attr.is_constexpr;
            var->is_static           = attr.is_static || attr.is_constexpr;
            var->is_tls              = attr.is_tls;
            var->is_local_symbol     = vm->compiler.scope->next != NULL;
            var->is_compound_literal = true;
            gvar_initializer(vm, rest, tok, var);
            node = new_var_node(vm, var, start);
        } else {
            Obj  *var = new_lvar(vm, "", 0, ty);
            Node *lhs = lvar_initializer(vm, rest, tok, var);
            node = new_binary(vm, ND_COMMA, lhs, new_var_node(vm, var, start),
                              start);
        }
        // #1112: a compound literal is a postfix-expression like any other
        // -- `.member` / `[index]` / `->member` bind tighter than any unary
        // operator above it (C99 6.5.2p5), so it must run through the same
        // tail loop primary()-based expressions get. This used to return
        // directly from each literal branch, making `(struct P){30,12}.x`
        // a syntax error ("expected ','") even though real compilers accept
        // it; only the parenthesized spelling parsed. The initializers left
        // *rest just past the literal's closing '}' -- resume there.
        tok = *rest;
    } else {
        node = primary(vm, &tok, tok);
    }

    for (;;) {
        if (equal(tok, "(")) {
            // Check if this is a block invocation
            add_type(vm, node);
            if (node->ty && node->ty->kind == TY_BLOCK) {
                // Block invocation: create ND_BLOCK_CALL
                Token *start = tok;
                tok          = tok->next; // Skip '('

                // Parse arguments
                Node  head = {};
                Node *cur  = &head;
                while (!equal(tok, ")")) {
                    if (cur != &head)
                        tok = skip(vm, tok, ",");
                    Node *arg = assign(vm, &tok, tok);
                    cur = cur->next = arg;
                }
                tok        = tok->next; // Skip ')'

                Node *call = new_node(vm, ND_BLOCK_CALL, start);
                call->lhs  = node;
                call->args = head.next;
                call->ty = node->ty->return_ty ? node->ty->return_ty : ty_void;
                node     = call;
            } else {
                node = funcall(vm, &tok, tok->next, node);
            }
            continue;
        }

        if (equal(tok, "[")) {
            // x[y] is short for *(x+y)
            Token *start = tok;
            Node  *idx   = expr(vm, &tok, tok->next);

            // Try to recover if ']' is missing
            if (!equal(tok, "]")) {
                if (vm->collect_errors &&
                    error_tok_recover(vm, tok, "expected ']'")) {
                    // Use index 0 as placeholder and continue
                    idx = new_num(vm, 0, tok);
                } else {
                    tok = skip(vm, tok, "]");
                }
            } else {
                tok = tok->next;
            }

            // GNU vector_size subscript (tracker #72): v[i] reads/writes a
            // lane. Unlike arrays, a vector type's `base` is its element
            // type but the vector itself does NOT decay to a pointer (it's
            // a register/memory-slot value, not addressable-by-default the
            // way an array is) -- so this must be intercepted before
            // new_add(), which would otherwise mis-treat vec->base as
            // pointer arithmetic. Lower to element address: &v, cast to
            // element-pointer, then ordinary pointer-offset + deref (this
            // supports a runtime-variable index for free, and reuses the
            // scalar load/store path -- no vector opcode needed here).
            add_type(vm, node);
            if (node->ty && is_vector(node->ty)) {
                Type *elem_ty = node->ty->base;
                Node *addr    = new_unary(vm, ND_ADDR, node, start);
                addr          = new_cast(vm, addr, pointer_to(vm, elem_ty));
                node = new_unary(vm, ND_DEREF, new_add(vm, addr, idx, start),
                                 start);
                continue;
            }

            {
                Node *base_addr = node;
                Node *deref = new_unary(vm, ND_DEREF,
                                        new_add(vm, node, idx, start), start);
                set_checked_deref_bounds(vm, deref, base_addr, start);
                node = deref;
            }
            continue;
        }

        if (equal(tok, ".")) {
            node = struct_ref(vm, node, tok->next);
            tok  = tok->next->next;
            continue;
        }

        if (equal(tok, "->")) {
            // x->y is short for (*x).y
            Node *base_addr = node;
            Node *deref     = new_unary(vm, ND_DEREF, node, tok);
            set_checked_deref_bounds(vm, deref, base_addr, tok);
            node = struct_ref(vm, deref, tok->next);
            tok  = tok->next->next;
            continue;
        }

        if (equal(tok, "++")) {
            node = new_inc_dec(vm, node, tok, 1);
            tok  = tok->next;
            continue;
        }

        if (equal(tok, "--")) {
            node = new_inc_dec(vm, node, tok, -1);
            tok  = tok->next;
            continue;
        }

        *rest = tok;
        return node;
    }
}

// Expected argument types for format string validation
enum {
    FMT_EXPECT_INT,
    FMT_EXPECT_UINT,
    FMT_EXPECT_DOUBLE,
    FMT_EXPECT_STRING,    // char*
    FMT_EXPECT_POINTER,   // void*
    FMT_EXPECT_INT_PTR,   // int* (for %n, scanf %d)
    FMT_EXPECT_UINT_PTR,  // unsigned int* (scanf %u, %x)
    FMT_EXPECT_FLOAT_PTR, // float* (scanf %f)
    // length-modifier-aware printf variants
    FMT_EXPECT_LONG,    // %ld, %lld, %jd, %td
    FMT_EXPECT_ULONG,   // %lu, %llu, %zu, %ju
    FMT_EXPECT_LDOUBLE, // %Lf, %Le, %Lg, %La
    // length-modifier-aware scanf pointer variants
    FMT_EXPECT_LONG_PTR,    // scanf %ld → long *
    FMT_EXPECT_ULONG_PTR,   // scanf %lu, %zu → unsigned long *
    FMT_EXPECT_SHORT_PTR,   // scanf %hd → short *
    FMT_EXPECT_SCHAR_PTR,   // scanf %hhd → char *
    FMT_EXPECT_LDOUBLE_PTR, // scanf %Lf → long double *
    // #829: decimal length-modifier variants (%Hf/%Df/%DDf)
    FMT_EXPECT_DECIMAL32,      // %Hf
    FMT_EXPECT_DECIMAL64,      // %Df
    FMT_EXPECT_DECIMAL128,     // %DDf
    FMT_EXPECT_DECIMAL32_PTR,  // scanf %Hf → _Decimal32 *
    FMT_EXPECT_DECIMAL64_PTR,  // scanf %Df → _Decimal64 *
    FMT_EXPECT_DECIMAL128_PTR, // scanf %DDf → _Decimal128 *
};

#define MAX_FMT_ARGS 64

static const char *fmt_type_names[] = {"int",
                                       "unsigned int",
                                       "double",
                                       "char *",
                                       "void *",
                                       "int *",
                                       "unsigned int *",
                                       "float *",
                                       "long",
                                       "unsigned long",
                                       "long double",
                                       "long *",
                                       "unsigned long *",
                                       "short *",
                                       "char *",
                                       "long double *",
                                       "_Decimal32",
                                       "_Decimal64",
                                       "_Decimal128",
                                       "_Decimal32 *",
                                       "_Decimal64 *",
                                       "_Decimal128 *"};

// Validate format string arguments for __attribute__((format(...)))
static void validate_format_call(VirtualMachine *vm, Token *tok, Type *func_ty,
                                 Node *args) {
    if (!func_ty->format_style)
        return;

    // Walk to the format string argument (0-based index)
    Node *fmt_arg = args;
    int   idx     = 0;
    while (fmt_arg && idx < func_ty->format_string_index - 1) {
        fmt_arg = fmt_arg->next;
        idx++;
    }

    if (!fmt_arg)
        return;

    // String literals may be wrapped in ND_CAST for array-to-pointer decay
    if (fmt_arg->kind == ND_CAST)
        fmt_arg = fmt_arg->lhs;

    if (fmt_arg->kind != ND_VAR || !fmt_arg->var || !fmt_arg->var->init_data)
        return;

    const char *fmt   = fmt_arg->var->init_data;
    int         style = func_ty->format_style;

    // Count variadic args
    Node *vararg      = args;
    int   vararg_idx  = 0;
    int   num_varargs = 0;
    while (vararg) {
        vararg_idx++;
        if (vararg_idx >= func_ty->format_fmt_first_arg)
            num_varargs++;
        vararg = vararg->next;
    }

    // Parse format string and collect expected types
    int         fmt_count = 0;
    int         expected[MAX_FMT_ARGS];
    int         num_expected = 0;

    const char *p            = fmt;
    while (*p && num_expected < MAX_FMT_ARGS) {
        if (*p == '%') {
            p++;
            if (*p == '%') {
                p++;
                continue;
            }

            if (*p == '*') {
                expected[num_expected++] = FMT_EXPECT_INT;
                fmt_count++;
                p++;
            }
            while (*p >= '0' && *p <= '9')
                p++;
            if (*p == '.') {
                p++;
                if (*p == '*') {
                    expected[num_expected++] = FMT_EXPECT_INT;
                    fmt_count++;
                    p++;
                } else {
                    while (*p >= '0' && *p <= '9')
                        p++;
                }
            }
            // Capture length modifier (h, hh, l, ll, L, z, j, t, and the
            // #829 decimal modifiers H, D, DD)
            const char *mod_start = p;
            while (*p == 'h' || *p == 'l' || *p == 'L' || *p == 'z' ||
                   *p == 'j' || *p == 't' || *p == 'H' || *p == 'D')
                p++;
            int  mod_len = (int)(p - mod_start);
            char mod0    = mod_len > 0 ? mod_start[0] : 0;
            char mod1    = mod_len > 1 ? mod_start[1] : 0;
            // mod: 0=none,1=hh,2=h,3=l,4=ll,5=L,6=z,7=j,8=t,
            //      9=H(_Decimal32),10=D(_Decimal64),11=DD(_Decimal128)
            int mod = 0;
            if (mod_len == 0)
                mod = 0;
            else if (mod0 == 'h' && mod1 == 'h')
                mod = 1;
            else if (mod0 == 'h')
                mod = 2;
            else if (mod0 == 'l' && mod1 == 'l')
                mod = 4;
            else if (mod0 == 'l')
                mod = 3;
            else if (mod0 == 'L')
                mod = 5;
            else if (mod0 == 'z')
                mod = 6;
            else if (mod0 == 'j')
                mod = 7;
            else if (mod0 == 't')
                mod = 8;
            else if (mod0 == 'H')
                mod = 9;
            else if (mod0 == 'D' && mod1 == 'D')
                mod = 11;
            else if (mod0 == 'D')
                mod = 10;
            bool mod_is_decimal = (mod == 9 || mod == 10 || mod == 11);

            if (*p) {
                char c = *p;
                // A decimal length modifier only makes sense on a floating
                // conversion; %Dd, %Da, etc. are diagnosed here rather than
                // silently falling through to the integer/hex-float default.
                if (mod_is_decimal && c != 'f' && c != 'F' && c != 'e' &&
                    c != 'E' && c != 'g' && c != 'G')
                    warn_tok(vm, tok, CCCC_WARN_FORMAT,
                             "conversion '%c' does not accept a decimal "
                             "length modifier",
                             c);
                if (style == 1) {
                    switch (c) {
                        case 'd':
                        case 'i':
                        case 'c':
                            // l/ll/j/t → long; h/hh/none → int (promoted)
                            if (mod == 3 || mod == 4 || mod == 7 || mod == 8)
                                expected[num_expected++] = FMT_EXPECT_LONG;
                            else
                                expected[num_expected++] = FMT_EXPECT_INT;
                            break;
                        case 'u':
                        case 'o':
                        case 'x':
                        case 'X':
                            // l/ll/z/j → unsigned long; h/hh/none → unsigned
                            // int (promoted)
                            if (mod == 3 || mod == 4 || mod == 6 || mod == 7)
                                expected[num_expected++] = FMT_EXPECT_ULONG;
                            else
                                expected[num_expected++] = FMT_EXPECT_UINT;
                            break;
                        case 'f':
                        case 'F':
                        case 'e':
                        case 'E':
                        case 'g':
                        case 'G':
                        case 'a':
                        case 'A':
                            // L → long double; H/D/DD → _Decimal32/64/128;
                            // none → double (float promoted)
                            if (mod == 5)
                                expected[num_expected++] = FMT_EXPECT_LDOUBLE;
                            else if (mod == 9)
                                expected[num_expected++] = FMT_EXPECT_DECIMAL32;
                            else if (mod == 10)
                                expected[num_expected++] = FMT_EXPECT_DECIMAL64;
                            else if (mod == 11)
                                expected[num_expected++] =
                                    FMT_EXPECT_DECIMAL128;
                            else
                                expected[num_expected++] = FMT_EXPECT_DOUBLE;
                            break;
                        case 's':
                            expected[num_expected++] = FMT_EXPECT_STRING;
                            break;
                        case 'p':
                            expected[num_expected++] = FMT_EXPECT_POINTER;
                            break;
                        case 'n':
                            expected[num_expected++] = FMT_EXPECT_INT_PTR;
                            break;
                        default:
                            expected[num_expected++] = FMT_EXPECT_INT;
                            break;
                    }
                } else if (style == 2) {
                    switch (c) {
                        case 'd':
                        case 'i':
                            if (mod == 3 || mod == 4 || mod == 7 || mod == 8)
                                expected[num_expected++] = FMT_EXPECT_LONG_PTR;
                            else if (mod == 2)
                                expected[num_expected++] = FMT_EXPECT_SHORT_PTR;
                            else if (mod == 1)
                                expected[num_expected++] = FMT_EXPECT_SCHAR_PTR;
                            else
                                expected[num_expected++] = FMT_EXPECT_INT_PTR;
                            break;
                        case 'u':
                        case 'o':
                        case 'x':
                        case 'X':
                            if (mod == 3 || mod == 4 || mod == 6 || mod == 7)
                                expected[num_expected++] = FMT_EXPECT_ULONG_PTR;
                            else
                                expected[num_expected++] = FMT_EXPECT_UINT_PTR;
                            break;
                        case 'f':
                        case 'F':
                        case 'e':
                        case 'E':
                        case 'g':
                        case 'G':
                        case 'a':
                        case 'A':
                            if (mod == 5)
                                expected[num_expected++] =
                                    FMT_EXPECT_LDOUBLE_PTR;
                            else if (mod == 9)
                                expected[num_expected++] =
                                    FMT_EXPECT_DECIMAL32_PTR;
                            else if (mod == 10)
                                expected[num_expected++] =
                                    FMT_EXPECT_DECIMAL64_PTR;
                            else if (mod == 11)
                                expected[num_expected++] =
                                    FMT_EXPECT_DECIMAL128_PTR;
                            else
                                expected[num_expected++] = FMT_EXPECT_FLOAT_PTR;
                            break;
                        case 's':
                        case 'c':
                            expected[num_expected++] = FMT_EXPECT_STRING;
                            break;
                        case 'p':
                            expected[num_expected++] = FMT_EXPECT_POINTER;
                            break;
                        case 'n':
                            expected[num_expected++] = FMT_EXPECT_INT_PTR;
                            break;
                        default:
                            expected[num_expected++] = FMT_EXPECT_INT_PTR;
                            break;
                    }
                }
                fmt_count++;
                p++;
            }
        } else {
            p++;
        }
    }

    // Count remaining specifiers beyond MAX_FMT_ARGS
    while (*p) {
        if (*p == '%') {
            p++;
            if (*p == '%') {
                p++;
                continue;
            }
            if (*p == '*') {
                fmt_count++;
                p++;
            }
            while (*p >= '0' && *p <= '9')
                p++;
            if (*p == '.') {
                p++;
                if (*p == '*') {
                    fmt_count++;
                    p++;
                } else
                    while (*p >= '0' && *p <= '9')
                        p++;
            }
            while (*p == 'h' || *p == 'l' || *p == 'L' || *p == 'z' ||
                   *p == 'j' || *p == 't' || *p == 'H' || *p == 'D')
                p++;
            if (*p) {
                fmt_count++;
                p++;
            }
        } else {
            p++;
        }
    }

    // Check arg count
    if (fmt_count != num_varargs) {
        if (fmt_count < num_varargs)
            warn_tok(vm, tok, CCCC_WARN_FORMAT,
                     "too many arguments for format string "
                     "(format expects %d, call provides %d)",
                     fmt_count, num_varargs);
        else
            warn_tok(vm, tok, CCCC_WARN_FORMAT,
                     "too few arguments for format string "
                     "(format expects %d, call provides %d)",
                     fmt_count, num_varargs);
        return;
    }

    // Type-check each variadic argument
    int check_idx = 0;
    vararg        = args;
    vararg_idx    = 0;
    while (vararg && check_idx < num_expected) {
        vararg_idx++;
        if (vararg_idx >= func_ty->format_fmt_first_arg) {
            int   exp    = expected[check_idx];
            Type *arg_ty = vararg->ty;
            bool  ok     = true;
            switch (exp) {
                case FMT_EXPECT_INT:
                    ok = (arg_ty->kind == TY_INT || arg_ty->kind == TY_CHAR ||
                          arg_ty->kind == TY_SHORT || arg_ty->kind == TY_LONG);
                    break;
                case FMT_EXPECT_UINT:
                    ok = arg_ty->is_unsigned &&
                         (arg_ty->kind == TY_INT || arg_ty->kind == TY_CHAR ||
                          arg_ty->kind == TY_SHORT || arg_ty->kind == TY_LONG);
                    break;
                case FMT_EXPECT_DOUBLE:
                    ok = (arg_ty->kind == TY_DOUBLE ||
                          arg_ty->kind == TY_FLOAT ||
                          arg_ty->kind == TY_LDOUBLE);
                    break;
                case FMT_EXPECT_STRING:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          arg_ty->base->kind == TY_CHAR);
                    break;
                case FMT_EXPECT_POINTER:
                    ok = (arg_ty->kind == TY_PTR);
                    break;
                case FMT_EXPECT_INT_PTR:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          (arg_ty->base->kind == TY_INT ||
                           arg_ty->base->kind == TY_CHAR ||
                           arg_ty->base->kind == TY_SHORT ||
                           arg_ty->base->kind == TY_LONG));
                    break;
                case FMT_EXPECT_UINT_PTR:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          arg_ty->base->is_unsigned &&
                          (arg_ty->base->kind == TY_INT ||
                           arg_ty->base->kind == TY_CHAR ||
                           arg_ty->base->kind == TY_SHORT ||
                           arg_ty->base->kind == TY_LONG));
                    break;
                case FMT_EXPECT_FLOAT_PTR:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          (arg_ty->base->kind == TY_FLOAT ||
                           arg_ty->base->kind == TY_DOUBLE ||
                           arg_ty->base->kind == TY_LDOUBLE));
                    break;
                case FMT_EXPECT_LONG:
                    ok = (arg_ty->kind == TY_LONG);
                    break;
                case FMT_EXPECT_ULONG:
                    ok = (arg_ty->kind == TY_LONG && arg_ty->is_unsigned);
                    break;
                case FMT_EXPECT_LDOUBLE:
                    ok = (arg_ty->kind == TY_LDOUBLE);
                    break;
                case FMT_EXPECT_LONG_PTR:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          arg_ty->base->kind == TY_LONG &&
                          !arg_ty->base->is_unsigned);
                    break;
                case FMT_EXPECT_ULONG_PTR:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          arg_ty->base->kind == TY_LONG &&
                          arg_ty->base->is_unsigned);
                    break;
                case FMT_EXPECT_SHORT_PTR:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          arg_ty->base->kind == TY_SHORT);
                    break;
                case FMT_EXPECT_SCHAR_PTR:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          arg_ty->base->kind == TY_CHAR);
                    break;
                case FMT_EXPECT_LDOUBLE_PTR:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          arg_ty->base->kind == TY_LDOUBLE);
                    break;
                case FMT_EXPECT_DECIMAL32:
                    ok = (arg_ty->kind == TY_DECIMAL32);
                    break;
                case FMT_EXPECT_DECIMAL64:
                    ok = (arg_ty->kind == TY_DECIMAL64);
                    break;
                case FMT_EXPECT_DECIMAL128:
                    ok = (arg_ty->kind == TY_DECIMAL128);
                    break;
                case FMT_EXPECT_DECIMAL32_PTR:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          arg_ty->base->kind == TY_DECIMAL32);
                    break;
                case FMT_EXPECT_DECIMAL64_PTR:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          arg_ty->base->kind == TY_DECIMAL64);
                    break;
                case FMT_EXPECT_DECIMAL128_PTR:
                    ok = (arg_ty->kind == TY_PTR && arg_ty->base &&
                          arg_ty->base->kind == TY_DECIMAL128);
                    break;
            }
            if (!ok)
                warn_tok(vm, tok, CCCC_WARN_FORMAT,
                         "format argument %d expected type '%s' "
                         "but argument has incompatible type",
                         check_idx + 1, fmt_type_names[exp]);
            check_idx++;
        }
        vararg = vararg->next;
    }
}

// funcall = (assign ("," assign)*)? ")"
static Node *funcall(VirtualMachine *vm, Token **rest, Token *tok, Node *fn) {
    add_type(vm, fn);

    if (fn->ty->kind != TY_FUNC &&
        (fn->ty->kind != TY_PTR || fn->ty->base->kind != TY_FUNC))
        error_tok(vm, fn->tok, "not a function");

    Type *ty              = (fn->ty->kind == TY_FUNC) ? fn->ty : fn->ty->base;
    Type *param_ty        = ty->params;

    Node  head            = {};
    Node *cur             = &head;
    bool  deferred_splice = false;

    while (!equal(tok, ")")) {
        if (cur != &head)
            tok = skip(vm, tok, ",");

        Node *arg = assign(vm, &tok, tok);
        add_type(vm, arg);

        // Detect $@k splice placeholder: defer all arity/cast checks from here
        // on.
        if (!deferred_splice && arg->kind == ND_VAR && arg->var &&
            arg->var->is_splice_placeholder)
            deferred_splice = true;

        if (!deferred_splice) {
            if (!param_ty && !ty->is_variadic) {
                if (vm->collect_errors &&
                    error_tok_recover(vm, tok, "too many arguments")) {
                    // Continue parsing to find more errors, but don't add this
                    // arg
                    continue;
                }
                error_tok(vm, tok, "too many arguments");
            }

            if (param_ty) {
                // [static N] minimum-size check: only enforceable when the
                // argument is a compile-time-sized array; bare pointers are
                // best-effort.
                if (param_ty->static_min > 0 && arg->ty->kind == TY_ARRAY &&
                    arg->ty->array_len >= 0 &&
                    arg->ty->array_len < param_ty->static_min) {
                    warn_tok(vm, tok, CCCC_WARN_STATIC_ARRAY_SIZE,
                             "array argument has %d element%s but parameter "
                             "requires at least %d",
                             arg->ty->array_len,
                             arg->ty->array_len == 1 ? "" : "s",
                             param_ty->static_min);
                }

                if (param_ty->kind != TY_STRUCT && param_ty->kind != TY_UNION) {
                    warn_implicit_conversion(vm, arg, param_ty, tok);
                    arg = new_cast(vm, arg, param_ty);
                }
                param_ty = param_ty->next;
            } else if (arg->ty->kind == TY_FLOAT) {
                // If parameter type is omitted (e.g. in "..."), float
                // arguments are promoted to double.
                arg = new_cast(vm, arg, ty_double);
            }
        }

        cur = cur->next = arg;
    }

    if (!deferred_splice && param_ty) {
        if (vm->collect_errors &&
            error_tok_recover(vm, tok, "too few arguments")) {
            // Create placeholder arguments for missing parameters
            while (param_ty) {
                Node *placeholder = new_node(vm, ND_NUM, tok);
                placeholder->ty   = param_ty;
                placeholder->val  = 0;
                cur = cur->next = placeholder;
                param_ty        = param_ty->next;
            }
        } else {
            error_tok(vm, tok, "too few arguments");
        }
    }

    // __attribute__((error("msg"))): emit a compile-time error on every live
    // call. Suppressed when dead_code_depth > 0, i.e. the call site is inside a
    // statically-dead branch identified by static_branch_value() at the
    // enclosing if-statement.  Under collect_errors recovery a live error_tok
    // may longjmp past the decrement — but dead branches never trigger
    // error_tok, so the counter stays balanced.
    if (ty->attr_error_msg && vm->compiler.dead_code_depth == 0)
        error_tok(vm, fn->tok, "%s", ty->attr_error_msg);

    // __attribute__((warning("msg"))): emit a compile-time warning on every
    // live call.
    if (ty->attr_warning_msg && vm->compiler.dead_code_depth == 0)
        warn_tok(vm, fn->tok, CCCC_WARN_ATTRIBUTES, "%s", ty->attr_warning_msg);

    // Validate format string arguments when -F is active
    if (!deferred_splice && (vm->flags & CCCC_FORMAT_STR_CHECKS))
        validate_format_call(vm, tok, ty, head.next);

    // __attribute__((nonnull)): warn on statically-provable-null arguments.
    if (!deferred_splice && vm->compiler.dead_code_depth == 0 &&
        (vm->compiler.warnings & CCCC_WARN_NONNULL))
        validate_nonnull_call(vm, ty, head.next);

    // __attribute__((sentinel)): warn on a missing/non-literal NULL terminator.
    if (!deferred_splice && vm->compiler.dead_code_depth == 0 &&
        (vm->compiler.warnings & CCCC_WARN_SENTINEL))
        validate_sentinel_call(vm, tok, ty, head.next);

    if ((vm->compiler.warnings & CCCC_WARN_SIZEOF_POINTER_MEMACCESS) &&
        !deferred_splice && fn->kind == ND_VAR && fn->var && fn->var->name) {
        const char *fname = fn->var->name;
        if (strcmp(fname, "memset") == 0 || strcmp(fname, "memcpy") == 0 ||
            strcmp(fname, "memmove") == 0 || strcmp(fname, "memcmp") == 0) {
            Node *a = head.next;
            for (int i = 0; a && i < 2; i++)
                a = a->next;
            // strip any implicit cast to the parameter type to reach the sizeof
            // node
            Node *inner = a;
            while (inner && inner->kind == ND_CAST)
                inner = inner->lhs;
            if (inner && inner->is_sizeof_ptr_expr)
                warn_tok(vm, fn->tok, CCCC_WARN_SIZEOF_POINTER_MEMACCESS,
                         "argument to '%s' is the size of a pointer; "
                         "use sizeof(*ptr) or sizeof(pointed-to type) instead",
                         fname);
        }
    }

    *rest                = skip(vm, tok, ")");

    Node *node           = new_unary(vm, ND_FUNCALL, fn, tok);
    node->func_ty        = ty;
    node->ty             = ty->return_ty;
    node->args           = head.next;
    node->has_splice_arg = deferred_splice;

    // If a function returns a struct, it is caller's responsibility
    // to allocate a space for the return value.
    if (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION)
        node->ret_buffer = new_lvar(vm, "", 0, node->ty);
    return node;
}

// generic-selection = "(" assign "," generic-assoc ("," generic-assoc)* ")"
//
// generic-assoc = type-name ":" assign
//               | "default" ":" assign
static Node *generic_selection(VirtualMachine *vm, Token **rest, Token *tok) {
    Token *start = tok;
    tok          = skip(vm, tok, "(");

    Node *ctrl   = assign(vm, &tok, tok);
    add_type(vm, ctrl);

    Type *t1 = ctrl->ty;
    if (t1->kind == TY_FUNC)
        t1 = pointer_to(vm, t1);
    else if (t1->kind == TY_ARRAY)
        t1 = pointer_to(vm, t1->base);
    t1                 = copy_type(vm, t1);
    t1->is_const       = false;
    t1->is_volatile    = false;
    t1->origin         = NULL;

    Node *match        = NULL;
    Node *default_node = NULL;

    while (!consume(vm, rest, tok, ")")) {
        tok = skip(vm, tok, ",");

        if (equal(tok, "default")) {
            tok        = skip(vm, tok->next, ":");
            Node *node = assign(vm, &tok, tok);
            if (!default_node)
                default_node = node;
            continue;
        }

        Type *t2   = typename(vm, &tok, tok);
        tok        = skip(vm, tok, ":");
        Node *node = assign(vm, &tok, tok);
        if (!match && is_compatible(t1, t2))
            match = node;
    }

    Node *ret = match ? match : default_node;
    if (!ret)
        error_tok(vm, start,
                  "controlling expression type not compatible with"
                  " any generic association type");
    return ret;
}

// primary = "(" "{" stmt+ "}" ")"
//         | "(" expr ")"
//         | "sizeof" "(" type-name ")"
//         | "sizeof" unary
//         | "_Alignof" "(" type-name ")"
//         | "_Alignof" unary
//         | "_Generic" generic-selection
//         | "__builtin_types_compatible_p" "(" type-name, type-name, ")"
//         | "__builtin_reg_class" "(" type-name ")"
//         | backtick-quasi-quote
//         | ident
//         | str
//         | num
typedef enum {
    BT_LEX_NORMAL,
    BT_LEX_SQUOTE,
    BT_LEX_DQUOTE,
    BT_LEX_LINE_COMMENT,
    BT_LEX_BLOCK_COMMENT,
} BacktickLexState;

static void validate_backtick_fragment(VirtualMachine *vm, Token *fragment,
                                       BacktickLexState *state) {
    char *p = fragment->str;

    while (*p) {
        if (*state == BT_LEX_LINE_COMMENT) {
            if (*p++ == '\n')
                *state = BT_LEX_NORMAL;
            continue;
        }

        if (*state == BT_LEX_BLOCK_COMMENT) {
            if (p[0] == '*' && p[1] == '/') {
                *state  = BT_LEX_NORMAL;
                p      += 2;
            } else {
                p++;
            }
            continue;
        }

        if (*state == BT_LEX_SQUOTE || *state == BT_LEX_DQUOTE) {
            char quote = (*state == BT_LEX_SQUOTE) ? '\'' : '"';
            if (*p == '\\' && p[1]) {
                p += 2;
            } else if (*p++ == quote) {
                *state = BT_LEX_NORMAL;
            }
            continue;
        }

        if (p[0] == '/' && p[1] == '/') {
            *state  = BT_LEX_LINE_COMMENT;
            p      += 2;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            *state  = BT_LEX_BLOCK_COMMENT;
            p      += 2;
            continue;
        }
        if (*p == '\'') {
            *state = BT_LEX_SQUOTE;
            p++;
            continue;
        }
        if (*p == '"') {
            *state = BT_LEX_DQUOTE;
            p++;
            continue;
        }

        if (*p == '$' &&
            (isdigit((unsigned char)p[1]) || p[1] == '$' || p[1] == '@'))
            error_tok(vm, fragment,
                      "legacy Quote placeholders are not allowed in backtick "
                      "quasi-quotes; use ${...} or Quote(...)");
        p++;
    }
}

static Token *new_backtick_synthetic_token(VirtualMachine *vm, TokenKind kind,
                                           char *text, Token *origin) {
    Token *tok = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    memset(tok, 0, sizeof(Token));
    tok->kind     = kind;
    tok->loc      = text;
    tok->len      = (int)strlen(text);
    tok->file     = origin->file;
    tok->filename = origin->filename;
    tok->line_no  = origin->line_no;
    tok->col_no   = origin->col_no;
    tok->origin   = origin;
    return tok;
}

static Token *copy_backtick_expr_token(VirtualMachine *vm, Token *src) {
    Token *tok = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    *tok       = *src;
    tok->next  = NULL;
    return tok;
}

static bool backtick_fragment_has_splice(Token *fragment) {
    return fragment->next && equal(fragment->next, "$") &&
           fragment->next->next && equal(fragment->next->next, "{");
}

static Token *backtick_splice_end(VirtualMachine *vm, Token *fragment,
                                  Token **begin) {
    *begin     = fragment->next->next->next;
    Token *end = *begin;
    while (
        end && end->kind != TK_EOF &&
        !(equal(end, "}") && end->next && end->next->kind == TK_BACKTICK_STR))
        end = end->next;

    if (!end || end->kind == TK_EOF)
        error_tok(vm, fragment->next,
                  "unterminated backtick interpolation; expected '}'");
    if (*begin == end)
        error_tok(vm, fragment->next,
                  "empty backtick interpolation is not allowed");
    return end;
}

// Lower `fragment ${expr} fragment` to the parser-visible equivalent of
// __builtin_quote("fragment $1 fragment", expr).
// Interpolation tokens have already passed through the preprocessor, so this
// preserves macro expansion inside ${...}.
static Node *backtick_quasi_quote(VirtualMachine *vm, Token **rest,
                                  Token *tok) {
    if (!vm->compiler.in_macro_mode)
        error_tok(vm, tok,
                  "backtick quasi-quotes are only valid in comptime functions");

    int              fragment_count = 0;
    int              splice_count   = 0;
    size_t           template_len   = 0;
    BacktickLexState lex_state      = BT_LEX_NORMAL;
    Token           *fragment       = tok;

    // First pass: validate the stream and determine exact storage/template
    // sizes. Quote's FFI path supports overflow arguments on the VM stack, so
    // the parser does not impose an argument-register-derived splice limit.
    for (;;) {
        fragment_count++;
        template_len += strlen(fragment->str);
        validate_backtick_fragment(vm, fragment, &lex_state);

        if (!backtick_fragment_has_splice(fragment))
            break;

        Token *begin;
        Token *end = backtick_splice_end(vm, fragment, &begin);
        splice_count++;
        template_len += (size_t)snprintf(NULL, 0, " $%d ", splice_count);
        fragment      = end->next;
    }

    Token **fragments  = arena_alloc(&vm->compiler.parser_arena,
                                     sizeof(*fragments) * fragment_count);
    Token **expr_begin = NULL;
    Token **expr_end   = NULL;
    if (splice_count > 0) {
        expr_begin = arena_alloc(&vm->compiler.parser_arena,
                                 sizeof(*expr_begin) * splice_count);
        expr_end   = arena_alloc(&vm->compiler.parser_arena,
                                 sizeof(*expr_end) * splice_count);
    }

    // Second pass: retain fragment and interpolation ranges for lowering.
    fragment = tok;
    for (int i = 0; i < fragment_count; i++) {
        fragments[i] = fragment;
        if (i < splice_count) {
            Token *begin;
            Token *end    = backtick_splice_end(vm, fragment, &begin);
            expr_begin[i] = begin;
            expr_end[i]   = end;
            fragment      = end->next;
        }
    }

    char *template = arena_alloc(&vm->compiler.parser_arena, template_len + 1);
    char *out      = template;
    for (int i = 0; i < fragment_count; i++) {
        size_t len = strlen(fragments[i]->str);
        memcpy(out, fragments[i]->str, len);
        out += len;
        if (i < splice_count)
            out += sprintf(out, " $%d ", i + 1);
    }
    *out        = '\0';

    Token  head = {};
    Token *cur  = &head;
#define APPEND_BT_TOKEN(kind, text)                                            \
    (cur = cur->next = new_backtick_synthetic_token(vm, kind, text, tok))
    APPEND_BT_TOKEN(TK_IDENT, "__builtin_quote");
    APPEND_BT_TOKEN(TK_PUNCT, "(");

    Token *template_tok =
        new_backtick_synthetic_token(vm, TK_STR, tok->loc, tok);
    template_tok->len = tok->len;
    template_tok->str = template;
    Type *elem        = copy_type(vm, ty_char);
    elem->is_const    = true;
    template_tok->ty  = array_of(vm, elem, (int)template_len + 1);
    cur = cur->next = template_tok;

    for (int i = 0; i < splice_count; i++) {
        APPEND_BT_TOKEN(TK_PUNCT, ",");
        for (Token *src = expr_begin[i]; src != expr_end[i]; src = src->next)
            cur = cur->next = copy_backtick_expr_token(vm, src);
    }
    APPEND_BT_TOKEN(TK_PUNCT, ")");
#undef APPEND_BT_TOKEN

    cur->next = fragments[fragment_count - 1]->next;
    return postfix(vm, rest, head.next);
}

static Node *primary(VirtualMachine *vm, Token **rest, Token *tok) {
    Token *start = tok;

    if (tok->kind == TK_BACKTICK_STR)
        return backtick_quasi_quote(vm, rest, tok);

    // C23 true/false/nullptr - only when actually classified as keywords
    // (pre-C23 these are downgraded to TK_IDENT and may be used as
    // ordinary identifiers).
    if (tok->kind == TK_KEYWORD && equal(tok, "true")) {
        *rest      = tok->next;
        Node *node = new_num(vm, 1, start);
        node->ty   = ty_bool;
        return node;
    }

    if (tok->kind == TK_KEYWORD && equal(tok, "false")) {
        *rest      = tok->next;
        Node *node = new_num(vm, 0, start);
        node->ty   = ty_bool;
        return node;
    }

    if (tok->kind == TK_KEYWORD && equal(tok, "nullptr")) {
        *rest      = tok->next;
        Node *node = new_num(vm, 0, start);
        node->ty   = ty_nullptr_t;
        return node;
    }

    if (equal(tok, "(") && equal(tok->next, "{")) {
        // This is a GNU statement expresssion.
        Node *node = new_node(vm, ND_STMT_EXPR, tok);
        node->body = compound_stmt(vm, &tok, tok->next->next, NULL)->body;
        *rest      = skip(vm, tok, ")");
        return node;
    }

    if (equal(tok, "(")) {
        Node *node = expr(vm, &tok, tok->next);
        *rest      = skip(vm, tok, ")");
        return node;
    }

    if (equal(tok, "sizeof") && equal(tok->next, "(") &&
        is_typename(vm, tok->next->next)) {
        Type *ty = typename(vm, &tok, tok->next->next);
        *rest    = skip(vm, tok, ")");

        if (ty->kind == TY_VLA) {
            if (ty->vla_size)
                return new_var_node(vm, ty->vla_size, tok);

            Node *lhs = compute_vla_size(vm, ty, tok);
            Node *rhs = new_var_node(vm, ty->vla_size, tok);
            return new_binary(vm, ND_COMMA, lhs, rhs, tok);
        }

        Node *sn               = new_ulong(vm, ty->size, start);
        sn->is_sizeof_ptr_expr = (ty->kind == TY_PTR);
        sn->layout_ty          = ty; // #1031
        return sn;
    }

    if (equal(tok, "sizeof")) {
        Node *node = unary(vm, rest, tok->next);
        add_type(vm, node);
        if (node->ty->kind == TY_VLA)
            return new_var_node(vm, node->ty->vla_size, tok);
        Node *sn               = new_ulong(vm, node->ty->size, tok);
        sn->is_sizeof_ptr_expr = (node->ty->kind == TY_PTR);
        sn->layout_ty          = node->ty; // #1031
        return sn;
    }

    if (equal(tok, "_Alignof") && equal(tok->next, "(") &&
        is_typename(vm, tok->next->next)) {
        Type *ty            = typename(vm, &tok, tok->next->next);
        *rest               = skip(vm, tok, ")");
        Node *sn            = new_ulong(vm, ty->align, tok);
        sn->layout_ty       = ty; // #1031
        sn->layout_is_align = true;
        return sn;
    }

    if (equal(tok, "_Alignof")) {
        Node *node = unary(vm, rest, tok->next);
        add_type(vm, node);
        Node *sn            = new_ulong(vm, node->ty->align, tok);
        sn->layout_ty       = node->ty; // #1031
        sn->layout_is_align = true;
        return sn;
    }

    if (equal(tok, "_Generic")) {
        if (vm->compiler.c_std < CCCC_STD_C11)
            warn_tok(vm, tok, CCCC_WARN_PEDANTIC,
                     "'_Generic' is a C11 extension");
        return generic_selection(vm, rest, tok->next);
    }

    if (equal(tok, "__builtin_types_compatible_p")) {
        tok      = skip(vm, tok->next, "(");
        Type *t1 = typename(vm, &tok, tok);
        tok      = skip(vm, tok, ",");
        Type *t2 = typename(vm, &tok, tok);
        *rest    = skip(vm, tok, ")");
        return new_num(vm, is_compatible(t1, t2), start);
    }

    // __builtin_classify_type(expr) (GCC extension): returns a small integer
    // classifying expr's type. Only the operand's *type* is used -- exactly
    // like sizeof(expr) above, the expression is parsed to recover its type
    // and then discarded, so it is never emitted/evaluated (side effects in
    // expr, e.g. `x++`, do not occur). The codes below follow gcc's
    // typeclass.h where a matching CCCC type exists; TY_VECTOR has no gcc
    // counterpart so it gets a CCCC-specific code (used by <stdarg.h>'s
    // va_arg to detect a by-pointer variadic vector argument, ticket #721).
    if (equal(tok, "__builtin_classify_type")) {
        tok           = skip(vm, tok->next, "(");
        Node *operand = assign(vm, &tok, tok);
        *rest         = skip(vm, tok, ")");
        add_type(vm, operand);
        return new_num(vm, classify_type_code(operand->ty), start);
    }

    // __builtin_choose_expr(const-expr, expr1, expr2)
    //   Selects expr1 if const-expr is non-zero, else expr2, at compile time.
    //   The result carries the *type* of the chosen arm (unlike "?:", which
    //   fuses both arms via the usual arithmetic conversions).  The unchosen
    //   arm is parsed but discarded, so it is never type-checked against the
    //   chosen one nor emitted.  This is what <stdarg.h>'s va_arg relies on to
    //   give the correct type for the requested argument.
    if (equal(tok, "__builtin_choose_expr")) {
        tok          = skip(vm, tok->next, "(");
        int64_t cond = const_expr(vm, &tok, tok);
        tok          = skip(vm, tok, ",");
        Node *e1     = assign(vm, &tok, tok);
        tok          = skip(vm, tok, ",");
        Node *e2     = assign(vm, &tok, tok);
        *rest        = skip(vm, tok, ")");
        return cond ? e1 : e2;
    }

    // __cccc_va_start(ap, last, impl) / __cccc_va_arg(ap, type, impl) /
    // __cccc_va_copy(dest, src, impl) / __cccc_va_end(ap, impl) (ticket
    // #1018): internal-only builtins <stdarg.h>'s va_start/va_arg/va_copy/
    // va_end macros desugar to, alongside their existing VM-ABI expansion
    // (now passed as the trailing `impl` argument, unchanged from before
    // this ticket -- still the sole thing VM codegen/comptime/reflection
    // ever walk). `ap`/`last`/`type`/`src` are parsed a second,
    // independent time here purely so the returned `impl` node can be
    // stamped with enough annotation (Node.va_form et al, src/cccc.h) for
    // the serializer (src/serialize_stmt.c/src/serialize_expr.c) to
    // print the real host <stdarg.h> form instead of walking `impl`'s
    // VM-internal subtree under -c=native. Never reached directly from
    // user source -- only from these four macros' own expansion -- so no
    // diagnostic-quality error text is needed for malformed input.
    if (equal(tok, "__cccc_va_start")) {
        tok        = skip(vm, tok->next, "(");
        Node *ap   = assign(vm, &tok, tok);
        tok        = skip(vm, tok, ",");
        Node *last = assign(vm, &tok, tok);
        tok        = skip(vm, tok, ",");
        Node *impl = assign(vm, &tok, tok);
        *rest      = skip(vm, tok, ")");
        add_type(vm, ap);
        add_type(vm, last);
        impl->va_form = VA_START;
        impl->va_ap   = ap;
        impl->va_last = last;
        return impl;
    }

    if (equal(tok, "__cccc_va_arg")) {
        tok        = skip(vm, tok->next, "(");
        Node *ap   = assign(vm, &tok, tok);
        tok        = skip(vm, tok, ",");
        Type *type = typename(vm, &tok, tok);
        tok        = skip(vm, tok, ",");
        Node *impl = assign(vm, &tok, tok);
        *rest      = skip(vm, tok, ")");
        add_type(vm, ap);
        impl->va_form = VA_ARG;
        impl->va_ap   = ap;
        impl->va_type = type;
        return impl;
    }

    if (equal(tok, "__cccc_va_copy")) {
        tok        = skip(vm, tok->next, "(");
        Node *dest = assign(vm, &tok, tok);
        tok        = skip(vm, tok, ",");
        Node *src  = assign(vm, &tok, tok);
        tok        = skip(vm, tok, ",");
        Node *impl = assign(vm, &tok, tok);
        *rest      = skip(vm, tok, ")");
        add_type(vm, dest);
        add_type(vm, src);
        impl->va_form = VA_COPY;
        impl->va_ap   = dest;
        impl->va_src  = src;
        return impl;
    }

    if (equal(tok, "__cccc_va_end")) {
        tok        = skip(vm, tok->next, "(");
        Node *ap   = assign(vm, &tok, tok);
        tok        = skip(vm, tok, ",");
        Node *impl = assign(vm, &tok, tok);
        *rest      = skip(vm, tok, ")");
        add_type(vm, ap);
        impl->va_form = VA_END;
        impl->va_ap   = ap;
        return impl;
    }

    if (equal(tok, "__builtin_reg_class")) {
        tok      = skip(vm, tok->next, "(");
        Type *ty = typename(vm, &tok, tok);
        *rest    = skip(vm, tok, ")");

        if (is_integer(ty) || ty->kind == TY_PTR)
            return new_num(vm, 0, start);
        if (is_flonum(ty))
            return new_num(vm, 1, start);
        return new_num(vm, 2, start);
    }

    // __builtin_convertvector(expr, type) (tracker #715): cross-lane-family
    // element conversion between two vectors with the SAME lane count (e.g.
    // int32 lanes <-> float32 lanes) -- unlike a `(vTYPE)expr` cast, which
    // bit-reinterprets rather than converts. Because the VM substrate is a
    // fixed 16-byte vector register, matching lane counts also forces
    // matching element byte sizes, so only int32<->float32 and
    // int64<->float64 pairs are representable; same-domain conversions
    // (e.g. changing signedness or width without crossing int/float) have
    // no opcode yet and are rejected with a diagnostic.
    if (equal(tok, "__builtin_convertvector")) {
        tok       = skip(vm, tok->next, "(");
        Node *src = assign(vm, &tok, tok);
        add_type(vm, src);
        tok          = skip(vm, tok, ",");
        Type *dst_ty = typename(vm, &tok, tok);
        *rest        = skip(vm, tok, ")");

        if (!is_vector(src->ty))
            error_tok(vm, start,
                      "__builtin_convertvector: first argument must be a "
                      "vector type");
        if (!is_vector(dst_ty))
            error_tok(
                vm, start,
                "__builtin_convertvector: target type must be a vector type");
        if (src->ty->vec_len != dst_ty->vec_len)
            error_tok(vm, start,
                      "__builtin_convertvector: source and target vectors "
                      "must have the same number of lanes");
        bool src_f = is_flonum(src->ty->base), dst_f = is_flonum(dst_ty->base);
        if (src_f == dst_f || src->ty->base->size != dst_ty->base->size)
            error_tok(vm, start,
                      "__builtin_convertvector: unsupported lane conversion "
                      "(only int32<->float32 and int64<->float64 lane pairs "
                      "are currently supported)");

        Node *node = new_node(vm, ND_CONVERTVECTOR, start);
        node->lhs  = src;
        node->ty   = copy_type(vm, dst_ty);
        return node;
    }

    // __builtin_decimal_to_chars(buf, n, decimal_val) (#402): phase-1
    // decimal formatting entry point (printf/scanf %Hf/%Df/%DDf integration
    // is deferred to the follow-up ticket). Returns the number of bytes
    // that would have been written, snprintf-style.
    if (equal(tok, "__builtin_decimal_to_chars")) {
        tok       = skip(vm, tok->next, "(");
        Node *buf = assign(vm, &tok, tok);
        tok       = skip(vm, tok, ",");
        Node *n   = assign(vm, &tok, tok);
        tok       = skip(vm, tok, ",");
        Node *val = assign(vm, &tok, tok);
        *rest     = skip(vm, tok, ")");

        // add_type() short-circuits as soon as it sees node->ty already set
        // (see its guard: "node->ty && node->kind != ND_COMPLEX"), and this
        // node's ty is preset to ty_int below -- so the later whole-tree
        // add_type walk will never descend into lhs/rhs/cond through THIS
        // node. Each child must be add_type'd explicitly here first
        // (mirrors __builtin_convertvector's add_type(vm, src) above).
        add_type(vm, buf);
        add_type(vm, n);
        add_type(vm, val);
        if (!is_decimal(val->ty))
            error_tok(vm, start,
                      "__builtin_decimal_to_chars: third argument must have a "
                      "_Decimal32/64/128 type");

        Node *node = new_node(vm, ND_DECIMAL_TO_CHARS, start);
        node->lhs  = buf;
        node->rhs  = n;
        node->cond = val;
        node->ty   = ty_int;
        return node;
    }

    // __builtin_shuffle(vec, {i0,...,iN-1}) / __builtin_shuffle(vec1, vec2,
    // {i0,...,iN-1}) (tracker #715): GCC vector permute. The mask may be
    // either a COMPILE-TIME-CONSTANT brace-enclosed index list (matching
    // clang's constant-index __builtin_shufflevector) or, since tracker
    // #723, a RUNTIME (or named) integer vector value -- GCC's general
    // vector-typed mask form. Neither form needs a new opcode: both lower
    // to per-lane scalar reads/writes through the same vector-subscript
    // lvalue machinery the `[` postfix parse site uses (see
    // vector_lane_ref() above; verified a *runtime* index lowers correctly
    // through this same path before choosing this design), reusing the
    // brace-init-verified hidden-local pattern from compound literals.
    //
    // Constant mask: each index is range-checked at compile time and the
    // per-lane copy is fully unrolled with literal indices (no wrap needed
    // -- out of range is a hard compile error, since it's statically
    // checkable). Runtime mask: since the actual index isn't known until
    // runtime, an out-of-range value WRAPS via `% lane_count` (1-vector
    // form) or `% (2*lane_count)` (2-vector form), matching GCC's
    // documented __builtin_shuffle semantics -- this intentionally diverges
    // from the constant form's hard error, since a runtime value can't be
    // rejected at compile time.
    if (equal(tok, "__builtin_shuffle")) {
        tok      = skip(vm, tok->next, "(");
        Node *v1 = assign(vm, &tok, tok);
        add_type(vm, v1);
        if (!is_vector(v1->ty))
            error_tok(
                vm, start,
                "__builtin_shuffle: first argument must be a vector type");
        tok = skip(vm, tok, ",");

        // Disambiguating the arg count: __builtin_shuffle(v1, mask) (1-vector
        // runtime form) and __builtin_shuffle(v1, v2, mask) (2-vector form)
        // both have a non-brace second argument, so a single token of
        // lookahead ("{" vs not) can no longer tell them apart the way it
        // could when the mask was always brace-enclosed (tracker #715). We
        // parse the second argument eagerly and check what follows it: a
        // "," means it was v2 and a mask still follows; anything else means
        // it was itself the (1-vector, runtime) mask.
        Node *v2            = NULL;
        Node *mask          = NULL;
        bool  mask_is_const = equal(tok, "{");

        if (!mask_is_const) {
            Node *second = assign(vm, &tok, tok);
            add_type(vm, second);
            if (equal(tok, ",")) {
                v2 = second;
                if (!is_vector(v2->ty) || !is_compatible(v1->ty, v2->ty))
                    error_tok(vm, start,
                              "__builtin_shuffle: second vector argument must "
                              "match the first vector's type");
                tok           = skip(vm, tok, ",");
                mask_is_const = equal(tok, "{");
                if (!mask_is_const) {
                    mask = assign(vm, &tok, tok);
                    add_type(vm, mask);
                }
            } else {
                mask = second;
            }
        }

        int   lane_count = v1->ty->vec_len;
        int   max_index  = v2 ? lane_count * 2 : lane_count;
        Type *elem_ty    = v1->ty->base;

        // The constant form must be a BARE brace list, `{i0,...}` -- not a
        // `(vTYPE){...}` compound literal. A `(type)`-prefixed literal is
        // itself a valid vector *expression* (the runtime-mask form below),
        // which would be ambiguous with the two-vector form's second
        // argument (both start with '('); a bare '{' cannot start any other
        // valid argument expression here, so it unambiguously marks a
        // constant mask.
        if (mask_is_const) {
            tok          = tok->next; // consume '{'
            int *indices = arena_alloc(&vm->compiler.parser_arena,
                                       sizeof(int) * (size_t)lane_count);
            for (int i = 0; i < lane_count; i++) {
                if (i > 0)
                    tok = skip(vm, tok, ",");
                int64_t v = const_expr(vm, &tok, tok);
                if (v < 0 || v >= max_index)
                    error_tok(vm, tok, "__builtin_shuffle: index out of range");
                indices[i] = (int)v;
            }
            if (equal(tok, ","))
                tok = tok->next; // optional trailing comma
            tok   = skip(vm, tok, "}");
            *rest = skip(vm, tok, ")");

            // Materialize the source vector(s) into hidden locals so any
            // side effects in v1/v2 run exactly once, then gather each
            // destination lane individually.
            Obj  *v1var = new_lvar(vm, "", 0, v1->ty);
            Node *chain = new_binary(vm, ND_ASSIGN,
                                     new_var_node(vm, v1var, start), v1, start);

            Obj  *v2var = NULL;
            if (v2) {
                v2var        = new_lvar(vm, "", 0, v2->ty);
                Node *v2init = new_binary(
                    vm, ND_ASSIGN, new_var_node(vm, v2var, start), v2, start);
                chain = new_binary(vm, ND_COMMA, chain, v2init, start);
            }

            Obj *rvar = new_lvar(vm, "", 0, v1->ty);
            for (int i = 0; i < lane_count; i++) {
                int   idx     = indices[i];
                bool  from_v2 = v2 && idx >= lane_count;
                Obj  *srcvar  = from_v2 ? v2var : v1var;
                int   srclane = from_v2 ? idx - lane_count : idx;
                Node *dst_lane =
                    vector_lane_ref(vm, new_var_node(vm, rvar, start), elem_ty,
                                    new_num(vm, i, start), start);
                Node *src_lane = vector_lane_ref(
                    vm, new_var_node(vm, srcvar, start), elem_ty,
                    new_num(vm, srclane, start), start);
                Node *assign_lane =
                    new_binary(vm, ND_ASSIGN, dst_lane, src_lane, start);
                chain = new_binary(vm, ND_COMMA, chain, assign_lane, start);
            }
            Node *result = new_var_node(vm, rvar, start);
            return new_binary(vm, ND_COMMA, chain, result, start);
        }

        // Runtime/named vector mask (tracker #723): an ordinary integer
        // vector expression, not a bare brace list. Already parsed above
        // (as either the 2nd or 3rd argument) while disambiguating arg count.
        *rest = skip(vm, tok, ")");

        if (!is_vector(mask->ty) || !is_integer(mask->ty->base))
            error_tok(vm, start,
                      "__builtin_shuffle: the index mask must be an integer "
                      "vector (a brace-enclosed compile-time-constant list, "
                      "or a runtime/named integer vector value)");
        if (mask->ty->vec_len != lane_count)
            error_tok(vm, start,
                      "__builtin_shuffle: the index mask must have the same "
                      "number of lanes as the vector being shuffled");
        if (mask->ty->base->size != elem_ty->size)
            error_tok(vm, start,
                      "__builtin_shuffle: the index mask's element size must "
                      "match the shuffled vector's element size");

        // Materialize v1/v2/mask into hidden locals so side effects run
        // exactly once, then gather each destination lane via a runtime
        // index read out of the mask vector, wrapped into range with `%`.
        Obj  *v1var = new_lvar(vm, "", 0, v1->ty);
        Node *chain = new_binary(vm, ND_ASSIGN, new_var_node(vm, v1var, start),
                                 v1, start);

        Obj  *v2var = NULL;
        if (v2) {
            v2var        = new_lvar(vm, "", 0, v2->ty);
            Node *v2init = new_binary(
                vm, ND_ASSIGN, new_var_node(vm, v2var, start), v2, start);
            chain = new_binary(vm, ND_COMMA, chain, v2init, start);
        }

        Obj  *maskvar  = new_lvar(vm, "", 0, mask->ty);
        Node *maskinit = new_binary(
            vm, ND_ASSIGN, new_var_node(vm, maskvar, start), mask, start);
        chain              = new_binary(vm, ND_COMMA, chain, maskinit, start);

        Type *mask_elem_ty = mask->ty->base;
        Obj  *rvar         = new_lvar(vm, "", 0, v1->ty);
        for (int i = 0; i < lane_count; i++) {
            // raw_idx = maskvar[i]  (mask's own element type)
            Node *raw_idx =
                vector_lane_ref(vm, new_var_node(vm, maskvar, start),
                                mask_elem_ty, new_num(vm, i, start), start);

            Node *dst_lane =
                vector_lane_ref(vm, new_var_node(vm, rvar, start), elem_ty,
                                new_num(vm, i, start), start);
            Node *src_lane;
            if (!v2) {
                // idx = raw_idx % lane_count; result = v1var[idx]
                Node *idx = new_binary(vm, ND_MOD, raw_idx,
                                       new_num(vm, lane_count, start), start);
                src_lane  = vector_lane_ref(vm, new_var_node(vm, v1var, start),
                                            elem_ty, idx, start);
            } else {
                // idx = raw_idx % (2*lane_count); result = idx < lane_count
                //     ? v1var[idx] : v2var[idx - lane_count]
                // Bind idx to a hidden scalar local so it's evaluated once.
                Obj  *idxvar  = new_lvar(vm, "", 0, ty_int);
                Node *idx_mod = new_binary(
                    vm, ND_MOD, raw_idx, new_num(vm, max_index, start), start);
                Node *idx_init =
                    new_binary(vm, ND_ASSIGN, new_var_node(vm, idxvar, start),
                               idx_mod, start);

                Node *cmp =
                    new_binary(vm, ND_LT, new_var_node(vm, idxvar, start),
                               new_num(vm, lane_count, start), start);
                Node *then_lane =
                    vector_lane_ref(vm, new_var_node(vm, v1var, start), elem_ty,
                                    new_var_node(vm, idxvar, start), start);
                Node *els_idx =
                    new_binary(vm, ND_SUB, new_var_node(vm, idxvar, start),
                               new_num(vm, lane_count, start), start);
                Node *els_lane =
                    vector_lane_ref(vm, new_var_node(vm, v2var, start), elem_ty,
                                    els_idx, start);
                Node *cond = new_node(vm, ND_COND, start);
                cond->cond = cmp;
                cond->then = then_lane;
                cond->els  = els_lane;
                src_lane   = new_binary(vm, ND_COMMA, idx_init, cond, start);
            }
            Node *assign_lane =
                new_binary(vm, ND_ASSIGN, dst_lane, src_lane, start);
            chain = new_binary(vm, ND_COMMA, chain, assign_lane, start);
        }
        Node *result = new_var_node(vm, rvar, start);
        return new_binary(vm, ND_COMMA, chain, result, start);
    }

    if (equal(tok, "__builtin_compare_and_swap")) {
        Node *node     = new_node(vm, ND_CAS, tok);
        tok            = skip(vm, tok->next, "(");
        node->cas_addr = assign(vm, &tok, tok);
        tok            = skip(vm, tok, ",");
        node->cas_old  = assign(vm, &tok, tok);
        tok            = skip(vm, tok, ",");
        node->cas_new  = assign(vm, &tok, tok);
        *rest          = skip(vm, tok, ")");
        return node;
    }

    if (equal(tok, "__builtin_atomic_exchange")) {
        Node *node = new_node(vm, ND_EXCH, tok);
        tok        = skip(vm, tok->next, "(");
        node->lhs  = assign(vm, &tok, tok);
        tok        = skip(vm, tok, ",");
        node->rhs  = assign(vm, &tok, tok);
        *rest      = skip(vm, tok, ")");
        return node;
    }

    // __builtin_atomic_load(addr) — atomic tagged load; emits ALDR opcode
    if (equal(tok, "__builtin_atomic_load")) {
        Node *node = new_node(vm, ND_ALOAD, tok);
        tok        = skip(vm, tok->next, "(");
        node->lhs  = assign(vm, &tok, tok);
        *rest      = skip(vm, tok, ")");
        return node;
    }

    // __builtin_atomic_store(addr, val) — atomic tagged store; emits ASTR
    // opcode
    if (equal(tok, "__builtin_atomic_store")) {
        Node *node = new_node(vm, ND_ASTORE, tok);
        tok        = skip(vm, tok->next, "(");
        node->lhs  = assign(vm, &tok, tok);
        tok        = skip(vm, tok, ",");
        node->rhs  = assign(vm, &tok, tok);
        *rest      = skip(vm, tok, ")");
        return node;
    }

    // __builtin_frame_address(0) - returns the current frame's base pointer
    if (equal(tok, "__builtin_frame_address")) {
        tok = skip(vm, tok->next, "(");
        // Only level 0 is supported (current frame)
        long long level = const_expr(vm, &tok, tok);
        if (level != 0)
            error_tok(vm, tok, "__builtin_frame_address only supports level 0");
        *rest      = skip(vm, tok, ")");
        Node *node = new_node(vm, ND_FRAME_ADDR, start);
        node->ty   = pointer_to(vm, ty_void);
        return node;
    }

    // __builtin_return_address(n) - returns the return address of the nth
    // caller frame. The returned value is a VM bytecode offset (Pc, uint32_t)
    // cast to void*, NOT a host machine address. This differs from
    // __builtin_frame_address which returns bp as a real host pointer. Returns
    // NULL past the outermost frame. Lowered to the RETADDR opcode which walks
    // the saved-bp chain at runtime and bounds-checks each step against the
    // live stack region.
    if (equal(tok, "__builtin_return_address")) {
        tok             = skip(vm, tok->next, "(");
        long long level = const_expr(vm, &tok, tok);
        *rest           = skip(vm, tok, ")");
        Node *node      = new_node(vm, ND_RETURN_ADDR, start);
        node->val       = level;
        node->ty        = pointer_to(vm, ty_void);
        return node;
    }

    // __builtin_pc_function_name(pc) — map a VM bytecode pc (void*) to the
    // name of the enclosing C function.  Composes with
    // __builtin_return_address:
    //   const char *fn =
    //   __builtin_pc_function_name(__builtin_return_address(0));
    // Returns NULL if the pc is NULL or falls outside all known function
    // ranges. Works in all builds; does NOT require -g. Lowered to a CALLF to
    // the __cccc_pc_to_name FFI shim registered by cc_load_symbolize_runtime.
    if (equal(tok, "__builtin_pc_function_name")) {
        tok        = skip(vm, tok->next, "(");
        Node *arg  = assign(vm, &tok, tok);
        *rest      = skip(vm, tok, ")");
        Node *node = new_unary(
            vm, ND_FUNCALL,
            new_var_node(vm, vm->compiler.builtin_pc_to_name, arg->tok),
            arg->tok);
        node->func_ty = vm->compiler.builtin_pc_to_name->ty;
        node->ty      = vm->compiler.builtin_pc_to_name->ty->return_ty;
        node->args    = arg;
        add_type(vm, arg);
        return node;
    }

    // __builtin_pc_source_location(pc, &file, &line) — map a VM bytecode pc
    // (void*) to a source file name and line number.  Returns 1 on success,
    // 0 if the source map is unavailable (requires -g) or the pc is unknown.
    // On success, *file and *line are set; on failure both are zeroed.
    // Composes with __builtin_return_address:
    //   const char *file; int line;
    //   __builtin_pc_source_location(__builtin_return_address(0), &file,
    //   &line);
    // Lowered to a CALLF to the __cccc_pc_to_source FFI shim.
    if (equal(tok, "__builtin_pc_source_location")) {
        tok            = skip(vm, tok->next, "(");
        Node *pc_arg   = assign(vm, &tok, tok);
        tok            = skip(vm, tok, ",");
        Node *file_arg = assign(vm, &tok, tok);
        tok            = skip(vm, tok, ",");
        Node *line_arg = assign(vm, &tok, tok);
        *rest          = skip(vm, tok, ")");
        Node *node     = new_unary(
            vm, ND_FUNCALL,
            new_var_node(vm, vm->compiler.builtin_pc_to_source, pc_arg->tok),
            pc_arg->tok);
        node->func_ty  = vm->compiler.builtin_pc_to_source->ty;
        node->ty       = vm->compiler.builtin_pc_to_source->ty->return_ty;
        node->args     = pc_arg;
        pc_arg->next   = file_arg;
        file_arg->next = line_arg;
        add_type(vm, pc_arg);
        add_type(vm, file_arg);
        add_type(vm, line_arg);
        return node;
    }

    // __builtin_object_size(ptr, type) — compile-time object size.
    //
    // The `type` argument encodes two independent bits:
    //   bit 0 == 0: whole base object (type 0 or 2)
    //   bit 0 == 1: nearest surrounding subobject (type 1 or 3)
    //   bit 1 == 0: unknown fallback = (size_t)-1 (maximum, type 0 or 1)
    //   bit 1 == 1: unknown fallback = 0          (minimum, type 2 or 3)
    //
    // For objects of statically known size (local/global arrays, scalars,
    // struct members accessed via constant-offset chains) we compute the exact
    // remaining byte count.  For anything else — function-parameter pointers,
    // non-constant indices, heap allocations — we fall back to the conservative
    // estimate, preserving _FORTIFY_SOURCE safety.
    //
    // Ternary (cond ? a : b) pointers: resolve both branches independently and
    // combine with max (type 0/1) or min (type 2/3), matching GCC behavior.
    // Union member access is handled by objsize_resolve_lvalue's ND_MEMBER case
    // (offset == 0 for all union members; base_size reflects the whole union).
    //
    // The ptr argument is not evaluated (no side-effects emitted), matching
    // GCC. Runtime sizing is a separate builtin
    // (__builtin_dynamic_object_size).
    if (equal(tok, "__builtin_object_size")) {
        tok                = skip(vm, tok->next, "(");
        Node *ptr          = assign(vm, &tok, tok);
        tok                = skip(vm, tok, ",");
        long long type_arg = const_expr(vm, &tok, tok);
        *rest              = skip(vm, tok, ")");

        add_type(vm, ptr);

        // Conservative default: type 0/1 → (size_t)-1, type 2/3 → 0.
        size_t result = (type_arg & 2) ? 0 : (size_t)-1;

        // Helper: bytes remaining in `info` for this type_arg.
#define OBJSZ_REMAINING(info)                                                  \
    ({                                                                         \
        int _rem = (type_arg & 1) ? (info).sub_size - (info).sub_offset        \
                                  : (info).base_size - (info).base_offset;     \
        _rem > 0 ? (size_t)_rem : (size_t)0;                                   \
    })

        if (ptr->kind == ND_COND) {
            // Ternary: resolve each branch; combine with max (type 0/1) or
            // min (type 2/3).  If either branch is unresolvable, keep the
            // conservative default.
            ObjSizeInfo ti, ei;
            if (objsize_resolve_ptr(vm, ptr->then, &ti) &&
                objsize_resolve_ptr(vm, ptr->els, &ei)) {
                size_t sa = OBJSZ_REMAINING(ti);
                size_t sb = OBJSZ_REMAINING(ei);
                result =
                    (type_arg & 2) ? (sa < sb ? sa : sb) : (sa > sb ? sa : sb);
            }
        } else {
            ObjSizeInfo info;
            if (objsize_resolve_ptr(vm, ptr, &info))
                result = OBJSZ_REMAINING(info);
        }

#undef OBJSZ_REMAINING

        Node *node = new_node(vm, ND_NUM, start);
        node->val  = (int64_t)result;
        node->ty   = ty_ulong;

        // #642: constant malloc-family allocation tracking. A bare pointer
        // variable (through casts) whose declaration initializer was
        // recognized as `malloc(const)`/`calloc(const,const)`/etc. gets its
        // size resolved *after* the whole function is parsed, not here — a
        // later reassignment or address-of (including across a loop
        // back-edge) must be able to poison it first. See
        // resolve_objsize_queries. The node already holds the conservative
        // fallback, so if it's never upgraded (or this turns out not to be a
        // whole-function query, e.g. objsize_resolve_ptr already resolved a
        // more specific path above) behavior is unchanged.
        //
        // The query is only registered when it is asked from the *same*
        // function the pointer was declared in. A query made from inside a
        // nested function / block on an enclosing-scope pointer would
        // otherwise be resolved (and its ND_NUM frozen) the instant that
        // inner function finishes parsing — which happens *before* a later
        // reassignment in the enclosing scope is even parsed, let alone
        // poison-scanned. Restricting registration this way means such
        // cross-scope queries always keep the conservative fallback, which
        // is exactly what a same-function query would fall back to anyway
        // once reassigned.
        if (ptr->kind != ND_COND) {
            // #697/#700: peel casts *and* constant-offset ND_ADDs (interior
            // pointers, e.g. `p + 32`, written inline in the builtin's
            // argument) down to the base tracked var, accumulating the byte
            // delta. `base` may itself be a derived var (#700's `q = p +
            // const`), in which case objsize_effective_remaining follows the
            // chain at resolve time. A non-constant offset (or an
            // unresolvable/untracked base) simply skips registration,
            // leaving the conservative fallback already stored in `node`.
            // This is deliberately *not* done in objsize_resolve_ptr, which
            // runs at parse time before objsize_unsafe can be poisoned by a
            // later reassignment -- see resolve_objsize_queries.
            Obj *base;
            int  base_offset;
            if (objsize_peel_offset_chain(vm, ptr, &base, &base_offset) &&
                base->objsize_has_alloc &&
                base->objsize_decl_fn == vm->compiler.current_fn) {
                struct ObjSizeQuery *q = arena_alloc(
                    &vm->compiler.parser_arena, sizeof(struct ObjSizeQuery));
                q->node                      = node;
                q->var                       = base;
                q->offset                    = base_offset;
                q->next                      = vm->compiler.objsize_queries;
                vm->compiler.objsize_queries = q;
            }
        }

        return node;
    }

    // __builtin_dynamic_object_size(ptr, type) — runtime object-size query.
    //
    // GCC semantics mirror __builtin_object_size, with one key difference: when
    // the object's size cannot be determined at compile time we emit a DYNOBJSZ
    // opcode that looks up AllocHeader.requested_size at runtime, rather than
    // falling back unconditionally to a conservative constant.
    //
    // The `type` argument encodes the same two bits as __builtin_object_size:
    //   bit 0 == 0: whole base object (type 0 or 2)
    //   bit 0 == 1: nearest surrounding subobject (type 1 or 3)
    //   bit 1 == 0: unknown fallback = (size_t)-1 (type 0 or 1)
    //   bit 1 == 1: unknown fallback = 0           (type 2 or 3)
    //
    // Static fold: we first try objsize_resolve_ptr.  If it succeeds (the
    // pointer's backing object is statically known — stack/global/constant
    // offset chain), we emit an ND_NUM constant, identical to the compile-time
    // builtin.  This ensures correctness for all cases the static pass handles.
    //
    // Runtime path: for pointers not resolved statically (heap allocations,
    // function-parameter pointers, non-constant indices) we build an
    // ND_DYNOBJ_SIZE node that evaluates `ptr` and emits DYNOBJSZ.  For VM
    // heap allocations the opcode looks up the containing allocation via
    // vm->sorted_allocs (a base-address range query), so both base pointers
    // and interior pointers (p + k) resolve to
    // AllocHeader.requested_size - offset; for all other pointers it returns
    // the conservative fallback.
    //
    // Scope limitations (v1):
    //   - stack/VLA/alloca buffers: no AllocHeader → conservative.
    if (equal(tok, "__builtin_dynamic_object_size")) {
        tok                = skip(vm, tok->next, "(");
        Node *ptr          = assign(vm, &tok, tok);
        tok                = skip(vm, tok, ",");
        long long type_arg = const_expr(vm, &tok, tok);
        *rest              = skip(vm, tok, ")");

        add_type(vm, ptr);

        // Static fold: try to resolve at compile time (same as
        // __builtin_object_size). Ternary (ND_COND) is handled by resolving
        // both branches and combining.
#define DYNOSZ_REMAINING(info)                                                 \
    ({                                                                         \
        int _rem = (type_arg & 1) ? (info).sub_size - (info).sub_offset        \
                                  : (info).base_size - (info).base_offset;     \
        _rem > 0 ? (size_t)_rem : (size_t)0;                                   \
    })

        {
            bool   folded      = false;
            size_t fold_result = 0;
            if (ptr->kind == ND_COND) {
                ObjSizeInfo ti, ei;
                if (objsize_resolve_ptr(vm, ptr->then, &ti) &&
                    objsize_resolve_ptr(vm, ptr->els, &ei)) {
                    size_t sa   = DYNOSZ_REMAINING(ti);
                    size_t sb   = DYNOSZ_REMAINING(ei);
                    fold_result = (type_arg & 2) ? (sa < sb ? sa : sb)
                                                 : (sa > sb ? sa : sb);
                    folded      = true;
                }
            } else {
                ObjSizeInfo info;
                if (objsize_resolve_ptr(vm, ptr, &info)) {
                    fold_result = DYNOSZ_REMAINING(info);
                    folded      = true;
                }
            }
            if (folded) {
                Node *node = new_node(vm, ND_NUM, start);
                node->val  = (int64_t)fold_result;
                node->ty   = ty_ulong;
#undef DYNOSZ_REMAINING
                return node;
            }
        }
#undef DYNOSZ_REMAINING

        // Runtime path: emit DYNOBJSZ opcode that reads AllocHeader at runtime.
        Node *node = new_node(vm, ND_DYNOBJ_SIZE, start);
        node->lhs  = ptr;
        node->val  = type_arg;
        node->ty   = ty_ulong;
        return node;
    }

    // __builtin_huge_val() -> double infinity
    if (equal(tok, "__builtin_huge_val")) {
        tok        = skip(vm, tok->next, "(");
        *rest      = skip(vm, tok, ")");
        Node *node = new_node(vm, ND_NUM, start);
        node->fval = HUGE_VAL;
        node->ty   = ty_double;
        return node;
    }

    // __builtin_huge_valf() -> float infinity
    if (equal(tok, "__builtin_huge_valf")) {
        tok        = skip(vm, tok->next, "(");
        *rest      = skip(vm, tok, ")");
        Node *node = new_node(vm, ND_NUM, start);
        node->fval = (float)HUGE_VAL;
        node->ty   = ty_float;
        return node;
    }

    // __builtin_huge_vall() -> long double infinity
    if (equal(tok, "__builtin_huge_vall")) {
        tok        = skip(vm, tok->next, "(");
        *rest      = skip(vm, tok, ")");
        Node *node = new_node(vm, ND_NUM, start);
        node->fval = HUGE_VAL;
        node->ty   = ty_ldouble;
        return node;
    }

    // __builtin_infd32/64/128() -> _Decimal infinity (#402, backs
    // <float.h>'s DEC_INFINITY). Unlike __builtin_inf's binary-float family
    // below, a decimal value's ND_NUM node carries its digit text in
    // dec_digits rather than a host `double` in fval -- BID's own
    // from_string accepts "Inf"/"NaN" directly (verified), so that text is
    // exactly what a decimal literal's dec_digits would already contain.
    if (equal(tok, "__builtin_infd32") || equal(tok, "__builtin_infd64") ||
        equal(tok, "__builtin_infd128")) {
        Type *ty         = equal(tok, "__builtin_infd32")    ? ty_decimal32
                           : equal(tok, "__builtin_infd128") ? ty_decimal128
                                                             : ty_decimal64;
        tok              = skip(vm, tok->next, "(");
        *rest            = skip(vm, tok, ")");
        Node *node       = new_node(vm, ND_NUM, start);
        node->dec_digits = "Inf";
        node->ty         = ty;
        return node;
    }

    // __builtin_nand32/64/128("tag") -> _Decimal quiet NaN (#402, backs
    // <float.h>'s DEC_NAN). The tag argument is parsed and discarded, same
    // as __builtin_nan's binary-float family below.
    if (equal(tok, "__builtin_nand32") || equal(tok, "__builtin_nand64") ||
        equal(tok, "__builtin_nand128")) {
        Type *ty  = equal(tok, "__builtin_nand32")    ? ty_decimal32
                    : equal(tok, "__builtin_nand128") ? ty_decimal128
                                                      : ty_decimal64;
        tok       = skip(vm, tok->next, "(");
        Node *tag = assign(vm, &tok, tok);
        (void)tag;
        *rest            = skip(vm, tok, ")");
        Node *node       = new_node(vm, ND_NUM, start);
        node->dec_digits = "NaN";
        node->ty         = ty;
        return node;
    }

    // __builtin_inf() / __builtin_inff() / __builtin_infl() -> infinity
    if (equal(tok, "__builtin_inf") || equal(tok, "__builtin_infl") ||
        equal(tok, "__builtin_inff")) {
        Type *ty   = equal(tok, "__builtin_inff")   ? ty_float
                     : equal(tok, "__builtin_infl") ? ty_ldouble
                                                    : ty_double;
        tok        = skip(vm, tok->next, "(");
        *rest      = skip(vm, tok, ")");
        Node *node = new_node(vm, ND_NUM, start);
        node->fval = INFINITY;
        node->ty   = ty;
        return node;
    }

    // __builtin_nan("tag") / __builtin_nanf("tag") / __builtin_nanl("tag") ->
    // NaN
    if (equal(tok, "__builtin_nan") || equal(tok, "__builtin_nanf") ||
        equal(tok, "__builtin_nanl")) {
        Type *ty = equal(tok, "__builtin_nanf")   ? ty_float
                   : equal(tok, "__builtin_nanl") ? ty_ldouble
                                                  : ty_double;
        tok      = skip(vm, tok->next, "(");
        // Parse and discard the string tag argument
        Node *tag = assign(vm, &tok, tok);
        (void)tag;
        *rest      = skip(vm, tok, ")");
        Node *node = new_node(vm, ND_NUM, start);
        node->fval = NAN;
        node->ty   = ty;
        return node;
    }

    // __builtin_nans("tag") / __builtin_nansf("tag") / __builtin_nansl("tag")
    // -> signaling NaN Same shape as __builtin_nan above, but sets the IEEE-754
    // quiet bit to 0 (mantissa MSB) so the bit pattern is a signaling NaN
    // rather than a quiet one. Note: a narrowing conversion (e.g. long double
    // -> float at literal codegen) may still quiet the value per IEEE-754
    // conversion rules -- this only guarantees the *initial* bit pattern is
    // signaling.
    if (equal(tok, "__builtin_nans") || equal(tok, "__builtin_nansf") ||
        equal(tok, "__builtin_nansl")) {
        Type *ty = equal(tok, "__builtin_nansf")   ? ty_float
                   : equal(tok, "__builtin_nansl") ? ty_ldouble
                                                   : ty_double;
        tok      = skip(vm, tok->next, "(");
        // Parse and discard the string tag argument
        Node *tag = assign(vm, &tok, tok);
        (void)tag;
        *rest      = skip(vm, tok, ")");
        Node *node = new_node(vm, ND_NUM, start);
        if (ty == ty_float) {
            union {
                uint32_t u;
                float    f;
            } snan     = {.u = 0x7F800001u};
            node->fval = snan.f;
        } else {
            union {
                uint64_t u;
                double   d;
            } snan     = {.u = 0x7FF0000000000001ULL};
            node->fval = snan.d;
        }
        node->ty = ty;
        return node;
    }

    // __builtin_isnan(x) -> x != x
    if (equal(tok, "__builtin_isnan")) {
        tok       = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest     = skip(vm, tok, ")");
        // PLACEHOLDER: arg is evaluated twice; should use a temp for
        // side-effecting exprs
        Node *node = new_binary(vm, ND_NE, arg, arg, start);
        return node;
    }

    // __builtin_isinf(x) -> x == HUGE_VAL || x == -HUGE_VAL
    if (equal(tok, "__builtin_isinf")) {
        tok            = skip(vm, tok->next, "(");
        Node *arg      = assign(vm, &tok, tok);
        *rest          = skip(vm, tok, ")");
        Node *huge     = new_node(vm, ND_NUM, start);
        huge->fval     = HUGE_VAL;
        huge->ty       = ty_double;
        Node *neg_huge = new_unary(vm, ND_NEG, huge, start);
        // PLACEHOLDER: arg is evaluated twice; should use a temp for
        // side-effecting exprs
        Node *eq_pos = new_binary(vm, ND_EQ, arg, huge, start);
        Node *eq_neg = new_binary(vm, ND_EQ, arg, neg_huge, start);
        Node *node   = new_binary(vm, ND_LOGOR, eq_pos, eq_neg, start);
        return node;
    }

    // __builtin_isfinite(x) -> !(x != x || x == HUGE_VAL || x == -HUGE_VAL)
    if (equal(tok, "__builtin_isfinite")) {
        tok            = skip(vm, tok->next, "(");
        Node *arg      = assign(vm, &tok, tok);
        *rest          = skip(vm, tok, ")");
        Node *huge     = new_node(vm, ND_NUM, start);
        huge->fval     = HUGE_VAL;
        huge->ty       = ty_double;
        Node *neg_huge = new_unary(vm, ND_NEG, huge, start);
        // PLACEHOLDER: arg is evaluated multiple times; should use a temp
        Node *nan_check = new_binary(vm, ND_NE, arg, arg, start);
        Node *inf_pos   = new_binary(vm, ND_EQ, arg, huge, start);
        Node *inf_neg   = new_binary(vm, ND_EQ, arg, neg_huge, start);
        Node *inf_check = new_binary(vm, ND_LOGOR, inf_pos, inf_neg, start);
        Node *any       = new_binary(vm, ND_LOGOR, nan_check, inf_check, start);
        Node *node      = new_unary(vm, ND_NOT, any, start);
        return node;
    }

    // __builtin_signbit(x) -> x < 0
    if (equal(tok, "__builtin_signbit")) {
        tok        = skip(vm, tok->next, "(");
        Node *arg  = assign(vm, &tok, tok);
        *rest      = skip(vm, tok, ")");
        Node *zero = new_node(vm, ND_NUM, start);
        zero->fval = 0.0;
        zero->ty   = ty_double;
        Node *node = new_binary(vm, ND_LT, arg, zero, start);
        return node;
    }

    // __builtin_expect(exp, c) -> exp (branch prediction hint, ignored for now)
    if (equal(tok, "__builtin_expect")) {
        tok       = skip(vm, tok->next, "(");
        Node *exp = assign(vm, &tok, tok);
        tok       = skip(vm, tok, ",");
        Node *c   = assign(vm, &tok, tok);
        (void)c;
        *rest = skip(vm, tok, ")");
        return exp;
    }

    // __builtin_expect_with_probability(exp, c, prob) -> exp
    // Three-arg extension of __builtin_expect; probability hint is discarded.
    if (equal(tok, "__builtin_expect_with_probability")) {
        tok        = skip(vm, tok->next, "(");
        Node *exp  = assign(vm, &tok, tok);
        tok        = skip(vm, tok, ",");
        Node *c    = assign(vm, &tok, tok);
        tok        = skip(vm, tok, ",");
        Node *prob = assign(vm, &tok, tok);
        (void)c;
        (void)prob;
        *rest = skip(vm, tok, ")");
        return exp;
    }

    // __builtin_prefetch(addr, [rw], [locality]) -> (void)addr
    // Cache prefetch hint; ignored by the VM. The address operand IS evaluated
    // for side effects (matching GCC). rw and locality are compile-time
    // constant hints that are parsed and discarded.
    if (equal(tok, "__builtin_prefetch")) {
        tok        = skip(vm, tok->next, "(");
        Node *addr = assign(vm, &tok, tok);
        while (consume(vm, &tok, tok, ","))
            (void)assign(vm, &tok, tok); // discard rw / locality hints
        *rest = skip(vm, tok, ")");
        return new_cast(vm, addr, ty_void);
    }

    // __builtin_assume(expr) -> no-op (optimizer hint; expr NOT evaluated)
    // Matches Clang/GCC semantics: the assumption is for the optimizer only;
    // side effects inside expr must not be relied upon.
    if (equal(tok, "__builtin_assume")) {
        tok        = skip(vm, tok->next, "(");
        Node *expr = assign(vm, &tok, tok);
        (void)expr;
        *rest      = skip(vm, tok, ")");
        Node *node = new_node(vm, ND_NULL_EXPR, start);
        node->ty   = ty_void;
        return node;
    }

    // __builtin_constant_p(expr) -> 1 if compile-time constant, 0 otherwise
    if (equal(tok, "__builtin_constant_p")) {
        tok        = skip(vm, tok->next, "(");
        Node *expr = assign(vm, &tok, tok);
        *rest      = skip(vm, tok, ")");
        add_type(vm, expr);
        int is_const = is_const_expr(vm, expr);
        return new_num(vm, is_const ? 1 : 0, start);
    }

    // __builtin_alloca(size) -> dynamic stack allocation
    if (equal(tok, "__builtin_alloca")) {
        tok        = skip(vm, tok->next, "(");
        Node *sz   = assign(vm, &tok, tok);
        *rest      = skip(vm, tok, ")");
        Node *node = new_unary(
            vm, ND_FUNCALL,
            new_var_node(vm, vm->compiler.builtin_alloca, sz->tok), sz->tok);
        node->func_ty = vm->compiler.builtin_alloca->ty;
        node->ty      = vm->compiler.builtin_alloca->ty->return_ty;
        node->args    = sz;
        add_type(vm, sz);
        return node;
    }

    // __builtin_alloca_with_align(size, align) -> dynamic stack allocation
    // align is in bits and must be a constant; only 16-byte (128-bit) alignment
    // is guaranteed by the VM arena. Finer alignment is silently ignored.
    // PLACEHOLDER: actual alignment enforcement not implemented; see ticket for
    // follow-up if needed.
    if (equal(tok, "__builtin_alloca_with_align")) {
        tok      = skip(vm, tok->next, "(");
        Node *sz = assign(vm, &tok, tok);
        tok      = skip(vm, tok, ",");
        // alignment argument must be a compile-time constant (GCC requirement)
        (void)const_expr(vm, &tok, tok);
        *rest      = skip(vm, tok, ")");
        Node *node = new_unary(
            vm, ND_FUNCALL,
            new_var_node(vm, vm->compiler.builtin_alloca, sz->tok), sz->tok);
        node->func_ty = vm->compiler.builtin_alloca->ty;
        node->ty      = vm->compiler.builtin_alloca->ty->return_ty;
        node->args    = sz;
        add_type(vm, sz);
        return node;
    }

    // __builtin_strlen(s) -> forward to libc strlen
    if (equal(tok, "__builtin_strlen")) {
        tok        = skip(vm, tok->next, "(");
        Node *arg  = assign(vm, &tok, tok);
        *rest      = skip(vm, tok, ")");
        Node *node = new_unary(
            vm, ND_FUNCALL,
            new_var_node(vm, vm->compiler.builtin_strlen, arg->tok), arg->tok);
        node->func_ty = vm->compiler.builtin_strlen->ty;
        node->ty      = vm->compiler.builtin_strlen->ty->return_ty;
        node->args    = arg;
        add_type(vm, arg);
        return node;
    }

    // __builtin_strcmp(a, b) -> forward to libc strcmp
    if (equal(tok, "__builtin_strcmp")) {
        tok        = skip(vm, tok->next, "(");
        Node *a    = assign(vm, &tok, tok);
        tok        = skip(vm, tok, ",");
        Node *b    = assign(vm, &tok, tok);
        *rest      = skip(vm, tok, ")");
        Node *node = new_unary(
            vm, ND_FUNCALL,
            new_var_node(vm, vm->compiler.builtin_strcmp, a->tok), a->tok);
        node->func_ty = vm->compiler.builtin_strcmp->ty;
        node->ty      = vm->compiler.builtin_strcmp->ty->return_ty;
        node->args    = a;
        a->next       = b;
        add_type(vm, a);
        add_type(vm, b);
        return node;
    }

    // #1144: __builtin_memset/memcpy/memmove/memcmp -> forward to libc, same
    // pattern as __builtin_strlen/__builtin_strcmp above (a private stub Obj,
    // not in global scope, so this doesn't need -- or conflict with -- the
    // user's own <string.h> declaration on the VM path).
    if (equal(tok, "__builtin_memset") || equal(tok, "__builtin_memcpy") ||
        equal(tok, "__builtin_memmove") || equal(tok, "__builtin_memcmp")) {
        Obj *fn = equal(tok, "__builtin_memset")   ? vm->compiler.builtin_memset
                  : equal(tok, "__builtin_memcpy") ? vm->compiler.builtin_memcpy
                  : equal(tok, "__builtin_memmove")
                      ? vm->compiler.builtin_memmove
                      : vm->compiler.builtin_memcmp;
        tok     = skip(vm, tok->next, "(");
        Node *a = assign(vm, &tok, tok);
        tok     = skip(vm, tok, ",");
        Node *b = assign(vm, &tok, tok);
        tok     = skip(vm, tok, ",");
        Node *c = assign(vm, &tok, tok);
        *rest   = skip(vm, tok, ")");
        Node *node =
            new_unary(vm, ND_FUNCALL, new_var_node(vm, fn, a->tok), a->tok);
        node->func_ty = fn->ty;
        node->ty      = fn->ty->return_ty;
        node->args    = a;
        a->next       = b;
        b->next       = c;
        add_type(vm, a);
        add_type(vm, b);
        add_type(vm, c);
        return node;
    }

    // __builtin_unreachable() / __builtin_trap() / __builtin_debugtrap() ->
    // BTRAP
    if (equal(tok, "__builtin_unreachable") || equal(tok, "__builtin_trap") ||
        equal(tok, "__builtin_debugtrap")) {
        tok        = skip(vm, tok->next, "(");
        *rest      = skip(vm, tok, ")");
        Node *node = new_node(vm, ND_UNREACHABLE, start);
        node->ty   = ty_void;
        return node;
    }

    // ND_BITOP: integer bit-manipulation builtins (#212)
    // val encoding: (op_selector << 8) | bit_width
    //   op: 0=CLZ 1=CTZ 2=POPCOUNT 3=PARITY 4=FFS 5=BSWAP
    if (equal(tok, "__builtin_clz") || equal(tok, "__builtin_clzll")) {
        int width = equal(tok, "__builtin_clzll") ? 64 : 32;
        tok       = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest     = skip(vm, tok, ")");
        add_type(vm, arg);
        Node *node = new_unary(vm, ND_BITOP, arg, start);
        node->val  = (0 << 8) | width;
        node->ty   = ty_int;
        return node;
    }

    if (equal(tok, "__builtin_ctz") || equal(tok, "__builtin_ctzll")) {
        int width = equal(tok, "__builtin_ctzll") ? 64 : 32;
        tok       = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest     = skip(vm, tok, ")");
        add_type(vm, arg);
        Node *node = new_unary(vm, ND_BITOP, arg, start);
        node->val  = (1 << 8) | width;
        node->ty   = ty_int;
        return node;
    }

    if (equal(tok, "__builtin_popcount") ||
        equal(tok, "__builtin_popcountll")) {
        tok       = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest     = skip(vm, tok, ")");
        add_type(vm, arg);
        Node *node = new_unary(vm, ND_BITOP, arg, start);
        node->val  = (2 << 8) | 0;
        node->ty   = ty_int;
        return node;
    }

    if (equal(tok, "__builtin_parity") || equal(tok, "__builtin_parityll")) {
        tok       = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest     = skip(vm, tok, ")");
        add_type(vm, arg);
        Node *node = new_unary(vm, ND_BITOP, arg, start);
        node->val  = (3 << 8) | 0;
        node->ty   = ty_int;
        return node;
    }

    if (equal(tok, "__builtin_ffs") || equal(tok, "__builtin_ffsll")) {
        int width = equal(tok, "__builtin_ffsll") ? 64 : 32;
        tok       = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest     = skip(vm, tok, ")");
        add_type(vm, arg);
        Node *node = new_unary(vm, ND_BITOP, arg, start);
        node->val  = (4 << 8) | width;
        node->ty   = ty_int;
        return node;
    }

    if (equal(tok, "__builtin_bswap16") || equal(tok, "__builtin_bswap32") ||
        equal(tok, "__builtin_bswap64")) {
        int bytes = equal(tok, "__builtin_bswap16")   ? 2
                    : equal(tok, "__builtin_bswap32") ? 4
                                                      : 8;
        tok       = skip(vm, tok->next, "(");
        Node *arg = assign(vm, &tok, tok);
        *rest     = skip(vm, tok, ")");
        add_type(vm, arg);
        Node *node = new_unary(vm, ND_BITOP, arg, start);
        node->val  = (5 << 8) | bytes;
        node->ty = (bytes == 2) ? ty_ushort : (bytes == 4) ? ty_uint : ty_ulong;
        return node;
    }

    // ND_OVERFLOW_ARITH: checked arithmetic builtins (#213)
    // val: 0=add 1=sub 2=mul; lhs=a, rhs=b, cas_addr=result_ptr
    if (equal(tok, "__builtin_add_overflow") ||
        equal(tok, "__builtin_sub_overflow") ||
        equal(tok, "__builtin_mul_overflow")) {
        int op    = equal(tok, "__builtin_add_overflow")   ? 0
                    : equal(tok, "__builtin_sub_overflow") ? 1
                                                           : 2;
        tok       = skip(vm, tok->next, "(");
        Node *a   = assign(vm, &tok, tok);
        tok       = skip(vm, tok, ",");
        Node *b   = assign(vm, &tok, tok);
        tok       = skip(vm, tok, ",");
        Node *ptr = assign(vm, &tok, tok);
        *rest     = skip(vm, tok, ")");
        add_type(vm, a);
        add_type(vm, b);
        add_type(vm, ptr);
        // #964: gcc/clang require a pointer to a non-const integer here --
        // tightened from the previous bare "must be a pointer" check so a
        // mistyped third argument (e.g. `float *`, `const int *`) is
        // rejected at parse time instead of reaching the serializer and
        // producing C the host compiler refuses (the emitted form calls the
        // same builtin with the same signature, so any type the parser
        // accepts here must also be one clang/gcc accept there).
        if (ptr->ty->kind != TY_PTR || !ptr->ty->base ||
            !is_integer(ptr->ty->base) || ptr->ty->base->is_const)
            error_tok(vm, ptr->tok,
                      "__builtin_*_overflow: third argument must be a pointer "
                      "to a non-const integer type");
        Node *node     = new_node(vm, ND_OVERFLOW_ARITH, start);
        node->lhs      = a;
        node->rhs      = b;
        node->cas_addr = ptr;
        node->val      = op;
        node->ty       = ty_int;
        return node;
    }

    if (equal(tok, "__cccc_cmplx") || equal(tok, "__cccc_cmplxf") ||
        equal(tok, "__cccc_cmplxl")) {
        Type *ty   = equal(tok, "__cccc_cmplxf")   ? ty_fcomplex
                     : equal(tok, "__cccc_cmplxl") ? ty_ldcomplex
                                                   : ty_dcomplex;
        tok        = skip(vm, tok->next, "(");
        Node *real = assign(vm, &tok, tok);
        tok        = skip(vm, tok, ",");
        Node *imag = assign(vm, &tok, tok);
        *rest      = skip(vm, tok, ")");
        return new_complex_node(vm, real, imag, ty, start);
    }

    if (equal(tok, "__cccc_creal") || equal(tok, "__cccc_crealf") ||
        equal(tok, "__cccc_creall") || equal(tok, "__cccc_cimag") ||
        equal(tok, "__cccc_cimagf") || equal(tok, "__cccc_cimagl")) {
        bool  imag_part = equal(tok, "__cccc_cimag") ||
                          equal(tok, "__cccc_cimagf") ||
                          equal(tok, "__cccc_cimagl");
        Type *ret_ty =
            (equal(tok, "__cccc_crealf") || equal(tok, "__cccc_cimagf"))
                ? ty_float
            : (equal(tok, "__cccc_creall") || equal(tok, "__cccc_cimagl"))
                ? ty_ldouble
                : ty_double;
        tok        = skip(vm, tok->next, "(");
        Node *arg  = assign(vm, &tok, tok);
        *rest      = skip(vm, tok, ")");
        Node *node = new_unary(vm, ND_COMPLEX, arg, start);
        node->val  = imag_part ? 2 : 1;
        node->ty   = ret_ty;
        return node;
    }

    if (equal(tok, "__cccc_conj") || equal(tok, "__cccc_conjf") ||
        equal(tok, "__cccc_conjl")) {
        Type *ty   = equal(tok, "__cccc_conjf")   ? ty_fcomplex
                     : equal(tok, "__cccc_conjl") ? ty_ldcomplex
                                                  : ty_dcomplex;
        tok        = skip(vm, tok->next, "(");
        Node *arg  = assign(vm, &tok, tok);
        *rest      = skip(vm, tok, ")");
        Node *node = new_unary(vm, ND_COMPLEX, arg, start);
        node->val  = 3;
        node->ty   = ty;
        return node;
    }

    // Block_copy(block) - Apple Blocks extension
    // Copies the descriptor to the heap so the block can safely outlive its
    // declaring stack frame. Calls __cccc_block_copy_impl(desc) which reads
    // desc[1] (the descriptor byte-size) and returns a malloc'd copy.
    if (equal(tok, "Block_copy")) {
        tok              = skip(vm, tok->next, "(");
        Node *block_expr = assign(vm, &tok, tok);
        *rest            = skip(vm, tok, ")");
        add_type(vm, block_expr);

        // The __cccc_block_copy_impl prototype is declared as a builtin (its
        // host implementation is registered in the FFI table by the stdlib).
        Obj *copy_fn = vm->compiler.builtin_block_copy;
        if (!copy_fn) {
            // No stdlib loaded; fall back to returning the block as-is
            return block_expr;
        }

        Node *fn_node = new_var_node(vm, copy_fn, start);
        Node *call    = new_unary(vm, ND_FUNCALL, fn_node, start);
        call->func_ty = copy_fn->ty;
        call->ty      = copy_fn->ty->return_ty;
        call->args    = block_expr;
        return call;
    }

    // Block_release(block) - Apple Blocks extension
    // Frees a heap-allocated block descriptor previously obtained via
    // Block_copy. Only call on blocks returned by Block_copy; calling on a
    // stack block is UB.
    if (equal(tok, "Block_release")) {
        tok              = skip(vm, tok->next, "(");
        Node *block_expr = assign(vm, &tok, tok);
        *rest            = skip(vm, tok, ")");
        add_type(vm, block_expr);

        // Prefer a user-declared free() prototype (e.g. from <stdlib.h>);
        // fall back to the builtin_free prototype so Block_release always
        // works even when <stdlib.h> is not included (#458).
        Obj *free_fn = NULL;
        for (Obj *g = vm->compiler.globals; g; g = g->next)
            if (g->name && strcmp(g->name, "free") == 0) {
                free_fn = g;
                break;
            }
        if (!free_fn)
            free_fn = vm->compiler.builtin_free;

        if (!free_fn) {
            Node *node = new_node(vm, ND_NULL_EXPR, start);
            node->ty   = ty_void;
            return node;
        }

        Node *fn_node = new_var_node(vm, free_fn, start);
        Node *call    = new_unary(vm, ND_FUNCALL, fn_node, start);
        call->func_ty = free_fn->ty;
        call->ty      = ty_void;
        call->args    = block_expr;
        return call;
    }

    // $identifier: compile-time reflect operator.
    // $TypeName → Type *, $varname/$fnname → Obj *
    // Resolved at parse time; result is a ND_NUM constant holding the pointer.
    if (tok->kind == TK_IDENT && tok->len > 1 && tok->loc[0] == '$' &&
        (isalpha((unsigned char)tok->loc[1]) || tok->loc[1] == '_')) {
        Token fake = *tok;
        fake.loc   = tok->loc + 1;
        fake.len   = tok->len - 1;

        // Helper to get Type* or Obj* as the node's C type.
        // Look up the typedef name from reflection.h; fall back to void*.
        Token type_fake = {.kind = TK_IDENT, .loc = "Type", .len = 4};
        Token obj_fake  = {.kind = TK_IDENT, .loc = "Obj", .len = 3};

        // 1. Typedef (e.g. typedef struct Foo Foo)
        Type *found_ty = find_typedef(vm, &fake);
        // 2. Struct/union/enum tag (e.g. struct Foo)
        if (!found_ty)
            found_ty = find_tag(vm, &fake);
        if (found_ty) {
            Node *node = new_ulong(vm, (uint64_t)(uintptr_t)found_ty, tok);
            Type *meta = find_typedef(vm, &type_fake);
            node->ty   = meta ? pointer_to(vm, meta) : pointer_to(vm, ty_void);
            *rest      = tok->next;
            return node;
        }

        // 3. Variable or function (Obj)
        VarScope *vs = find_var(vm, &fake);
        if (vs && vs->var) {
            Node *node = new_ulong(vm, (uint64_t)(uintptr_t)vs->var, tok);
            Type *meta = find_typedef(vm, &obj_fake);
            node->ty   = meta ? pointer_to(vm, meta) : pointer_to(vm, ty_void);
            *rest      = tok->next;
            return node;
        }

        error_tok(vm, tok, "$%.*s: unknown name", tok->len - 1, tok->loc + 1);
    }

    if (tok->kind == TK_IDENT) {
        // Variable or enum constant
        VarScope *sc = find_var(vm, tok);

        *rest        = tok->next;

        // #887 repro 2: compile_macro_program isolates the comptime compile
        // by nulling vm->compiler.globals, but never resets vm->compiler.scope
        // -- so if this comptime program is compiled lazily, after cc_parse has
        // already registered some of the runtime TU's own globals (e.g. a
        // deferred global-initializer macro call reached via cc_expand_macros,
        // as opposed to the eager pre-parse path), find_var can still resolve
        // a real runtime Obj through the surviving scope chain. That Obj's
        // data-segment offset belongs to the runtime program, not this
        // isolated macro program, so building a var node from it produces
        // bytecode that reads/writes through a meaningless offset -- observed
        // as a host SIGSEGV at runtime, not a clean compile error. Reject any
        // non-local, non-function global that isn't actually part of this
        // macro program's own (freshly nulled-and-rebuilt) global list, and
        // let it fall through to the ordinary undefined-variable path below.
        // The $symbol reflection path (above, in this same TK_IDENT branch's
        // sibling handling) is deliberately exempt: reaching into the runtime
        // Obj table for its address is that path's entire purpose.
        if (vm->compiler.in_macro_mode && sc && sc->var && !sc->var->is_local &&
            !sc->var->is_function) {
            bool in_macro_program = false;
            for (Obj *o = vm->compiler.globals; o; o = o->next) {
                if (o == sc->var) {
                    in_macro_program = true;
                    break;
                }
            }
            if (!in_macro_program)
                sc = NULL;
        }

        // #894: on a miss during the comptime parse, ask the demand-driven
        // declaration index to splice a matching object/function/enum
        // constant in, then retry once. Deliberately placed AFTER the #887
        // guard above, not before it: find_var() can resolve a *stale*
        // runtime-TU Obj through the surviving scope chain (exactly what
        // #887 rejects), and checking "sc == NULL" before that guard runs
        // would wrongly treat that stale hit as "already resolved, don't
        // bother splicing" and skip straight to the same undefined-variable
        // error #887 was trying to avoid. A freshly spliced object -- which
        // new_gvar prepends to vm->compiler.globals, this compile's own list
        // -- needs no re-check: it is by construction part of
        // "in_macro_program".
        if (!sc &&
            (vm->compiler.in_macro_mode ||
             vm->compiler.comptime_splice_active) &&
            cc_comptime_resolve_var(vm, tok))
            sc = find_var(vm, tok);

        // For "static inline" function
        if (sc && sc->var && sc->var->is_function) {
            if (vm->compiler.current_fn)
                arena_strarray_push(vm, &vm->compiler.current_fn->refs,
                                    sc->var->name);
            else
                sc->var->is_root = true;
        }

        if (sc) {
            if (sc->var) {
                sc->var->is_used = true;
                if (sc->var->is_deprecated)
                    warn_deprecated_use(vm, tok, obj_display_name(sc->var),
                                        sc->var->deprecated_msg);
                return new_var_node(vm, sc->var, tok);
            }
            if (sc->enum_ty) {
                if (sc->is_deprecated)
                    warn_deprecated_use(vm, tok,
                                        arena_strndup(vm, tok->loc, tok->len),
                                        sc->deprecated_msg);
                Node *num = new_num(vm, sc->enum_val, tok);
                // Use the enum's own type so size/signedness are correct for
                // enums with a C23 underlying type (e.g. unsigned long).
                num->ty = sc->enum_ty;
                // #1095: propagate layout provenance from the enumerator's
                // own `= sizeof(T)` (see VarScope.enum_layout_ty's own
                // comment) so this USE, not just the enum body, flows
                // through #1031's ND_NUM re-materialization in
                // serialize_expr -- without this the body would print
                // "N = sizeof(struct statfs)" while every reference to N
                // printed the guest-folded literal.
                num->layout_ty       = sc->enum_layout_ty;
                num->layout_is_align = sc->enum_layout_is_align;
                return num;
            }
        }

        // Check if this is a macro call. When parsing macro bytecode itself,
        // keep calls as ordinary C function calls so macros can call each
        // other directly.
        if (!vm->compiler.in_macro_mode && equal(tok->next, "(")) {
            MacroFn *pm = find_macro_fn(vm, tok);
            if (pm) {
                // Create ND_MACRO_CALL node
                Token *macro_tok = tok;
                tok              = tok->next->next; // Skip identifier and '('

                // Parse arguments
                Node  head      = {};
                Node *cur       = &head;
                int   arg_count = 0;

                while (!equal(tok, ")")) {
                    if (cur != &head)
                        tok = skip(vm, tok, ",");
                    Node *arg = assign(vm, &tok, tok);
                    cur = cur->next = arg;
                    arg_count++;
                }
                *rest                 = tok->next; // Skip ')'

                Node *node            = new_node(vm, ND_MACRO_CALL, macro_tok);
                node->macro_name      = pm->name;
                node->args            = head.next;
                node->macro_arg_count = arg_count;
                node->macro_scope     = vm->compiler.scope;
                // Type will be determined after macro expansion
                node->ty =
                    ty_long; // Placeholder - macros return Node* (pointer)
                return node;
            }
        }

        if (equal(tok->next, "(")) {
            // #1144: implicit function declaration was removed from the
            // language in C99 (ISO C99 6.5.2.2p1's constraint requires a
            // visible declaration) and every real host compiler treats it as
            // an error at C99 and later -- verified against both Apple
            // clang and gcc-16, which additionally still tolerates it (as a
            // warning) at -std=c89/gnu89. CCCC used to accept it silently at
            // every standard, including its own C23 default: the call
            // compiled fine on the VM (an implicit call resolves as an FFI
            // symbol at codegen with no declaration needed at all -- see
            // find_ffi_function()/ffi_index_for_callee(), src/codegen_
            // regalloc.c), but -c=native intentionally never emits a
            // prototype for the guessed `int f(...)` signature (see the
            // obj->is_implicit skip in cc_serialize_program(), src/
            // serialize_program.c) since it could conflict with a real one
            // from a replayed header -- so the generated C referenced an
            // undeclared function and the host compiler rejected it. Under
            // -c=native the same divergence exists even at --std=c89: the
            // synthesized signature is still never emitted, so the host
            // compiler still fails, just with cccc's own message.
            if (vm->compiler.c_std >= CCCC_STD_C99 ||
                vm->compiler.native_mode) {
                char *msg = arena_format(
                    vm,
                    "call to undeclared function '%.*s'; ISO C99 and later "
                    "do not support implicit function declarations (add a "
                    "declaration, #include the header that declares it, or "
                    "compile with --std=c89)",
                    tok->len, tok->loc);
                if (vm->collect_errors &&
                    error_tok_recover(vm, tok, "%s", msg)) {
                    Node *node = new_var_node(vm, &vm->compiler.error_var, tok);
                    node->ty   = ty_error;
                    // Recovery: skip the whole call expression (balanced
                    // parens), not just the identifier, so the caller's
                    // token stream stays in sync with the source -- the
                    // args were never parsed since we bailed out before
                    // committing to a real call node.
                    Token *p     = tok->next; // the '('
                    int    depth = 0;
                    do {
                        if (equal(p, "("))
                            depth++;
                        else if (equal(p, ")"))
                            depth--;
                        p = p->next;
                    } while (p->kind != TK_EOF && depth > 0);
                    *rest = p;
                    return node;
                }
                error_tok(vm, tok, "%s", msg);
            }

            warn_tok(vm, tok, CCCC_WARN_IMPLICIT_FUNCTION_DECLARATION,
                     "implicit declaration of function '%.*s'", tok->len,
                     tok->loc);

            Obj *fn = new_implicit_function(vm, tok);
            if (vm->compiler.current_fn)
                arena_strarray_push(vm, &vm->compiler.current_fn->refs,
                                    fn->name);
            else
                fn->is_root = true;
            return new_var_node(vm, fn, tok);
        }

        // #887: an identifier that's undefined here only because it's a
        // #define from the runtime translation unit -- isolate_comptime_macros
        // strips those before the comptime preprocess/parse, so ordinary
        // preprocessing has NOT already substituted it, unlike in the rest of
        // the file. Point at the actual cause and the supported fixes rather
        // than leaving the reporter to conclude preprocessing is broken.
        // #888: #define @shared NAME is the closest fix to what a reporter in
        // this situation usually wants (opt the one macro in, in place,
        // without moving it to a header) -- list it first.
        char *msg;
        if (vm->compiler.in_macro_mode &&
            cc_is_source_define_name(vm, tok->loc, tok->len)) {
            msg = arena_format(
                vm,
                "undefined variable '%.*s' (it is a #define from the "
                "runtime translation unit; #defines are not forwarded into "
                "comptime bodies -- add @shared to its #define, route the "
                "header with @shared, pass -D, or use --comptime-include-all)",
                tok->len, tok->loc);
        } else if (vm->compiler.in_macro_mode &&
                   cc_is_dropped_comptime_global(vm, tok->loc, tok->len)) {
            // #893: an initialized global that the demand-driven declaration
            // index (src/macros.c, #894) declined to splice in -- either its
            // initializer isn't a self-contained constant, or it isn't
            // declared in the primary source file.
            msg = arena_format(
                vm,
                "undefined variable '%.*s' (it is an initialized global in "
                "the runtime translation unit; only constant-initialized "
                "globals declared directly in the main source file are "
                "forwarded to comptime bodies -- mark it [[cccc::comptime]] "
                "(see #188), or route its value through a #define @shared)",
                tok->len, tok->loc);
        } else {
            msg = arena_format(vm, "undefined variable '%.*s'", tok->len,
                               tok->loc);
        }

        // Try error recovery if enabled
        if (vm->collect_errors && error_tok_recover(vm, tok, "%s", msg)) {
            // Return error placeholder node instead of aborting
            Node *node = new_var_node(vm, &vm->compiler.error_var, tok);
            node->ty   = ty_error;
            return node;
        }

        error_tok(vm, tok, "%s", msg);
    }

    if (tok->kind == TK_STR) {
        Obj *var = new_string_literal(vm, tok->str, tok->ty);
        *rest    = tok->next;
        return new_var_node(vm, var, tok);
    }

    if (tok->kind == TK_NUM) {
        Node *node;
        if (vm->debug_vm)
            printf("  primary: TK_NUM tok->ty kind=%d, is_flonum=%d\n",
                   tok->ty ? tok->ty->kind : -1, is_flonum(tok->ty));

        if (is_flonum(tok->ty)) {
            node       = new_node(vm, ND_NUM, tok);
            node->fval = tok->fval;
            if (vm->debug_vm)
                printf("  primary: created flonum node, fval=%Lf\n",
                       node->fval);
        } else if (is_decimal(tok->ty)) {
            // _Decimal32/64/128 literal (#402): node->fval stays 0.0 (never
            // populated for these -- see tokenize.c); node->dec_digits below
            // is the sole source of truth, encoded to BID bits at codegen.
            node = new_node(vm, ND_NUM, tok);
        } else {
            node = new_num(vm, tok->val, tok);
            if (vm->debug_vm)
                printf("  primary: created int node, val=%lld\n", node->val);
        }

        node->ty          = tok->ty;
        node->wide_digits = tok->wide_digits;
        node->wide_base   = tok->wide_base;
        node->dec_digits  = tok->dec_digits;
        if (vm->debug_vm)
            printf(" primary: set node->ty to tok->ty, kind=%d\n",
                   node->ty ? node->ty->kind : -1);

        *rest = tok->next;
        return node;
    }

    // Try error recovery if enabled
    if (vm->collect_errors &&
        error_tok_recover(vm, tok, "expected an expression, found '%.*s'",
                          tok->len, tok->loc)) {
        // Skip the invalid token and return error placeholder
        *rest      = tok->next;
        Node *node = new_node(vm, ND_NUM, tok);
        node->ty   = ty_int;
        node->val  = 0;
        return node;
    }

    error_tok(vm, tok, "expected an expression, found '%.*s'", tok->len,
              tok->loc);
    return NULL;
}
