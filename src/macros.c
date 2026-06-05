/*
 JCC: JIT C Compiler

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

// Macro compilation and execution subsystem
// Compiles [[jcc::macro]] / __attribute__((macro)) functions and expands
// macro calls in the AST

#include "./internal.h"

// External declaration from reflect.c
extern JCC *__jcc_current_vm;

// Forward declarations for reflection API functions (to register as FFI)
extern JCC *__jcc_get_vm(void);
extern const char *__jcc_gensym(JCC *vm, const char *prefix);
extern Token *__jcc_ast_current_token(JCC *vm);
extern Token *__jcc_ast_synthetic_token(JCC *vm, const char *label);
extern Token *__jcc_ast_token_from_node(Node *node);
extern Node *__jcc_ast_set_token(Node *node, Token *tok);
extern Node *__jcc_ast_copy_location(Node *dst, Node *src);
extern Type *__jcc_ast_find_type(JCC *vm, const char *name);
extern bool __jcc_ast_type_exists(JCC *vm, const char *name);
extern Type *__jcc_ast_get_type(JCC *vm, const char *name);
extern TypeKind __jcc_ast_type_kind(Type *ty);
extern int __jcc_ast_type_size(Type *ty);
extern int __jcc_ast_type_align(Type *ty);
extern bool __jcc_ast_type_is_unsigned(Type *ty);
extern bool __jcc_ast_type_is_const(Type *ty);
extern Type *__jcc_ast_type_base(Type *ty);
extern int __jcc_ast_type_array_len(Type *ty);
extern Type *__jcc_ast_type_return_type(Type *ty);
extern int __jcc_ast_type_param_count(Type *ty);
extern Type *__jcc_ast_type_param_at(Type *ty, int index);
extern bool __jcc_ast_type_is_variadic(Type *ty);
extern const char *__jcc_ast_type_name(Type *ty);
extern Node *__jcc_ast_int_literal(JCC *vm, int64_t value);
extern Node *__jcc_ast_float_literal(JCC *vm, double value);
extern Node *__jcc_ast_string_literal(JCC *vm, const char *str);
extern Node *__jcc_ast_var_ref(JCC *vm, const char *name);
extern Node *__jcc_ast_binary(JCC *vm, NodeKind op, Node *left, Node *right);
extern Node *__jcc_ast_unary(JCC *vm, NodeKind op, Node *operand);
extern Node *__jcc_ast_cast(JCC *vm, Node *expr, Type *target_type);
extern Node *__jcc_ast_return(JCC *vm, Node *expr);
extern Node *__jcc_ast_if(JCC *vm, Node *cond, Node *then_body, Node *else_body);
extern Node *__jcc_ast_switch(JCC *vm, Node *cond);
extern void __jcc_ast_switch_add_case(JCC *vm, Node *switch_node, Node *value,
                                Node *body);
extern void __jcc_ast_switch_set_default(JCC *vm, Node *switch_node, Node *body);
extern int __jcc_ast_enum_count(JCC *vm, Type *enum_type);
extern EnumConstant *__jcc_ast_enum_at(JCC *vm, Type *enum_type, int index);
extern const char *__jcc_ast_enum_constant_name(EnumConstant *ec);
extern int __jcc_ast_enum_constant_value(EnumConstant *ec);
extern Type *__jcc_ast_make_pointer(JCC *vm, Type *base);
extern Type *__jcc_ast_make_array(JCC *vm, Type *base, int len);

// Function generation
extern Obj *__jcc_ast_function(JCC *vm, const char *name, Type *return_type);
extern void __jcc_ast_function_add_param(JCC *vm, Obj *fn, const char *name,
                                    Type *type);
extern void __jcc_ast_function_set_body(JCC *vm, Obj *fn, Node *body);
extern void __jcc_ast_function_set_static(Obj *fn, bool is_static);
extern void __jcc_ast_function_set_inline(Obj *fn, bool is_inline);
extern void __jcc_ast_function_set_variadic(Obj *fn, bool is_variadic);
extern Node *__jcc_ast_forward_declare(JCC *vm, Obj *fn);
extern Node *__jcc_ast_param_ref(JCC *vm, Obj *fn, const char *name);

// Ticket #152: global variable generation
extern Obj  *__jcc_ast_global_var(JCC *vm, const char *name, Type *ty);
extern void  __jcc_ast_global_var_set_init_data(JCC *vm, Obj *var,
                                                const char *data, int len);
extern void  __jcc_ast_global_var_set_static(Obj *var, bool is_static);

// Ticket #148: function-building context (push/pop current_fn for _QUOTE)
extern void __jcc_ast_push_fn(JCC *vm, Obj *fn);
extern void __jcc_ast_pop_fn(JCC *vm);

// Ticket #58: AST dump
extern void __jcc_dump_tree(JCC *vm, Node *node);
extern const char *__jcc_dump_tree_to_string(JCC *vm, Node *node);
extern void __jcc_dump_ast_gen(JCC *vm, Node *node);
extern const char *__jcc_dump_ast_gen_to_string(JCC *vm, Node *node);

// Ticket #78: source-located macro diagnostics
extern void __jcc_macro_error_at(JCC *vm, Node *node, const char *fmt, ...);
extern void __jcc_macro_warning_at(JCC *vm, Node *node, const char *fmt, ...);

// Previously unregistered statement builders (existing functions, now exposed)
extern Node *__jcc_ast_block(JCC *vm, Node **stmts, int count);
extern Node *__jcc_ast_expr_stmt(JCC *vm, Node *expr);

// Ticket #77: hygienic local variable injection
extern Node *__jcc_ast_local_var(JCC *vm, const char *name, Type *ty);
extern Node *__jcc_ast_local_var_unique(JCC *vm, Type *ty);

// Ticket #51: new expression/statement builders
extern Node *__jcc_ast_assign(JCC *vm, Node *target, Node *value);
extern Node *__jcc_ast_member(JCC *vm, Node *obj, const char *name);
extern Node *__jcc_ast_funcall(JCC *vm, Node *callee, Node **args, int n);
extern Node *__jcc_ast_while(JCC *vm, Node *cond, Node *body);
extern Node *__jcc_ast_for(JCC *vm, Node *init, Node *cond, Node *inc, Node *body);
extern Node *__jcc_ast_do_while(JCC *vm, Node *body, Node *cond);

// Ticket #1: quasi-quoting; Ticket #172: list splice helper
extern Node *__jcc_quote(JCC *vm, const char *tmpl, ...);
extern Node *__jcc_quote_n(JCC *vm, const char *tmpl, Node **nodes, int count);
extern Node *__jcc_node_list(JCC *vm, Node **nodes, int count);

// Ticket #171: new expression builders
extern Node  *__jcc_ast_cond(JCC *vm, Node *cond, Node *then_expr, Node *else_expr);
extern Node  *__jcc_ast_null(JCC *vm);
extern Node  *__jcc_ast_sizeof_type(JCC *vm, Type *ty);
extern Node  *__jcc_ast_alignof_type(JCC *vm, Type *ty);
extern Node  *__jcc_ast_sizeof_expr(JCC *vm, Node *expr);
extern Node  *__jcc_ast_subscript(JCC *vm, Node *arr, Node *idx);
extern Node  *__jcc_ast_comma(JCC *vm, Node *lhs, Node *rhs);

// Ticket #171: qualified type builders
extern Type  *__jcc_ast_make_const(JCC *vm, Type *ty);
extern Type  *__jcc_ast_make_volatile(JCC *vm, Type *ty);

// Ticket #171: function prototype builder
extern Obj   *__jcc_ast_function_prototype(JCC *vm, const char *name,
                                            Type *return_type);

// Ticket #171: struct/union/enum/typedef type builders
extern Type  *__jcc_ast_make_struct(JCC *vm, const char *name);
extern Type  *__jcc_ast_make_union(JCC *vm, const char *name);
extern Type  *__jcc_ast_struct_add_field(JCC *vm, Type *ty, const char *name,
                                          Type *field_type);
extern Type  *__jcc_ast_make_enum(JCC *vm, const char *name);
extern void   __jcc_ast_enum_add_constant(JCC *vm, Type *ty, const char *name,
                                           int value);
extern void   __jcc_ast_make_typedef(JCC *vm, const char *name, Type *underlying);

// Ticket #188: comptime variable access
extern int64_t __jcc_get_comptime_int(JCC *vm, const char *name);
extern double  __jcc_get_comptime_float(JCC *vm, const char *name);
extern Node   *__jcc_get_comptime_var(JCC *vm, const char *name);
extern Node   *__jcc_get_comptime_member(JCC *vm, const char *var_name,
                                         const char *field);

// Ticket #277: Lisp-style single-macro expansion
extern Node   *__jcc_macroexpand_1(JCC *vm, Node *node);
extern Node   *__jcc_macroexpand(JCC *vm, Node *node);

// Register reflection API functions as FFI
static void register_reflection_ffi(JCC *vm) {
    // VM accessor
    cc_register_cfunc(vm, "__jcc_get_vm", (void *)__jcc_get_vm, 0, 0);
    cc_register_cfunc(vm, "__jcc_gensym", (void *)__jcc_gensym, 2, 0);

    // Source location helpers
    cc_register_cfunc(vm, "__jcc_ast_current_token",
                      (void *)__jcc_ast_current_token, 1, 0);
    cc_register_cfunc(vm, "__jcc_ast_synthetic_token",
                      (void *)__jcc_ast_synthetic_token, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_token_from_node",
                      (void *)__jcc_ast_token_from_node, 1, 0);
    cc_register_cfunc(vm, "__jcc_ast_set_token",
                      (void *)__jcc_ast_set_token, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_copy_location",
                      (void *)__jcc_ast_copy_location, 2, 0);

    // Type lookup
    cc_register_cfunc(vm, "__jcc_ast_find_type", (void *)__jcc_ast_find_type, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_type_exists", (void *)__jcc_ast_type_exists, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_get_type", (void *)__jcc_ast_get_type, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_type_kind", (void *)__jcc_ast_type_kind, 1, 0);
    cc_register_cfunc(vm, "__jcc_ast_type_size", (void *)__jcc_ast_type_size, 1, 0);
    cc_register_cfunc(vm, "__jcc_ast_type_align", (void *)__jcc_ast_type_align, 1, 0);
    cc_register_cfunc(vm, "__jcc_ast_type_is_unsigned",
                      (void *)__jcc_ast_type_is_unsigned, 1, 0);
    cc_register_cfunc(vm, "__jcc_ast_type_is_const",
                      (void *)__jcc_ast_type_is_const, 1, 0);
    cc_register_cfunc(vm, "__jcc_ast_type_base", (void *)__jcc_ast_type_base, 1, 0);
    cc_register_cfunc(vm, "__jcc_ast_type_array_len",
                      (void *)__jcc_ast_type_array_len, 1, 0);
    cc_register_cfunc(vm, "__jcc_ast_type_return_type",
                      (void *)__jcc_ast_type_return_type, 1, 0);
    cc_register_cfunc(vm, "__jcc_ast_type_param_count",
                      (void *)__jcc_ast_type_param_count, 1, 0);
    cc_register_cfunc(vm, "__jcc_ast_type_param_at",
                      (void *)__jcc_ast_type_param_at, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_type_is_variadic",
                      (void *)__jcc_ast_type_is_variadic, 1, 0);
    cc_register_cfunc(vm, "__jcc_ast_type_name", (void *)__jcc_ast_type_name, 1, 0);

    // Type construction
    cc_register_cfunc(vm, "__jcc_ast_make_pointer", (void *)__jcc_ast_make_pointer, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_make_array", (void *)__jcc_ast_make_array, 3, 0);

    // Literal construction
    cc_register_cfunc(vm, "__jcc_ast_int_literal", (void *)__jcc_ast_int_literal, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_float_literal", (void *)__jcc_ast_float_literal, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_string_literal", (void *)__jcc_ast_string_literal, 2,
                      0);
    cc_register_cfunc(vm, "__jcc_ast_var_ref", (void *)__jcc_ast_var_ref, 2, 0);

    // Expression construction
    cc_register_cfunc(vm, "__jcc_ast_binary", (void *)__jcc_ast_binary, 4, 0);
    cc_register_cfunc(vm, "__jcc_ast_unary", (void *)__jcc_ast_unary, 3, 0);
    cc_register_cfunc(vm, "__jcc_ast_cast", (void *)__jcc_ast_cast, 3, 0);

    // Statement construction
    cc_register_cfunc(vm, "__jcc_ast_return", (void *)__jcc_ast_return, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_if", (void *)__jcc_ast_if, 4, 0);
    cc_register_cfunc(vm, "__jcc_ast_switch", (void *)__jcc_ast_switch, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_switch_add_case", (void *)__jcc_ast_switch_add_case, 4,
                      0);
    cc_register_cfunc(vm, "__jcc_ast_switch_set_default",
                      (void *)__jcc_ast_switch_set_default, 3, 0);

    // Ticket #58: AST dump
    cc_register_cfunc(vm, "__jcc_dump_tree",              (void *)__jcc_dump_tree,              2, 0);
    cc_register_cfunc(vm, "__jcc_dump_tree_to_string",    (void *)__jcc_dump_tree_to_string,    2, 0);
    cc_register_cfunc(vm, "__jcc_dump_ast_gen",           (void *)__jcc_dump_ast_gen,           2, 0);
    cc_register_cfunc(vm, "__jcc_dump_ast_gen_to_string", (void *)__jcc_dump_ast_gen_to_string, 2, 0);

    // Ticket #78: source-located macro diagnostics (variadic)
    cc_register_variadic_cfunc(vm, "__jcc_macro_error_at",   (void *)__jcc_macro_error_at,   3, 0);
    cc_register_variadic_cfunc(vm, "__jcc_macro_warning_at", (void *)__jcc_macro_warning_at, 3, 0);

    // Previously unregistered statement builders
    cc_register_cfunc(vm, "__jcc_ast_block",     (void *)__jcc_ast_block,     3, 0);
    cc_register_cfunc(vm, "__jcc_ast_expr_stmt", (void *)__jcc_ast_expr_stmt, 2, 0);

    // Ticket #77: hygienic local variable injection
    cc_register_cfunc(vm, "__jcc_ast_local_var",        (void *)__jcc_ast_local_var,        3, 0);
    cc_register_cfunc(vm, "__jcc_ast_local_var_unique",  (void *)__jcc_ast_local_var_unique,  2, 0);

    // Ticket #51: new expression/statement builders
    cc_register_cfunc(vm, "__jcc_ast_assign",   (void *)__jcc_ast_assign,   3, 0);
    cc_register_cfunc(vm, "__jcc_ast_member",   (void *)__jcc_ast_member,   3, 0);
    cc_register_cfunc(vm, "__jcc_ast_funcall",  (void *)__jcc_ast_funcall,  4, 0);
    cc_register_cfunc(vm, "__jcc_ast_while",    (void *)__jcc_ast_while,    3, 0);
    cc_register_cfunc(vm, "__jcc_ast_for",      (void *)__jcc_ast_for,      5, 0);
    cc_register_cfunc(vm, "__jcc_ast_do_while", (void *)__jcc_ast_do_while, 3, 0);

    // Enum reflection
    cc_register_cfunc(vm, "__jcc_ast_enum_count", (void *)__jcc_ast_enum_count, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_enum_at", (void *)__jcc_ast_enum_at, 3, 0);
    cc_register_cfunc(vm, "__jcc_ast_enum_constant_name",
                      (void *)__jcc_ast_enum_constant_name, 1, 0);
    cc_register_cfunc(vm, "__jcc_ast_enum_constant_value",
                      (void *)__jcc_ast_enum_constant_value, 1, 0);

    // Function generation
    cc_register_cfunc(vm, "__jcc_ast_function", (void *)__jcc_ast_function, 3, 0);
    cc_register_cfunc(vm, "__jcc_ast_function_add_param",
                      (void *)__jcc_ast_function_add_param, 4, 0);
    cc_register_cfunc(vm, "__jcc_ast_function_set_body",
                      (void *)__jcc_ast_function_set_body, 3, 0);
    cc_register_cfunc(vm, "__jcc_ast_function_set_static",
                      (void *)__jcc_ast_function_set_static, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_function_set_inline",
                      (void *)__jcc_ast_function_set_inline, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_function_set_variadic",
                      (void *)__jcc_ast_function_set_variadic, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_forward_declare",
                      (void *)__jcc_ast_forward_declare, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_param_ref", (void *)__jcc_ast_param_ref, 3, 0);

    // Ticket #152: global variable generation
    cc_register_cfunc(vm, "__jcc_ast_global_var",
                      (void *)__jcc_ast_global_var, 3, 0);
    cc_register_cfunc(vm, "__jcc_ast_global_var_set_init_data",
                      (void *)__jcc_ast_global_var_set_init_data, 4, 0);
    cc_register_cfunc(vm, "__jcc_ast_global_var_set_static",
                      (void *)__jcc_ast_global_var_set_static, 2, 0);

    // Ticket #148: function-building context
    cc_register_cfunc(vm, "__jcc_ast_push_fn",
                      (void *)__jcc_ast_push_fn, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_pop_fn",
                      (void *)__jcc_ast_pop_fn, 1, 0);

    // Ticket #1: quasi-quoting
    cc_register_variadic_cfunc(vm, "__jcc_quote",      (void *)__jcc_quote,      2, 0);
    cc_register_cfunc(vm,          "__jcc_quote_n",    (void *)__jcc_quote_n,    4, 0);

    // Ticket #172: list splice helper
    cc_register_cfunc(vm,          "__jcc_node_list",  (void *)__jcc_node_list,  3, 0);

    // Ticket #171: new expression builders
    cc_register_cfunc(vm, "__jcc_ast_cond",         (void *)__jcc_ast_cond,         4, 0);
    cc_register_cfunc(vm, "__jcc_ast_null",         (void *)__jcc_ast_null,         1, 0);
    cc_register_cfunc(vm, "__jcc_ast_sizeof_type",  (void *)__jcc_ast_sizeof_type,  2, 0);
    cc_register_cfunc(vm, "__jcc_ast_alignof_type", (void *)__jcc_ast_alignof_type, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_sizeof_expr",  (void *)__jcc_ast_sizeof_expr,  2, 0);
    cc_register_cfunc(vm, "__jcc_ast_subscript",    (void *)__jcc_ast_subscript,    3, 0);
    cc_register_cfunc(vm, "__jcc_ast_comma",        (void *)__jcc_ast_comma,        3, 0);

    // Ticket #171: qualified type builders
    cc_register_cfunc(vm, "__jcc_ast_make_const",    (void *)__jcc_ast_make_const,    2, 0);
    cc_register_cfunc(vm, "__jcc_ast_make_volatile", (void *)__jcc_ast_make_volatile, 2, 0);

    // Ticket #171: function prototype builder
    cc_register_cfunc(vm, "__jcc_ast_function_prototype",
                      (void *)__jcc_ast_function_prototype, 3, 0);

    // Ticket #171: struct/union/enum/typedef type builders
    cc_register_cfunc(vm, "__jcc_ast_make_struct",
                      (void *)__jcc_ast_make_struct, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_make_union",
                      (void *)__jcc_ast_make_union, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_struct_add_field",
                      (void *)__jcc_ast_struct_add_field, 4, 0);
    cc_register_cfunc(vm, "__jcc_ast_make_enum",
                      (void *)__jcc_ast_make_enum, 2, 0);
    cc_register_cfunc(vm, "__jcc_ast_enum_add_constant",
                      (void *)__jcc_ast_enum_add_constant, 4, 0);
    cc_register_cfunc(vm, "__jcc_ast_make_typedef",
                      (void *)__jcc_ast_make_typedef, 3, 0);

    // Ticket #188: comptime variable access
    cc_register_cfunc(vm, "__jcc_get_comptime_int",
                      (void *)__jcc_get_comptime_int, 2, 0);
    cc_register_cfunc(vm, "__jcc_get_comptime_float",
                      (void *)__jcc_get_comptime_float, 2, 1); // returns double
    cc_register_cfunc(vm, "__jcc_get_comptime_var",
                      (void *)__jcc_get_comptime_var, 2, 0);
    cc_register_cfunc(vm, "__jcc_get_comptime_member",
                      (void *)__jcc_get_comptime_member, 3, 0);

    // Ticket #277: Lisp-style macro expansion
    cc_register_cfunc(vm, "__jcc_macroexpand_1",
                      (void *)__jcc_macroexpand_1, 2, 0);
    cc_register_cfunc(vm, "__jcc_macroexpand",
                      (void *)__jcc_macroexpand, 2, 0);
}

static void init_vm_segments_for_macros(JCC *vm);

static Token *copy_macro_token(JCC *vm, Token *tok) {
    Token *copy = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    *copy = *tok;
    copy->next = NULL;
    copy->origin = tok;
    return copy;
}

static Token *new_macro_punct(JCC *vm, char *str, Token *tmpl) {
    Token *tok = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    memset(tok, 0, sizeof(Token));
    tok->kind = TK_PUNCT;
    tok->loc = str;
    tok->len = strlen(str);
    if (tmpl) {
        tok->file = tmpl->file;
        tok->filename = tmpl->filename;
        tok->line_no = tmpl->line_no;
        tok->col_no = tmpl->col_no;
    }
    return tok;
}

static Token *new_macro_eof(JCC *vm, Token *tmpl) {
    Token *tok = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    memset(tok, 0, sizeof(Token));
    tok->kind = TK_EOF;
    if (tmpl) {
        tok->loc = tmpl->loc;
        tok->len = tmpl->len;
        tok->file = tmpl->file;
        tok->filename = tmpl->filename;
        tok->line_no = tmpl->line_no;
        tok->col_no = tmpl->col_no;
    }
    return tok;
}

static Token *append_macro_prototype(JCC *vm, Token *cur, MacroFn *pm) {
    Token *last = pm->body_tokens;
    for (Token *tok = pm->body_tokens; tok && tok->kind != TK_EOF;
         tok = tok->next) {
        last = tok;
        if (equal(tok, "{"))
            break;
        cur = cur->next = copy_macro_token(vm, tok);
    }
    cur = cur->next = new_macro_punct(vm, ";", last);
    return cur;
}

static Token *append_macro_definition(JCC *vm, Token *cur, MacroFn *pm) {
    Token *last = pm->body_tokens;
    for (Token *tok = pm->body_tokens; tok && tok->kind != TK_EOF;
         tok = tok->next) {
        last = tok;
        cur = cur->next = copy_macro_token(vm, tok);
    }
    (void)last;
    return cur;
}

static Token *append_token_list(JCC *vm, Token *cur, Token *tokens) {
    for (Token *tok = tokens; tok && tok->kind != TK_EOF; tok = tok->next)
        cur = cur->next = copy_macro_token(vm, tok);
    return cur;
}

static Token *find_matching_brace(Token *tok) {
    int depth = 0;
    for (Token *t = tok; t && t->kind != TK_EOF; t = t->next) {
        if (equal(t, "{"))
            depth++;
        else if (equal(t, "}")) {
            depth--;
            if (depth == 0)
                return t;
        }
    }
    return NULL;
}

static bool starts_file_scope_call(Token *tok) {
    return tok && tok->kind == TK_IDENT && tok->next && equal(tok->next, "(");
}

// Capture file-scope declarations that can safely be prepended to the macro
// bytecode program: typedefs, tag declarations, prototypes, externs, and other
// declarations without top-level initializers. Function bodies and file-scope
// macro calls are skipped so ordinary program code is not compiled into the
// macro VM.
static Token *build_macro_context_tokens(JCC *vm, Token **input_tokens,
                                         int count) {
    Token head = {};
    Token *cur = &head;

    for (int fi = 0; fi < count; fi++) {
        Token *tok = input_tokens[fi];
        while (tok && tok->kind != TK_EOF) {
            Token *start = tok;

            if (starts_file_scope_call(tok)) {
                while (tok && tok->kind != TK_EOF && !equal(tok, ";"))
                    tok = tok->next;
                if (tok && equal(tok, ";"))
                    tok = tok->next;
                continue;
            }

            bool has_top_level_eq = false;
            bool is_function_body = false;
            int paren_depth = 0;
            int bracket_depth = 0;
            int brace_depth = 0;
            Token *prev_sig = NULL;

            while (tok && tok->kind != TK_EOF) {
                if (brace_depth == 0 && paren_depth == 0 &&
                    bracket_depth == 0) {
                    if (equal(tok, "="))
                        has_top_level_eq = true;

                    if (equal(tok, "{") && prev_sig &&
                        equal(prev_sig, ")")) {
                        is_function_body = true;
                        Token *close = find_matching_brace(tok);
                        tok = close ? close->next : tok->next;
                        break;
                    }

                    if (equal(tok, ";")) {
                        if (!has_top_level_eq) {
                            for (Token *t = start; t != tok->next &&
                                 t && t->kind != TK_EOF; t = t->next)
                                cur = cur->next = copy_macro_token(vm, t);
                        }
                        tok = tok->next;
                        break;
                    }
                }

                if (equal(tok, "("))
                    paren_depth++;
                else if (equal(tok, ")") && paren_depth > 0)
                    paren_depth--;
                else if (equal(tok, "["))
                    bracket_depth++;
                else if (equal(tok, "]") && bracket_depth > 0)
                    bracket_depth--;
                else if (equal(tok, "{"))
                    brace_depth++;
                else if (equal(tok, "}") && brace_depth > 0)
                    brace_depth--;

                if (!equal(tok, ";"))
                    prev_sig = tok;
                tok = tok->next;
            }

            if (!tok || tok->kind == TK_EOF)
                break;
            if (is_function_body)
                continue;
        }
    }

    if (!head.next)
        return NULL;
    cur->next = new_macro_eof(vm, cur);
    return head.next;
}

// Find the first top-level '=' in a token list (brace-depth 0).
// Returns the token AT '=', or NULL if none found.
static Token *find_top_level_eq(Token *tokens) {
    int depth = 0;
    for (Token *t = tokens; t && t->kind != TK_EOF; t = t->next) {
        if (equal(t, "{")) depth++;
        else if (equal(t, "}")) depth--;
        else if (depth == 0 && equal(t, "="))
            return t;
    }
    return NULL;
}

// Find the struct/union tag in a comptime var's decl_tokens.
// For `struct Dims { int w; int h; } dims = {...};` returns the 'Dims' token
// and writes the 'struct' keyword token to *kw_out.
// Returns NULL for anonymous structs or typedef'd aggregate types (no
// struct/union keyword present), which cannot have a cast synthesized.
static Token *comptime_struct_tag(Token *decl_tokens, Token **kw_out) {
    for (Token *t = decl_tokens; t && t->kind != TK_EOF; t = t->next) {
        if (equal(t, "struct") || equal(t, "union")) {
            Token *next = t->next;
            if (next && next->kind == TK_IDENT) {
                if (kw_out) *kw_out = t;
                return next;
            }
            // Anonymous struct/union: no synthesizable cast.
            return NULL;
        }
    }
    return NULL; // Typedef'd aggregate or plain scalar.
}

// True if this comptime var's initializer should be evaluated via
// __jcc_comptime_init (ticket #191/#192) rather than the constant init_data
// path. Both build_combined_macro_tokens and build_comptime_init_fn_tokens
// call this so they agree on which vars are routed through the init fn.
//
// Scalar initializers (= <non-brace-expr>): always routed.
// Tagged struct/union initializers (= { ... }): routed iff a tag can be
// extracted (so the compound-literal cast can be synthesized).
// Anonymous / typedef'd aggregates: remain on the constant path.
static bool comptime_var_uses_init_fn(ComptimeVar *cv) {
    Token *eq = find_top_level_eq(cv->decl_tokens);
    if (!eq)
        return false; // No initializer at all.
    if (!eq->next || !equal(eq->next, "{"))
        return true;  // Scalar expression init.
    // Aggregate init: routable only if we can synthesize (struct/union Tag).
    return comptime_struct_tag(cv->decl_tokens, NULL) != NULL;
}

// Inject decl_tokens up to (not including) eq_tok, then emit ';'.
// Produces a declaration without its initializer, e.g. "int buf_size ;"
static Token *append_decl_stripped(JCC *vm, Token *cur, Token *decl_tokens,
                                   Token *eq_tok) {
    for (Token *t = decl_tokens; t && t->kind != TK_EOF && t != eq_tok;
         t = t->next)
        cur = cur->next = copy_macro_token(vm, t);
    cur = cur->next = new_macro_punct(vm, ";", eq_tok);
    return cur;
}

// Build a __jcc_comptime_init function that assigns each comptime var's
// initializer expression in source order. Handles:
//   - scalar expression inits:  name = expr ;
//   - tagged struct/union inits: name = (struct Tag){ ... } ;
// Returns a token list via tokenize_string, or NULL when there are no vars
// routed through the init fn.
static Token *build_comptime_init_fn_tokens(JCC *vm,
                                             ComptimeVar **vars, int count) {
    bool has_any = false;
    for (int i = 0; i < count; i++) {
        if (comptime_var_uses_init_fn(vars[i])) {
            has_any = true;
            break;
        }
    }
    if (!has_any)
        return NULL;

    char buf[16384];
    char *p   = buf;
    char *end = buf + sizeof(buf) - 4;

    p += snprintf(p, end - p, "void __jcc_comptime_init(void){\n");

    for (int i = 0; i < count; i++) {
        ComptimeVar *cv = vars[i];
        if (!comptime_var_uses_init_fn(cv)) continue;

        Token *eq = find_top_level_eq(cv->decl_tokens);
        if (!eq) continue; // Should not happen if predicate is true, but be safe.

        if (eq->next && equal(eq->next, "{")) {
            // Aggregate init: emit  name = (struct Tag){ ... } ;
            Token *kw = NULL;
            Token *tag = comptime_struct_tag(cv->decl_tokens, &kw);
            // tag != NULL is guaranteed by comptime_var_uses_init_fn, but guard.
            if (!tag || !kw) continue;
            p += snprintf(p, end - p, "%s=(%.*s %.*s)",
                          cv->name,
                          kw->len,  kw->loc,   // "struct" or "union"
                          tag->len, tag->loc);  // tag name

            // Emit the brace-group '{ ... }' verbatim from eq->next.
            int depth = 0;
            for (Token *t = eq->next; t && t->kind != TK_EOF; t = t->next) {
                if (equal(t, "{")) depth++;
                else if (equal(t, "}")) { depth--; }
                else if (depth == 0 && equal(t, ";")) break;
                if (p + t->len + 2 >= end)
                    error("comptime init function source overflow (too many/long tokens)");
                p += snprintf(p, end - p, " %.*s", t->len, t->loc);
                if (equal(t, "}") && depth == 0) break;
            }
            p += snprintf(p, end - p, ";\n");
        } else {
            // Scalar expression init: emit  name = expr ;
            p += snprintf(p, end - p, "%s=", cv->name);

            int depth = 0;
            for (Token *t = eq->next; t && t->kind != TK_EOF; t = t->next) {
                if (equal(t, "{")) depth++;
                else if (equal(t, "}")) depth--;
                else if (depth == 0 && equal(t, ";")) break;
                if (p + t->len + 2 >= end)
                    error("comptime init function source overflow (too many/long tokens)");
                p += snprintf(p, end - p, " %.*s", t->len, t->loc);
            }
            p += snprintf(p, end - p, ";\n");
        }
    }

    p += snprintf(p, end - p, "}\n");

    Token *toks = tokenize_string(vm, "<comptime-init-fn>", buf);
    if (!toks)
        return NULL;
    convert_pp_tokens(vm, toks);
    return toks;
}

static Token *implicit_reflection_tokens(JCC *vm) {
    char *header = get_std_header("reflection.h");
    if (!header)
        error("could not load embedded reflection.h");

    // The user's translation unit may already have included stdbool.h,
    // stddef.h, or stdint.h. Temporarily clear those guards so reflection.h's
    // private macro API is processed completely in the macro compilation
    // scope. We restore the guards afterwards so subsequent compilation phases
    // are unaffected.
    static const char *guards[] = {
        "JCC_REFLECTION_H", "__STDBOOL_H", "__STDDEF_H", "__STDINT_H", NULL
    };
    void *saved_guards[4] = {};
    for (int i = 0; guards[i]; i++) {
        saved_guards[i] = hashmap_get(&vm->compiler.macros, (char *)guards[i]);
        if (saved_guards[i])
            hashmap_delete(&vm->compiler.macros, (char *)guards[i]);
    }

    Token *tokens = tokenize_string(vm, "<implicit-reflection.h>", header);
    Token *result = preprocess(vm, tokens);

    for (int i = 0; guards[i]; i++)
        if (saved_guards[i])
            hashmap_put(&vm->compiler.macros, (char *)guards[i], saved_guards[i]);

    return result;
}

static Token *build_combined_macro_tokens(JCC *vm, Token *reflection_tokens,
                                          MacroFn **macros, int count) {
    Token head = {};
    Token *cur = &head;

    cur = append_token_list(vm, cur, reflection_tokens);
    if (vm->compiler.macro_context_tokens)
        cur = append_token_list(vm, cur, vm->compiler.macro_context_tokens);

    // Reverse comptime_vars to source order (list is prepended, so reversed).
    int nv = 0;
    for (ComptimeVar *cv = vm->compiler.comptime_vars; cv; cv = cv->next)
        nv++;
    ComptimeVar **vars = nv > 0 ? alloca(nv * sizeof(ComptimeVar *)) : NULL;
    if (nv > 0) {
        int idx = nv - 1;
        for (ComptimeVar *cv = vm->compiler.comptime_vars; cv; cv = cv->next)
            vars[idx--] = cv;
    }

    // Inject comptime variable declarations as file-scope globals.
    // Vars routed through __jcc_comptime_init (ticket #191/#192) have their
    // initializer stripped so the parser never sees a non-constant expression
    // as a global initializer (which would hard-error via eval2). The stripped
    // var is declared as a zero-initialized global; __jcc_comptime_init fills
    // it in at VM run time.
    // Uninitialised vars, and aggregate vars whose initializer is constant or
    // whose tag cannot be synthesized, are injected as-is (constant path).
    for (int i = 0; i < nv; i++) {
        ComptimeVar *cv = vars[i];
        if (comptime_var_uses_init_fn(cv)) {
            // Strip initializer; __jcc_comptime_init will assign it.
            Token *eq = find_top_level_eq(cv->decl_tokens);
            cur = append_decl_stripped(vm, cur, cv->decl_tokens, eq);
        } else {
            cur = append_token_list(vm, cur, cv->decl_tokens);
        }
    }

    for (int i = 0; i < count; i++)
        cur = append_macro_prototype(vm, cur, macros[i]);
    for (int i = 0; i < count; i++)
        cur = append_macro_definition(vm, cur, macros[i]);

    // Synthesized init function: runs after bytecode is compiled to evaluate
    // scalar comptime var initializers that call comptime functions.
    Token *init_fn = build_comptime_init_fn_tokens(vm, vars, nv);
    if (init_fn)
        cur = append_token_list(vm, cur, init_fn);

    Token *tmpl = count > 0 ? macros[count - 1]->body_tokens : NULL;
    cur->next = new_macro_eof(vm, tmpl);
    return head.next;
}

static Obj *find_macro_function(Obj *prog, const char *name) {
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->is_function && obj->body &&
            strlen(obj->name) == strlen(name) &&
            strncmp(obj->name, name, strlen(name)) == 0)
            return obj;
    }
    return NULL;
}

static Obj *find_macro_global(Obj *prog, const char *name) {
    size_t len = strlen(name);
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_function && strlen(obj->name) == len &&
            strncmp(obj->name, name, len) == 0)
            return obj;
    }
    return NULL;
}

// Read a scalar value from the macro VM's data segment.
// Valid after init_macro_globals has allocated storage; the value may have
// been written by a constant-initializer memcpy, by __jcc_comptime_init, or
// left as zero (no initializer).
static bool read_comptime_scalar(JCC *vm, Obj *obj, bool *is_float_out,
                                 int64_t *int_out, double *float_out) {
    if (!obj)
        return false;
    char *base = vm->data_seg + obj->offset;
    TypeKind kind = obj->ty->kind;
    switch (kind) {
    case TY_FLOAT:
        *is_float_out = true;
        *float_out = (double)(*(float *)base);
        return true;
    case TY_DOUBLE:
    case TY_LDOUBLE:
        *is_float_out = true;
        *float_out = *(double *)base;
        return true;
    default:
        if (obj->ty->size == 1) { *int_out = obj->ty->is_unsigned ? (int64_t)*(uint8_t *)base  : (int64_t)*(int8_t *)base; }
        else if (obj->ty->size == 2) { *int_out = obj->ty->is_unsigned ? (int64_t)*(uint16_t *)base : (int64_t)*(int16_t *)base; }
        else if (obj->ty->size == 4) { *int_out = obj->ty->is_unsigned ? (int64_t)*(uint32_t *)base : (int64_t)*(int32_t *)base; }
        else { *int_out = (int64_t)*(int64_t *)base; }
        *is_float_out = false;
        return true;
    }
}

// Execute the synthesized __jcc_comptime_init function (if present) to
// evaluate scalar comptime variable initializers that call comptime
// functions. Must be called after gen_function + patch_macro_call_addresses
// so all bytecode and call targets are resolved.
static void run_comptime_var_initializers(JCC *vm, Obj *macro_prog) {
    Obj *init_fn = find_macro_function(macro_prog, "__jcc_comptime_init");
    if (!init_fn)
        return;

    if (vm->debug_vm)
        printf("Running __jcc_comptime_init for comptime variable initializers...\n");

    __jcc_current_vm = vm;

    JCCPc      saved_pc   = vm->pc;
    long long *saved_sp   = vm->sp;
    long long *saved_bp   = vm->bp;
    long long  saved_regs[NUM_REGS];
    memcpy(saved_regs, vm->regs, sizeof(saved_regs));
    Obj *saved_current_fn = vm->compiler.current_fn;

    vm->sp = vm->initial_sp;
    vm->bp = vm->initial_bp;
    *(--vm->sp) = 0; // sentinel return address

    vm->pc = (JCCPc)init_fn->code_addr;

    int saved_debug = vm->debug_vm;
    vm->debug_vm = 0;
    vm_eval(vm);
    vm->debug_vm = saved_debug;

    __jcc_current_vm = NULL;

    vm->pc            = saved_pc;
    vm->sp            = saved_sp;
    vm->bp            = saved_bp;
    memcpy(vm->regs, saved_regs, sizeof(saved_regs));
    vm->compiler.current_fn = saved_current_fn;

    if (vm->debug_vm)
        printf("__jcc_comptime_init completed.\n");
}

static void evaluate_comptime_vars(JCC *vm, Obj *macro_prog) {
    for (ComptimeVar *cv = vm->compiler.comptime_vars; cv; cv = cv->next) {
        if (cv->is_evaluated)
            continue;

        Obj *obj = find_macro_global(macro_prog, cv->name);
        if (!obj) {
            fprintf(stderr, "Warning: comptime var '%s' not found in macro program\n",
                    cv->name);
            continue;
        }

        // Pointer vars create relocations — rejected at preprocess time, but
        // guard here too in case something slips through.
        if (obj->rel) {
            fprintf(stderr, "Warning: comptime var '%s' has a relocation "
                    "(pointer/string vars are not yet supported)\n", cv->name);
            continue;
        }

        TypeKind kind = obj->ty->kind;
        if (kind == TY_STRUCT || kind == TY_UNION) {
            // Routed vars (ticket #192): __jcc_comptime_init wrote the bytes
            // into the data segment, so init_data is NULL — that is expected.
            // Non-routed vars (constant path): init_data must be present.
            // Anonymous/typedef'd structs with non-constant initializers are
            // not yet supported and fall through to the warning below.
            if (!obj->init_data && !comptime_var_uses_init_fn(cv)) {
                fprintf(stderr,
                        "Warning: comptime struct/union var '%s' has a "
                        "non-constant initializer and no synthesizable tag "
                        "(anonymous or typedef'd aggregates not yet supported)\n",
                        cv->name);
                continue;
            }

            cv->is_struct = true;
            char *base = vm->data_seg + obj->offset;
            for (Member *mem = obj->ty->members; mem; mem = mem->next) {
                if (mem->is_bitfield)
                    continue;
                TypeKind mk = mem->ty->kind;
                // Only scalar integer and float members are exposed.
                if (mk == TY_STRUCT || mk == TY_UNION || mk == TY_ARRAY ||
                    mk == TY_PTR)
                    continue;

                ComptimeVarMember *m =
                    arena_alloc(&vm->compiler.parser_arena, sizeof(ComptimeVarMember));
                memset(m, 0, sizeof(ComptimeVarMember));
                if (mem->name) {
                    char *mname = arena_alloc(&vm->compiler.parser_arena,
                                             mem->name->len + 1);
                    memcpy(mname, mem->name->loc, mem->name->len);
                    mname[mem->name->len] = '\0';
                    m->name = mname;
                }

                char *mbase = base + mem->offset;
                if (mk == TY_FLOAT) {
                    m->is_float = true;
                    m->float_val = (double)(*(float *)mbase);
                } else if (mk == TY_DOUBLE || mk == TY_LDOUBLE) {
                    m->is_float = true;
                    m->float_val = *(double *)mbase;
                } else {
                    int sz = mem->ty->size;
                    if (sz == 1) m->int_val = mem->ty->is_unsigned ? (int64_t)*(uint8_t *)mbase  : (int64_t)*(int8_t *)mbase;
                    else if (sz == 2) m->int_val = mem->ty->is_unsigned ? (int64_t)*(uint16_t *)mbase : (int64_t)*(int16_t *)mbase;
                    else if (sz == 4) m->int_val = mem->ty->is_unsigned ? (int64_t)*(uint32_t *)mbase : (int64_t)*(int32_t *)mbase;
                    else m->int_val = *(int64_t *)mbase;
                }

                m->next = cv->members;
                cv->members = m;
            }
        } else {
            // Scalar: read from data segment unconditionally. The value is
            // valid after init_macro_globals allocates storage — it was either
            // written by a constant-initializer memcpy (init_data path), by
            // __jcc_comptime_init (ticket #191 non-constant path), or is zero
            // for an uninitialised var.
            read_comptime_scalar(vm, obj, &cv->is_float, &cv->int_val, &cv->float_val);
        }

        cv->is_evaluated = true;

        if (vm->debug_vm) {
            if (cv->is_struct)
                printf("Evaluated comptime struct '%s'\n", cv->name);
            else if (cv->is_float)
                printf("Evaluated comptime var '%s' = %f\n", cv->name, cv->float_val);
            else
                printf("Evaluated comptime var '%s' = %lld\n", cv->name, cv->int_val);
        }
    }
}

static void init_macro_globals(JCC *vm, Obj *macro_prog) {
    int num_globals = 0;
    for (Obj *var = macro_prog; var; var = var->next) {
        if (!var->is_function)
            num_globals++;
    }

    if (num_globals == 0)
        return;

    Obj **globals_arr = alloca(num_globals * sizeof(Obj *));
    int idx = num_globals - 1;
    for (Obj *var = macro_prog; var; var = var->next) {
        if (!var->is_function)
            globals_arr[idx--] = var;
    }

    for (int i = 0; i < num_globals; i++) {
        Obj *var = globals_arr[i];

        long long offset = vm->data_ptr - vm->data_seg;
        offset = (offset + 7) & ~7;
        vm->data_ptr = vm->data_seg + offset;
        if (vm_data_ensure(vm, var->ty->size) != 0)
            error("codegen: data segment overflow (limit: %d bytes)", vm->poolsize_max);
        var->offset = vm->data_ptr - vm->data_seg;

        if (var->init_data)
            memcpy(vm->data_ptr, var->init_data, var->ty->size);
        if (var->rel)
            error("macro function global relocations are not supported");

        vm->data_ptr += var->ty->size;
    }
}

static void patch_macro_call_addresses(JCC *vm, Obj *macro_prog) {
    for (int i = 0; i < vm->compiler.num_call_patches; i++) {
        Obj *fn = vm->compiler.call_patches[i].function;
        JCCPc loc = vm->compiler.call_patches[i].location;
        Obj *fn_def = find_macro_function(macro_prog, fn->name);
        if (!fn_def)
            error("undefined function in macro bytecode: %s", fn->name);
        vm->text_seg[loc] = (JCCPc)fn_def->code_addr;
    }

    for (int i = 0; i < vm->compiler.num_func_addr_patches; i++) {
        Obj *fn = vm->compiler.func_addr_patches[i].function;
        JCCPc loc = vm->compiler.func_addr_patches[i].location;
        Obj *fn_def = find_macro_function(macro_prog, fn->name);
        if (!fn_def)
            error("undefined function address in macro bytecode: %s",
                  fn->name);
        cc_write_i64_at(vm, loc, cc_pc_to_byte_offset((JCCPc)fn_def->code_addr));
    }
}

// Compile all macro functions and comptime helpers as one compile-time program so
// macro bytecode can make ordinary function calls across the whole set.
static bool compile_macro_program(JCC *vm) {
    int count = 0;
    for (MacroFn *pm = vm->compiler.macro_fns; pm; pm = pm->next)
        count++;
    if (count == 0)
        return true;

    MacroFn **macros = alloca(count * sizeof(MacroFn *));
    int idx = count - 1;
    for (MacroFn *pm = vm->compiler.macro_fns; pm; pm = pm->next)
        macros[idx--] = pm;

    Obj *saved_locals = vm->compiler.locals;
    Obj *saved_current_fn = vm->compiler.current_fn;
    Obj *saved_globals = vm->compiler.globals;
    Scope *saved_scope = vm->compiler.scope;
    int saved_num_call_patches = vm->compiler.num_call_patches;
    int saved_num_func_addr_patches = vm->compiler.num_func_addr_patches;

    vm->compiler.in_macro_mode = true;
    vm->compiler.locals = NULL;
    vm->compiler.globals = NULL;
    vm->compiler.num_call_patches = 0;
    vm->compiler.num_func_addr_patches = 0;

    Token *reflection_tokens = implicit_reflection_tokens(vm);
    Token *tokens =
        build_combined_macro_tokens(vm, reflection_tokens, macros, count);
    tokens = preprocess(vm, tokens);
    Obj *macro_prog = parse(vm, tokens);
    if (!macro_prog) {
        vm->compiler.locals = saved_locals;
        vm->compiler.current_fn = saved_current_fn;
        vm->compiler.globals = saved_globals;
        for (Scope *sc = vm->compiler.scope; sc != saved_scope; sc = sc->next) {
            hashmap_deinit_borrowed(&sc->var_map);
            hashmap_deinit_borrowed(&sc->tag_map);
        }
        vm->compiler.scope = saved_scope;
        vm->compiler.in_macro_mode = false;
        vm->compiler.num_call_patches = saved_num_call_patches;
        vm->compiler.num_func_addr_patches = saved_num_func_addr_patches;
        return false;
    }
    vm->compiler.macro_context_scope = vm->compiler.scope;

    for (int i = 0; i < count; i++) {
        Obj *func = find_macro_function(macro_prog, macros[i]->name);
        if (!func) {
            if (vm->debug_vm)
                fprintf(stderr,
                        "Could not find macro function '%s' after parsing\n",
                        macros[i]->name);
            vm->compiler.locals = saved_locals;
            vm->compiler.current_fn = saved_current_fn;
            vm->compiler.globals = saved_globals;
            vm->compiler.scope = saved_scope;
            vm->compiler.in_macro_mode = false;
            vm->compiler.num_call_patches = saved_num_call_patches;
            vm->compiler.num_func_addr_patches = saved_num_func_addr_patches;
            return false;
        }
        macros[i]->compiled_fn = func;
        macros[i]->is_compiled = true;
    }

    // Step 1: allocate data segment storage for all globals and memcpy any
    //         constant initializer bytes (init_data path).
    init_macro_globals(vm, macro_prog);

    // Step 2: generate bytecode for all functions, including the synthesized
    //         __jcc_comptime_init helper produced by build_combined_macro_tokens.
    for (Obj *fn = macro_prog; fn; fn = fn->next) {
        if (fn->is_function && fn->body)
            gen_function(vm, fn);
    }

    // Step 3: patch call addresses so __jcc_comptime_init can call comptime fns.
    patch_macro_call_addresses(vm, macro_prog);

    // Step 4: run __jcc_comptime_init to evaluate scalar comptime var
    //         initializers (ticket #191). This writes results into the data
    //         segment via normal VM store instructions.
    run_comptime_var_initializers(vm, macro_prog);

    // Step 5: read comptime var values out of the data segment.
    evaluate_comptime_vars(vm, macro_prog);

    vm->compiler.locals = saved_locals;
    vm->compiler.current_fn = saved_current_fn;
    vm->compiler.globals = saved_globals;
    vm->compiler.scope = saved_scope;
    vm->compiler.in_macro_mode = false;
    vm->compiler.num_call_patches = saved_num_call_patches;
    vm->compiler.num_func_addr_patches = saved_num_func_addr_patches;

    if (vm->debug_vm) {
        for (int i = 0; i < count; i++) {
            printf("Compiled compile-time function '%s' at code address %lld\n",
                   macros[i]->name, macros[i]->compiled_fn->code_addr);
        }
    }

    return true;
}

// Compile all macro functions and comptime helpers (idempotent)
static void compile_all_macros(JCC *vm) {
    if (!vm->compiler.macro_fns)
        return;
    // Guard: compile once even if called from both the pre-parse inline phase
    // and the post-parse cc_expand_macros phase.
    if (vm->compiler.macro_fns_compiled)
        return;
    vm->compiler.macro_fns_compiled = true;

    if (vm->debug_vm)
        printf("Compiling %d compile-time function(s)...\n", ({
                   int n = 0;
                   for (MacroFn *p = vm->compiler.macro_fns; p;
                        p = p->next)
                       n++;
                   n;
               }));

    // Register reflection API as FFI
    register_reflection_ffi(vm);

    if (!compile_macro_program(vm))
        fprintf(stderr, "Warning: Failed to compile macro functions\n");
}

// Execute a macro function and return the generated AST node
static Node *execute_macro_fn(JCC *vm, MacroFn *pm, Token *call_tok,
                              Node *args, int arg_count) {
    if (!pm || !pm->is_compiled || !pm->compiled_fn)
        return NULL;

    if (vm->debug_vm)
        printf("Executing macro function '%s' with %d args...\n", pm->name,
               arg_count);

    // Set global VM pointer for __jcc_get_vm()
    __jcc_current_vm = vm;

    // Save VM execution state (including current_fn so a macro that calls
    // __jcc_ast_push_fn without a matching pop cannot leak context).
    JCCPc saved_pc = vm->pc;
    long long *saved_sp = vm->sp;
    long long *saved_bp = vm->bp;
    long long saved_regs[NUM_REGS];
    memcpy(saved_regs, vm->regs, sizeof(saved_regs));
    Obj *saved_current_fn = vm->compiler.current_fn;
    Token *saved_macro_call_tok = vm->compiler.macro_call_tok;
    vm->compiler.macro_call_tok = call_tok;

    // Reset stack for macro execution
    vm->sp = vm->initial_sp;
    vm->bp = vm->initial_bp;

    // Pass arguments via registers (REG_A0-A7 in the VM calling convention).
    // Arguments are Node* pointers to the AST nodes.
    int arg_idx = 0;
    for (Node *arg = args; arg && arg_idx < 8; arg = arg->next) {
        vm->regs[REG_A0 + arg_idx] = (long long)arg;
        arg_idx++;
    }

    // Push sentinel return address (0) so we can detect when function
    // returns
    *(--vm->sp) = 0;

    // Set PC to function entry point
    vm->pc = (JCCPc)pm->compiled_fn->code_addr;

    // Execute the macro function
    int saved_debug = vm->debug_vm;
    vm->debug_vm = 0; // Disable debug output during macro execution
    vm_eval(vm);
    vm->debug_vm = saved_debug;

    // Get the returned Node* from regs[REG_A0]
    Node *result = (Node *)vm->regs[REG_A0];

    // Clear VM pointer
    __jcc_current_vm = NULL;

    // Restore VM execution state (current_fn last so it overrides any leaked
    // push_fn call that wasn't matched by a pop_fn inside the macro).
    vm->pc = saved_pc;
    vm->sp = saved_sp;
    vm->bp = saved_bp;
    memcpy(vm->regs, saved_regs, sizeof(saved_regs));
    vm->compiler.current_fn = saved_current_fn;
    vm->compiler.macro_call_tok = saved_macro_call_tok;

    if (vm->debug_vm && result)
        printf("Macro function '%s' returned node of kind %d\n", pm->name,
               result->kind);

    return result;
}

// Find macro function by name
static MacroFn *find_macro_fn_by_name(JCC *vm, const char *name) {
    for (MacroFn *pm = vm->compiler.macro_fns; pm; pm = pm->next) {
        if (pm->is_macro_entry && strlen(pm->name) == strlen(name) &&
            strncmp(pm->name, name, strlen(name)) == 0)
            return pm;
    }
    return NULL;
}

// Ticket #277: Lisp-style single-step macro expansion (macroexpand-1).
// Expands the outermost ND_MACRO_CALL exactly once; identity for anything else.
Node *__jcc_macroexpand_1(JCC *vm, Node *node) {
    if (!vm || !node)
        return node;
    if (node->kind != ND_MACRO_CALL)
        return node;
    MacroFn *pm = find_macro_fn_by_name(vm, node->macro_name);
    if (!pm || !pm->is_compiled)
        return node;
    Scope *saved_scope = vm->compiler.scope;
    if (node->macro_scope)
        vm->compiler.scope = node->macro_scope;
    Node *result = execute_macro_fn(vm, pm, node->tok, node->args,
                                    node->macro_arg_count);
    vm->compiler.scope = saved_scope;
    return result ? result : node;
}

// Ticket #277: Lisp-style full macro expansion (macroexpand).
// Repeatedly expands the outermost node via macroexpand_1 until it is no
// longer an ND_MACRO_CALL (i.e. until the form is stable at the top level).
// Does not recurse into child nodes — only the top-level call is expanded.
Node *__jcc_macroexpand(JCC *vm, Node *node) {
    if (!vm || !node)
        return node;
    int limit = vm->compiler.macro_recursion_limit;
    int depth = 0;
    Node *current = node;
    while (current && current->kind == ND_MACRO_CALL) {
        if (limit > 0 && depth >= limit) {
            error_tok(vm, current->tok,
                      "macroexpand: recursion limit exceeded expanding "
                      "'%s' (depth %d, limit %d)",
                      current->macro_name, depth + 1, limit);
            return current;
        }
        Node *next = __jcc_macroexpand_1(vm, current);
        if (next == current)
            break;
        current = next;
        depth++;
    }
    return current;
}

void cc_execute_top_level_macro(JCC *vm, char *name, Token *tok,
                                       Node *args, int arg_count) {
    if (!vm || !name)
        return;

    MacroFn *pm = find_macro_fn_by_name(vm, name);
    if (!pm) {
        error_tok(vm, tok, "undefined macro: %s", name);
        return;
    }

    if (pm->is_inline) {
        error_tok(vm, tok, "inline macro '%s' cannot be called explicitly",
                  name);
        return;
    }

    if (arg_count > 8) {
        error_tok(vm, tok,
                  "macro '%s' called with %d arguments; maximum is 8",
                  name, arg_count);
        return;
    }

    init_vm_segments_for_macros(vm);
    compile_all_macros(vm);

    if (!pm->is_compiled) {
        error_tok(vm, tok, "macro '%s' failed to compile", name);
        return;
    }

    Node *result = execute_macro_fn(vm, pm, tok, args, arg_count);
    // Declaration position: NULL (or void return) is legal — the macro may have
    // emitted definitions as side-effects without having a node to splice.
    (void)result;
}

// Recursively transform macro calls in an AST node
static Node *transform_node(JCC *vm, Node *node, int depth);

static Node *transform_node(JCC *vm, Node *node, int depth) {
    if (!node)
        return NULL;

    // Handle ND_MACRO_CALL - this is where the magic happens
    if (node->kind == ND_MACRO_CALL) {
        if (vm->debug_vm)
            printf("  Found ND_MACRO_CALL for '%s'\n", node->macro_name);

        MacroFn *pm = find_macro_fn_by_name(vm, node->macro_name);
        if (!pm) {
            error_tok(vm, node->tok, "undefined macro: %s",
                      node->macro_name);
            return node;
        }

        if (pm->is_void_macro) {
            error_tok(vm, node->tok,
                      "void macro '%s' cannot be used as an expression; "
                      "it only emits definitions",
                      node->macro_name);
            return node;
        }

        if (!pm->is_compiled) {
            error_tok(vm, node->tok, "macro '%s' failed to compile",
                      node->macro_name);
            return node;
        }

        int limit = vm->compiler.macro_recursion_limit;
        if (limit > 0 && depth >= limit) {
            error_tok(vm, node->tok,
                      "macro recursion limit exceeded while expanding "
                      "'%s' (depth %d, limit %d)",
                      node->macro_name, depth + 1, limit);
            return node;
        }

        // Execute the macro to get the replacement AST
        if (vm->debug_vm)
            printf("  Executing macro '%s'...\n", pm->name);

        if (node->macro_arg_count > 8) {
            error_tok(vm, node->tok,
                      "macro '%s' called with %d arguments; maximum is 8",
                      node->macro_name, node->macro_arg_count);
            return node;
        }

        Scope *saved_scope = vm->compiler.scope;
        if (node->macro_scope)
            vm->compiler.scope = node->macro_scope;
        Node *result =
            execute_macro_fn(vm, pm, node->tok, node->args, node->macro_arg_count);
        vm->compiler.scope = saved_scope;

        if (vm->debug_vm)
            printf("  Macro returned %p (kind=%d)\n", (void *)result,
                   result ? result->kind : -1);

        if (!result) {
            error_tok(vm, node->tok, "macro '%s' returned NULL",
                      node->macro_name);
            // Return a placeholder
            Node *placeholder =
                arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
            memset(placeholder, 0, sizeof(Node));
            placeholder->kind = ND_NUM;
            placeholder->val = 0;
            placeholder->ty = ty_int;
            placeholder->tok = node->tok;
            return placeholder;
        }

        // Run add_type on the result to ensure types are set
        add_type(vm, result);

        // Recursively transform in case the macro result contains more
        // macro calls
        return transform_node(vm, result, depth + 1);
    }

    // For ND_EXPR_STMT: if the inner expression is replaced by a statement-kind
    // node (e.g. a macro returned ND_IF, ND_BLOCK, ND_RETURN, ...), lift the
    // statement up to replace the entire expression-statement wrapper.
    // Without this, codegen would try to gen_expr() a statement node and fail.
    //
    // When lifting, preserve the sibling chain: any statements that follow this
    // EXPR_STMT in the enclosing block must continue executing after the lifted
    // statement.  We attach them via ->next so the body traversal above picks
    // them up in subsequent loop iterations.
    if (node->kind == ND_EXPR_STMT) {
        node->lhs = transform_node(vm, node->lhs, depth);
        if (node->lhs) {
            NodeKind k = node->lhs->kind;
            if (k == ND_RETURN || k == ND_IF || k == ND_FOR || k == ND_DO ||
                k == ND_SWITCH || k == ND_BLOCK || k == ND_GOTO ||
                k == ND_LABEL || k == ND_EXPR_STMT) {
                Node *lifted = node->lhs;
                // Re-attach the sibling chain so statements after the macro
                // call are not dropped.  Walk to the tail of the lifted node
                // so we don't clobber a non-NULL ->next on the lifted result
                // (e.g. a macro that returned a pre-linked chain).
                if (node->next) {
                    Node *tail = lifted;
                    while (tail->next) tail = tail->next;
                    tail->next = node->next;
                }
                return lifted;
            }
        }
        return node;
    }

    // Recursively transform all child nodes
    node->lhs = transform_node(vm, node->lhs, depth);
    node->rhs = transform_node(vm, node->rhs, depth);
    node->cond = transform_node(vm, node->cond, depth);
    node->then = transform_node(vm, node->then, depth);
    node->els = transform_node(vm, node->els, depth);
    node->init = transform_node(vm, node->init, depth);
    node->inc = transform_node(vm, node->inc, depth);

    // For ND_BLOCK, body is a chain of statements linked via ->next
    // We need to transform each statement in the chain
    if (node->body) {
        node->body = transform_node(vm, node->body, depth);
        // Also transform sibling statements in the chain
        for (Node *stmt = node->body; stmt; stmt = stmt->next) {
            if (stmt->next) {
                stmt->next = transform_node(vm, stmt->next, depth);
            }
        }
    }

    // Transform argument lists (also a chain)
    if (node->args) {
        node->args = transform_node(vm, node->args, depth);
        for (Node *arg = node->args; arg && arg->next; arg = arg->next) {
            arg->next = transform_node(vm, arg->next, depth);
        }
    }

    // Transform case lists for switch
    for (Node *c = node->case_next; c; c = c->case_next) {
        c->body = transform_node(vm, c->body, depth);
    }
    if (node->default_case) {
        node->default_case->body =
            transform_node(vm, node->default_case->body, depth);
    }

    return node;
}

// Initialize VM segments for macro compilation (extracted from cc_compile)
static void init_vm_segments_for_macros(JCC *vm) {
    if (vm->text_seg)
        return; // Already initialized

    // Reserve and commit all segments (base pointers will never move)
    vm_alloc_segments(vm);

    // Initialize codegen state
    vm->compiler.current_codegen_fn = NULL;
    // sp/bp/stack_base already set correctly by vm_alloc_segments
}

// ---------------------------------------------------------------------------
// Inline macro pre-parse execution
// ---------------------------------------------------------------------------

// Write a C-syntax type string into buf[0..bufsize).
// Returns number of characters written (not counting the NUL).
// Handles primitives, pointer chains, struct/union/enum tags, and falls back
// to "int" for unrepresentable types. Does NOT handle function-pointer or
// array return types — those are uncommon for generated function signatures.
static int write_type_str(Type *ty, char *buf, int bufsize) {
    if (!ty || bufsize <= 1)
        return 0;

    int n = 0;

    if (ty->is_const && bufsize - n > 6)
        n += snprintf(buf + n, bufsize - n, "const ");

    switch (ty->kind) {
    case TY_VOID:
        n += snprintf(buf + n, bufsize - n, "void");
        break;
    case TY_BOOL:
        n += snprintf(buf + n, bufsize - n, "_Bool");
        break;
    case TY_CHAR:
        n += snprintf(buf + n, bufsize - n, "%schar",
                      ty->is_unsigned ? "unsigned " : "");
        break;
    case TY_SHORT:
        n += snprintf(buf + n, bufsize - n, "%sshort",
                      ty->is_unsigned ? "unsigned " : "");
        break;
    case TY_INT:
        n += snprintf(buf + n, bufsize - n, "%sint",
                      ty->is_unsigned ? "unsigned " : "");
        break;
    case TY_LONG:
        n += snprintf(buf + n, bufsize - n, "%slong",
                      ty->is_unsigned ? "unsigned " : "");
        break;
    case TY_FLOAT:
        n += snprintf(buf + n, bufsize - n, "float");
        break;
    case TY_DOUBLE:
        n += snprintf(buf + n, bufsize - n, "double");
        break;
    case TY_PTR:
        n += write_type_str(ty->base, buf + n, bufsize - n);
        n += snprintf(buf + n, bufsize - n, " *");
        break;
    case TY_STRUCT:
        if (ty->name)
            n += snprintf(buf + n, bufsize - n, "struct %.*s",
                          ty->name->len, ty->name->loc);
        else
            n += snprintf(buf + n, bufsize - n, "void /*anon struct*/");
        break;
    case TY_UNION:
        if (ty->name)
            n += snprintf(buf + n, bufsize - n, "union %.*s",
                          ty->name->len, ty->name->loc);
        else
            n += snprintf(buf + n, bufsize - n, "void /*anon union*/");
        break;
    case TY_ENUM:
        if (ty->name)
            n += snprintf(buf + n, bufsize - n, "enum %.*s",
                          ty->name->len, ty->name->loc);
        else
            n += snprintf(buf + n, bufsize - n, "int");
        break;
    default:
        // Fallback: emit int. Covers edge cases like TY_LDOUBLE, TY_ARRAY,
        // TY_FUNC return types, etc.
        n += snprintf(buf + n, bufsize - n, "int");
        break;
    }
    return n;
}

