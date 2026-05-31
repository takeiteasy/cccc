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

// Pragma macro compilation and execution subsystem
// Compiles #pragma macro functions and expands macro calls in the AST

#include "./internal.h"

// External declaration from reflect.c
extern JCC *__jcc_current_vm;

// Forward declarations for reflection API functions (to register as FFI)
extern JCC *__jcc_get_vm(void);
extern const char *__jcc_gensym(JCC *vm, const char *prefix);
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

// Ticket #1: quasi-quoting
extern Node *__jcc_quote(JCC *vm, const char *tmpl, ...);
extern Node *__jcc_quote_n(JCC *vm, const char *tmpl, Node **nodes, int count);

// Register reflection API functions as FFI
static void register_reflection_ffi(JCC *vm) {
    // VM accessor
    cc_register_cfunc(vm, "__jcc_get_vm", (void *)__jcc_get_vm, 0, 0);
    cc_register_cfunc(vm, "__jcc_gensym", (void *)__jcc_gensym, 2, 0);

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
    cc_register_variadic_cfunc(vm, "__jcc_quote",   (void *)__jcc_quote,   2, 0);
    cc_register_cfunc(vm,          "__jcc_quote_n", (void *)__jcc_quote_n, 4, 0);
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

static Token *append_macro_prototype(JCC *vm, Token *cur, PragmaMacro *pm) {
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

static Token *append_macro_definition(JCC *vm, Token *cur, PragmaMacro *pm) {
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

static Token *implicit_reflection_tokens(JCC *vm) {
    char *header = get_std_header("reflection.h");
    if (!header)
        error("could not load embedded reflection.h");

    // The user's translation unit may have already #included reflection.h
    // (and its transitive includes: stdbool.h, stddef.h, stdint.h), causing
    // their include guards to be set.  Temporarily clear those guards so the
    // full content is processed and all typedefs land in the macro compilation
    // scope.  We restore the guards afterwards so subsequent compilation
    // phases are unaffected.
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
                                          PragmaMacro **macros, int count) {
    Token head = {};
    Token *cur = &head;

    cur = append_token_list(vm, cur, reflection_tokens);

    for (int i = 0; i < count; i++)
        cur = append_macro_prototype(vm, cur, macros[i]);
    for (int i = 0; i < count; i++)
        cur = append_macro_definition(vm, cur, macros[i]);

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
            error("pragma macro global relocations are not supported");

        vm->data_ptr += var->ty->size;
    }
}

static void patch_macro_call_addresses(JCC *vm, Obj *macro_prog) {
    for (int i = 0; i < vm->compiler.num_call_patches; i++) {
        Obj *fn = vm->compiler.call_patches[i].function;
        JCCPc loc = vm->compiler.call_patches[i].location;
        Obj *fn_def = find_macro_function(macro_prog, fn->name);
        if (!fn_def)
            error("undefined function in pragma macro bytecode: %s", fn->name);
        vm->text_seg[loc] = (JCCPc)fn_def->code_addr;
    }

    for (int i = 0; i < vm->compiler.num_func_addr_patches; i++) {
        Obj *fn = vm->compiler.func_addr_patches[i].function;
        JCCPc loc = vm->compiler.func_addr_patches[i].location;
        Obj *fn_def = find_macro_function(macro_prog, fn->name);
        if (!fn_def)
            error("undefined function address in pragma macro bytecode: %s",
                  fn->name);
        cc_write_i64_at(vm, loc, cc_pc_to_byte_offset((JCCPc)fn_def->code_addr));
    }
}

// Compile all pragma macros and comptime helpers as one compile-time program so
// macro bytecode can make ordinary function calls across the whole set.
static bool compile_pragma_macro_program(JCC *vm) {
    int count = 0;
    for (PragmaMacro *pm = vm->compiler.pragma_macros; pm; pm = pm->next)
        count++;
    if (count == 0)
        return true;

    PragmaMacro **macros = alloca(count * sizeof(PragmaMacro *));
    int idx = count - 1;
    for (PragmaMacro *pm = vm->compiler.pragma_macros; pm; pm = pm->next)
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
        vm->compiler.scope = saved_scope;
        vm->compiler.in_macro_mode = false;
        vm->compiler.num_call_patches = saved_num_call_patches;
        vm->compiler.num_func_addr_patches = saved_num_func_addr_patches;
        return false;
    }

    for (int i = 0; i < count; i++) {
        Obj *func = find_macro_function(macro_prog, macros[i]->name);
        if (!func) {
            if (vm->debug_vm)
                fprintf(stderr,
                        "Could not find pragma macro function '%s' after parsing\n",
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

    init_macro_globals(vm, macro_prog);

    for (Obj *fn = macro_prog; fn; fn = fn->next) {
        if (fn->is_function && fn->body)
            gen_function(vm, fn);
    }

    patch_macro_call_addresses(vm, macro_prog);

    vm->compiler.locals = saved_locals;
    vm->compiler.current_fn = saved_current_fn;
    vm->compiler.globals = saved_globals;
    vm->compiler.scope = saved_scope;
    vm->compiler.in_macro_mode = false;
    vm->compiler.num_call_patches = saved_num_call_patches;
    vm->compiler.num_func_addr_patches = saved_num_func_addr_patches;

    if (vm->debug_vm) {
        for (int i = 0; i < count; i++) {
            printf("Compiled pragma compile-time function '%s' at code address %lld\n",
                   macros[i]->name, macros[i]->compiled_fn->code_addr);
        }
    }

    return true;
}

// Compile all pragma macros and comptime helpers (idempotent)
static void compile_all_pragma_macros(JCC *vm) {
    if (!vm->compiler.pragma_macros)
        return;
    // Guard: compile once even if called from both the pre-parse inline phase
    // and the post-parse cc_expand_pragma_macros phase.
    if (vm->compiler.pragma_macros_compiled)
        return;
    vm->compiler.pragma_macros_compiled = true;

    if (vm->debug_vm)
        printf("Compiling %d pragma compile-time function(s)...\n", ({
                   int n = 0;
                   for (PragmaMacro *p = vm->compiler.pragma_macros; p;
                        p = p->next)
                       n++;
                   n;
               }));

    // Register reflection API as FFI
    register_reflection_ffi(vm);

    if (!compile_pragma_macro_program(vm))
        fprintf(stderr, "Warning: Failed to compile pragma macros\n");
}

// Execute a pragma macro and return the generated AST node
static Node *execute_pragma_macro(JCC *vm, PragmaMacro *pm, Node *args,
                                  int arg_count) {
    if (!pm || !pm->is_compiled || !pm->compiled_fn)
        return NULL;

    if (vm->debug_vm)
        printf("Executing pragma macro '%s' with %d args...\n", pm->name,
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

    if (vm->debug_vm && result)
        printf("Pragma macro '%s' returned node of kind %d\n", pm->name,
               result->kind);

    return result;
}

// Find pragma macro by name
static PragmaMacro *find_pragma_macro_by_name(JCC *vm, const char *name) {
    for (PragmaMacro *pm = vm->compiler.pragma_macros; pm; pm = pm->next) {
        if (pm->is_macro_entry && strlen(pm->name) == strlen(name) &&
            strncmp(pm->name, name, strlen(name)) == 0)
            return pm;
    }
    return NULL;
}

void cc_execute_top_level_pragma_macro(JCC *vm, char *name, Token *tok,
                                       Node *args, int arg_count) {
    if (!vm || !name)
        return;

    PragmaMacro *pm = find_pragma_macro_by_name(vm, name);
    if (!pm) {
        error_tok(vm, tok, "undefined pragma macro: %s", name);
        return;
    }

    if (pm->is_inline) {
        error_tok(vm, tok, "inline pragma macro '%s' cannot be called explicitly",
                  name);
        return;
    }

    if (arg_count > 8) {
        error_tok(vm, tok,
                  "pragma macro '%s' called with %d arguments; maximum is 8",
                  name, arg_count);
        return;
    }

    init_vm_segments_for_macros(vm);
    compile_all_pragma_macros(vm);

    if (!pm->is_compiled) {
        error_tok(vm, tok, "pragma macro '%s' failed to compile", name);
        return;
    }

    Node *result = execute_pragma_macro(vm, pm, args, arg_count);
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

        PragmaMacro *pm = find_pragma_macro_by_name(vm, node->macro_name);
        if (!pm) {
            error_tok(vm, node->tok, "undefined pragma macro: %s",
                      node->macro_name);
            return node;
        }

        if (pm->is_void_macro) {
            error_tok(vm, node->tok,
                      "void pragma macro '%s' cannot be used as an expression; "
                      "it only emits definitions",
                      node->macro_name);
            return node;
        }

        if (!pm->is_compiled) {
            error_tok(vm, node->tok, "pragma macro '%s' failed to compile",
                      node->macro_name);
            return node;
        }

        int limit = vm->compiler.macro_recursion_limit;
        if (limit > 0 && depth >= limit) {
            error_tok(vm, node->tok,
                      "pragma macro recursion limit exceeded while expanding "
                      "'%s' (depth %d, limit %d)",
                      node->macro_name, depth + 1, limit);
            return node;
        }

        // Execute the macro to get the replacement AST
        if (vm->debug_vm)
            printf("  Executing macro '%s'...\n", pm->name);

        if (node->macro_arg_count > 8) {
            error_tok(vm, node->tok,
                      "pragma macro '%s' called with %d arguments; maximum is 8",
                      node->macro_name, node->macro_arg_count);
            return node;
        }

        Scope *saved_scope = vm->compiler.scope;
        if (node->macro_scope)
            vm->compiler.scope = node->macro_scope;
        Node *result =
            execute_pragma_macro(vm, pm, node->args, node->macro_arg_count);
        vm->compiler.scope = saved_scope;

        if (vm->debug_vm)
            printf("  Macro returned %p (kind=%d)\n", (void *)result,
                   result ? result->kind : -1);

        if (!result) {
            error_tok(vm, node->tok, "pragma macro '%s' returned NULL",
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
    // node (e.g. a macro returned ND_IF or ND_RETURN), lift the statement up
    // to replace the entire expression-statement wrapper.  Without this,
    // codegen would try to gen_expr() a statement node and fail.
    if (node->kind == ND_EXPR_STMT) {
        node->lhs = transform_node(vm, node->lhs, depth);
        if (node->lhs) {
            NodeKind k = node->lhs->kind;
            if (k == ND_RETURN || k == ND_IF || k == ND_FOR || k == ND_DO ||
                k == ND_SWITCH || k == ND_BLOCK || k == ND_GOTO ||
                k == ND_LABEL || k == ND_EXPR_STMT)
                return node->lhs;
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

// Execute all inline (#pragma macro inline) macros before the main parse.
// For each inline macro:
//   1. Execute it (it calls __jcc_ast_function etc. to register generated
//      function Objs in vm->compiler.globals).
//   2. Capture newly-added Objs and stash them in vm->compiler.macro_globals.
//   3. Synthesize a forward-declaration token stream for each generated
//      function and prepend it to every input_tokens[i].
//
// After all inline macros have run, vm->compiler.macro_globals contains the
// generated function definitions. main.c appends them to the merged program
// before codegen so the name-based call patcher can resolve calls to them.
void cc_execute_inline_macros(JCC *vm, Token **input_tokens, int count) {
    if (!vm || !vm->compiler.pragma_macros)
        return;

    // Quick check: any inline macros?
    bool any_inline = false;
    for (PragmaMacro *pm = vm->compiler.pragma_macros; pm; pm = pm->next)
        if (pm->is_inline) { any_inline = true; break; }
    if (!any_inline)
        return;

    if (vm->debug_vm)
        printf("Pre-parse: executing inline pragma macros...\n");

    // Segments must be initialised before macro bytecode can run.
    init_vm_segments_for_macros(vm);

    // Compile all macros (idempotent — subsequent call from
    // cc_expand_pragma_macros is a no-op).
    compile_all_pragma_macros(vm);

    // Walk the macro list in declaration order (list is prepended, so we
    // reverse to preserve source order).
    int macro_count = 0;
    for (PragmaMacro *pm = vm->compiler.pragma_macros; pm; pm = pm->next)
        macro_count++;

    PragmaMacro **ordered = alloca(macro_count * sizeof(PragmaMacro *));
    int idx = macro_count - 1;
    for (PragmaMacro *pm = vm->compiler.pragma_macros; pm; pm = pm->next)
        ordered[idx--] = pm;

    for (int i = 0; i < macro_count; i++) {
        PragmaMacro *pm = ordered[i];
        if (!pm->is_inline)
            continue;

        // Snapshot the globals list head before execution. Any Obj prepended
        // by __jcc_ast_function during macro execution will appear before this
        // pointer.
        Obj *globals_before = vm->compiler.globals;

        // Execute the inline macro (no arguments — inline macros are
        // zero-parameter by convention).
        execute_pragma_macro(vm, pm, NULL, 0);

        // Collect newly-added Objs (those prepended before globals_before):
        // function definitions AND global variable definitions.
        for (Obj *o = vm->compiler.globals; o && o != globals_before; o = o->next) {
            bool is_fn_def  = o->is_function  && o->body;
            bool is_gvar_def = !o->is_function && o->is_definition;
            if (!is_fn_def && !is_gvar_def)
                continue;

            // Move into macro_globals list (we prepend; order restored later).
            o->next = vm->compiler.macro_globals;
            vm->compiler.macro_globals = o;
        }

        // Synthesize forward declarations for every generated function and
        // extern declarations for every generated global variable, prepending
        // them to all input token streams so the parser can resolve references.
        for (Obj *o = vm->compiler.globals; o && o != globals_before; o = o->next) {
            bool is_fn_def  = o->is_function  && o->body;
            bool is_gvar_def = !o->is_function && o->is_definition;
            if (!is_fn_def && !is_gvar_def)
                continue;

            // Prepend to every file's token stream.
            for (int fi = 0; fi < count; fi++) {
                if (!input_tokens[fi])
                    continue;
                // Re-synthesise each time: tokenize_string allocates from the
                // arena (long-lived), and re-synthesising avoids tail->next
                // aliasing when prepending to multiple files.
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
    }

    if (vm->debug_vm)
        printf("Pre-parse inline macro execution complete.\n");
}

// Expand all pragma macro calls in the program
void cc_expand_pragma_macros(JCC *vm, Obj *prog) {
    if (!vm || !prog)
        return;

    // If no pragma macros were captured, nothing to do
    if (!vm->compiler.pragma_macros)
        return;

    if (vm->debug_vm)
        printf("Expanding pragma macros in program...\n");

    // Initialize VM segments if not already done (needed for macro
    // compilation)
    init_vm_segments_for_macros(vm);

    // Enter macro expansion mode
    vm->compiler.in_macro_expansion = true;

    // First, compile all pragma macros
    compile_all_pragma_macros(vm);

    // Then walk the AST and expand macro calls
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (!fn->is_function || !fn->body)
            continue;

        if (vm->debug_vm)
            printf("Expanding macros in function '%s'...\n", fn->name);

        // Set current function context
        vm->compiler.current_fn = fn;

        // Transform the function body
        fn->body = transform_node(vm, fn->body, 0);
    }

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
        printf("Pragma macro expansion complete.\n");
}