// Build a C prototype token stream for fn, e.g.:
//   int generated_func(void);
// Prepend these tokens to a file's token stream to give the parser a
// forward declaration without requiring the user to write one.
static Token *synthesize_forward_decl_tokens(JCC *vm, Obj *fn) {
    if (!fn || !fn->ty || fn->ty->kind != TY_FUNC)
        return NULL;

    char buf[512];
    char *p   = buf;
    char *end = buf + sizeof(buf) - 2; // leave room for ";\n\0"

    // Return type
    p += write_type_str(fn->ty->return_ty, p, (int)(end - p));

    // Space + function name
    p += snprintf(p, end - p, " %s(", fn->name);

    // Parameter types
    if (fn->ty->params == NULL) {
        p += snprintf(p, end - p, "void");
    } else {
        bool first = true;
        for (Type *pt = fn->ty->params; pt; pt = pt->next) {
            if (!first)
                p += snprintf(p, end - p, ", ");
            first = false;
            p += write_type_str(pt, p, (int)(end - p));
        }
        if (fn->ty->is_variadic)
            p += snprintf(p, end - p, ", ...");
    }

    p += snprintf(p, end - p, ");\n");

    if (vm->debug_vm)
        printf("Synthesized forward decl: %s", buf);

    // Tokenise and convert to parser tokens
    Token *toks = tokenize_string(vm, "<inline-macro-fwd>", buf);
    if (!toks)
        return NULL;
    convert_pp_tokens(vm, toks);
    return toks;
}

// Build an extern declaration token stream for a generated global variable.
// Handles arrays by walking the type chain: e.g. char[6] emits
//   extern char banner_data[6];
// rather than the incorrect  extern int banner_data;  that write_type_str's
// TY_ARRAY fallback would produce.
static Token *synthesize_global_decl_tokens(JCC *vm, Obj *var) {
    if (!var || var->is_function)
        return NULL;

    char buf[512];
    char *p   = buf;
    char *end = buf + sizeof(buf) - 2;

    p += snprintf(p, end - p, "extern ");

    // For array types, emit base_type name[len1][len2]... syntax.
    // Walk the type chain to collect dimensions, then emit them after the name.
    if (var->ty && var->ty->kind == TY_ARRAY) {
        // Collect array dimensions
        int dims[16];
        int ndims = 0;
        Type *t = var->ty;
        while (t && t->kind == TY_ARRAY && ndims < 16) {
            dims[ndims++] = t->array_len;
            t = t->base;
        }
        // t is now the element type
        p += write_type_str(t, p, (int)(end - p));
        p += snprintf(p, end - p, " %s", var->name);
        for (int i = 0; i < ndims; i++)
            p += snprintf(p, end - p, "[%d]", dims[i]);
        p += snprintf(p, end - p, ";\n");
    } else {
        p += write_type_str(var->ty, p, (int)(end - p));
        p += snprintf(p, end - p, " %s;\n", var->name);
    }

    if (vm->debug_vm)
        printf("Synthesized global extern decl: %s", buf);

    Token *toks = tokenize_string(vm, "<inline-macro-gvar>", buf);
    if (!toks)
        return NULL;
    convert_pp_tokens(vm, toks);
    return toks;
}

// Scan a single token stream for file-scope calls to non-inline macros,
// execute them, collect generated definitions, and remove the call tokens.
// Newly generated Obj definitions are drained into vm->compiler.macro_globals
// immediately after each execution using a saved-next walk, so globals is
// always restored to its pre-call state and no cycle can form.
static void scan_and_execute_global_calls(JCC *vm, Token **tokens_ptr) {
    Token *prev = NULL;
    Token *tok = *tokens_ptr;
    int brace_depth = 0;
    int paren_depth = 0;

    while (tok && tok->kind != TK_EOF) {
        // Track brace/paren depth
        if (equal(tok, "{")) brace_depth++;
        else if (equal(tok, "}")) brace_depth--;
        else if (equal(tok, "(")) paren_depth++;
        else if (equal(tok, ")")) paren_depth--;

        // Only match at file scope (outside braces and parens)
        if (brace_depth == 0 && paren_depth == 0 &&
            tok->kind == TK_IDENT && tok->next && equal(tok->next, "(")) {
            // Check if this identifier is a non-inline macro
            MacroFn *pm = NULL;
            for (MacroFn *m = vm->compiler.macro_fns; m; m = m->next) {
                if (m->is_macro_entry && !m->is_inline &&
                    strlen(m->name) == tok->len &&
                    strncmp(m->name, tok->loc, tok->len) == 0) {
                    pm = m;
                    break;
                }
            }

            if (pm) {
                // Find matching ')'
                Token *after_paren = tok->next->next;
                int call_depth = 1;
                while (after_paren && after_paren->kind != TK_EOF && call_depth > 0) {
                    if (equal(after_paren, "(")) call_depth++;
                    else if (equal(after_paren, ")")) {
                        call_depth--;
                        if (call_depth == 0) break;
                    }
                    after_paren = after_paren->next;
                }

                if (after_paren && call_depth == 0) {
                    Token *after_semi = after_paren->next;
                    if (after_semi && equal(after_semi, ";")) {
                        Token *next_tok = after_semi->next;

                        // For now, only zero-argument calls are supported
                        // for pre-parse global generation.
                        Token *arg_check = tok->next->next; // after '('
                        if (arg_check != after_paren) {
                            error_tok(vm, tok,
                                "file-scope macro call '%.*s' with arguments is not supported for global generation",
                                tok->len, tok->loc);
                        }

                        if (!pm->is_compiled) {
                            error_tok(vm, tok, "macro '%.*s' failed to compile",
                                      tok->len, tok->loc);
                        }

                        // Snapshot globals before execution so we can identify
                        // the objects this call generates.
                        Obj *globals_before = vm->compiler.globals;
                        Scope *scope_before = vm->compiler.scope;
                        Scope *saved_scope_next =
                            scope_before ? scope_before->next : NULL;
                        if (scope_before && vm->compiler.macro_context_scope)
                            scope_before->next =
                                vm->compiler.macro_context_scope;

                        execute_macro_fn(vm, pm, tok, NULL, 0);
                        if (scope_before)
                            scope_before->next = saved_scope_next;
                        vm->compiler.scope = scope_before;

                        // Drain newly prepended objects into macro_globals using
                        // a saved-next walk so we never overwrite a next pointer
                        // we still need to follow (which would create a cycle).
                        Obj *o = vm->compiler.globals;
                        while (o && o != globals_before) {
                            Obj *next_obj = o->next;
                            o->next = vm->compiler.macro_globals;
                            vm->compiler.macro_globals = o;
                            o = next_obj;
                        }
                        vm->compiler.globals = globals_before;

                        // Remove the call tokens from the stream
                        if (prev) {
                            prev->next = next_tok;
                        } else {
                            *tokens_ptr = next_tok;
                        }
                        tok = next_tok;
                        continue;
                    }
                }
            }
        }

        prev = tok;
        tok = tok->next;
    }
}

// Execute file-scope calls to non-inline macros before the main parse.
// For each file-scope call:
//   1. Scan the preprocessed token stream for zero-arg calls to non-inline
//      macros at file scope (brace depth 0, paren depth 0).
//   2. Execute the macro (it calls __jcc_ast_function etc.).
//   3. Drain newly-added Objs into vm->compiler.macro_globals immediately
//      (safe saved-next walk; globals is restored to its pre-call state).
//   4. Remove the call tokens so the parser never sees them.
//   5. Synthesize forward-declaration token streams for each generated
//      function/global and prepend them to every input_tokens[i].
//
// After this runs, vm->compiler.macro_globals contains the generated
// definitions. main.c appends them to the merged program before codegen.
void cc_execute_inline_macros(JCC *vm, Token **input_tokens, int count) {
    if (!vm || !vm->compiler.macro_fns)
        return;

    if (!vm->compiler.macro_context_tokens)
        vm->compiler.macro_context_tokens =
            build_macro_context_tokens(vm, input_tokens, count);

    // Quick check: any non-inline macros?
    bool any_global = false;
    for (MacroFn *pm = vm->compiler.macro_fns; pm; pm = pm->next)
        if (!pm->is_inline) { any_global = true; break; }
    if (!any_global)
        return;

    if (vm->debug_vm)
        printf("Pre-parse: executing global macro calls...\n");

    // Segments must be initialised before macro bytecode can run.
    init_vm_segments_for_macros(vm);

    // Compile all macros (idempotent — subsequent call from
    // cc_expand_macros is a no-op).
    compile_all_macros(vm);

    // Ensure a top-level scope exists so that macros can register typedefs,
    // enums, and struct tags that later parser code needs to resolve.
    if (!vm->compiler.scope) {
        Scope *sc = arena_alloc(&vm->compiler.parser_arena, sizeof(Scope));
        memset(sc, 0, sizeof(Scope));
        vm->compiler.scope = sc;
    }

    // Scan every input token stream for file-scope calls.
    // scan_and_execute_global_calls drains generated objects into
    // macro_globals directly and restores globals after each call,
    // so no bulk-move is needed here.
    for (int fi = 0; fi < count; fi++) {
        if (!input_tokens[fi])
            continue;
        scan_and_execute_global_calls(vm, &input_tokens[fi]);
    }

    // Synthesize forward declarations for every generated function and
    // extern declarations for every generated global variable, prepending
    // them to all input token streams so the parser can resolve references.
    for (Obj *o = vm->compiler.macro_globals; o; o = o->next) {
        bool is_fn_def  = o->is_function  && o->body &&
                          o->is_macro_generated;
        bool is_gvar_def = !o->is_function && o->is_definition &&
                            o->is_macro_generated;
        if (!is_fn_def && !is_gvar_def)
            continue;

        for (int fi = 0; fi < count; fi++) {
            if (!input_tokens[fi])
                continue;
            Token *decl = is_fn_def
                ? synthesize_forward_decl_tokens(vm, o)
                : synthesize_global_decl_tokens(vm, o);
            if (!decl)
                continue;
            Token *tail = decl;
            while (tail->next && tail->next->kind != TK_EOF)
                tail = tail->next;
            tail->next = input_tokens[fi];
            input_tokens[fi] = decl;
        }
    }

    if (vm->debug_vm)
        printf("Pre-parse global macro execution complete.\n");
}

// Expand all macro calls in the program
void cc_expand_macros(JCC *vm, Obj *prog) {
    if (!vm || !prog)
        return;

    // If no macro functions were captured, nothing to do
    if (!vm->compiler.macro_fns)
        return;

    if (vm->debug_vm)
        printf("Expanding macros in program...\n");

    // Initialize VM segments if not already done (needed for macro
    // compilation)
    init_vm_segments_for_macros(vm);

    // Enter macro expansion mode
    vm->compiler.in_macro_expansion = true;

    // First, compile all macro functions
    compile_all_macros(vm);

    // Then walk the AST and expand macro calls
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (!fn->is_function || !fn->body)
            continue;

        if (vm->debug_vm)
            printf("Expanding macros in function '%s'...\n", fn->name);

        // Set current function context, including the locals list so that
        // any new_lvar() calls inside __jcc_quote (e.g. pointer temps for
        // compound assignments in quote templates) are added to THIS
        // function's locals and get proper stack-offset allocation in codegen.
        vm->compiler.current_fn = fn;
        vm->compiler.locals = fn->locals;

        // Transform the function body
        fn->body = transform_node(vm, fn->body, 0);

        // Flush new locals (created by quote templates) back into fn->locals.
        fn->locals = vm->compiler.locals;
    }

    // Clear locals so a stray new_lvar in a global-init comptime context
    // cannot silently attach to the last function's frame.
    vm->compiler.locals = NULL;
    vm->compiler.current_fn = NULL;

    // Also check global initializers
    for (Obj *var = prog; var; var = var->next) {
        if (var->is_function)
            continue;
        if (var->init_expr) {
            var->init_expr = transform_node(vm, var->init_expr, 0);
        }
    }

    vm->compiler.in_macro_expansion = false;

    if (vm->debug_vm)
        printf("Macro expansion complete.\n");
}
