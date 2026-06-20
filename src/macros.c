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

// Macro compilation and execution subsystem
// Compiles [[cccc::macro]] / __attribute__((macro)) functions and expands
// macro calls in the AST

#include "./internal.h"

// External declaration from relfection.c
extern VirtualMachine *__builtin_current_vm;

// Forward declarations for reflection API functions (to register as FFI)
extern VirtualMachine *__builtin_get_vm(void);
extern const char *__builtin_gensym(VirtualMachine *vm, const char *prefix);
extern Token *__builtin_ast_current_token(VirtualMachine *vm);
extern Token *__builtin_ast_synthetic_token(VirtualMachine *vm, const char *label);
extern Token *__builtin_ast_token_from_node(Node *node);
extern Node *__builtin_ast_set_token(Node *node, Token *tok);
extern Node *__builtin_ast_copy_location(Node *dst, Node *src);
extern Type *__builtin_ast_find_type(VirtualMachine *vm, const char *name);
extern bool __builtin_ast_type_exists(VirtualMachine *vm, const char *name);
extern Type *__builtin_ast_get_type(VirtualMachine *vm, const char *name);
extern TypeKind __builtin_ast_type_kind(Type *ty);
extern int __builtin_ast_type_size(Type *ty);
extern int __builtin_ast_type_align(Type *ty);
extern bool __builtin_ast_type_is_unsigned(Type *ty);
extern bool __builtin_ast_type_is_const(Type *ty);
extern Type *__builtin_ast_type_base(Type *ty);
extern int __builtin_ast_type_array_len(Type *ty);
extern Type *__builtin_ast_type_return_type(Type *ty);
extern int __builtin_ast_type_param_count(Type *ty);
extern Type *__builtin_ast_type_param_at(Type *ty, int index);
extern bool __builtin_ast_type_is_variadic(Type *ty);
extern const char *__builtin_ast_type_name(Type *ty);
extern const char *__builtin_ast_type_c_name(VirtualMachine *vm, Type *ty);
extern Node *__builtin_ast_int_literal(VirtualMachine *vm, int64_t value);
extern Node *__builtin_ast_float_literal(VirtualMachine *vm, double value);
extern Node *__builtin_ast_string_literal(VirtualMachine *vm, const char *str);
extern Node *__builtin_ast_var_ref(VirtualMachine *vm, const char *name);
extern Node *__builtin_ast_binary(VirtualMachine *vm, NodeKind op, Node *left, Node *right);
extern Node *__builtin_ast_unary(VirtualMachine *vm, NodeKind op, Node *operand);
extern Node *__builtin_ast_cast(VirtualMachine *vm, Node *expr, Type *target_type);
extern Node *__builtin_ast_return(VirtualMachine *vm, Node *expr);
extern Node *__builtin_ast_if(VirtualMachine *vm, Node *cond, Node *then_body, Node *else_body);
extern Node *__builtin_ast_switch(VirtualMachine *vm, Node *cond);
extern void __builtin_ast_switch_add_case(VirtualMachine *vm, Node *switch_node, Node *value,
                                Node *body);
extern void __builtin_ast_switch_set_default(VirtualMachine *vm, Node *switch_node, Node *body);
extern Node *__builtin_ast_block_add_stmt(VirtualMachine *vm, Node *block, Node *stmt);
extern Node *__builtin_ast_block_add_current_stmt(VirtualMachine *vm, Node *stmt);
extern void __builtin_ast_switch_add_current_case(VirtualMachine *vm, Node *value, Node *body);
extern void __builtin_ast_switch_set_current_default(VirtualMachine *vm, Node *body);
extern int __builtin_ast_enum_count(VirtualMachine *vm, Type *enum_type);
extern EnumConstant *__builtin_ast_enum_at(VirtualMachine *vm, Type *enum_type, int index);
extern const char *__builtin_ast_enum_constant_name(EnumConstant *ec);
extern int __builtin_ast_enum_constant_value(EnumConstant *ec);
extern int __builtin_ast_global_count(VirtualMachine *vm);
extern Obj *__builtin_ast_global_at(VirtualMachine *vm, int index);
extern const char *__builtin_ast_obj_name(Obj *obj);
extern Type *__builtin_ast_obj_type(Obj *obj);
extern bool __builtin_ast_obj_is_function(Obj *obj);
extern bool __builtin_ast_obj_is_definition(Obj *obj);
extern bool __builtin_ast_obj_is_static(Obj *obj);
extern int __builtin_attr_target_kind(AttrTarget *target);
extern const char *__builtin_attr_target_name(AttrTarget *target);
extern Type *__builtin_attr_target_type(AttrTarget *target);
extern Obj *__builtin_attr_target_obj(AttrTarget *target);
extern Token *__builtin_attr_target_token(AttrTarget *target);
extern Type *__builtin_ast_make_pointer(VirtualMachine *vm, Type *base);
extern Type *__builtin_ast_make_array(VirtualMachine *vm, Type *base, int len);
extern Type *__builtin_ast_make_func_ptr_type(VirtualMachine *vm, Type *return_ty,
                                            Type **param_types, int nparams);
extern void __builtin_generate_sum(VirtualMachine *vm, Type *elem_ty);
extern void __builtin_generate_map(VirtualMachine *vm, Type *elem_ty);
extern void __builtin_generate_reduce(VirtualMachine *vm, Type *elem_ty);
extern void __builtin_generate_filter(VirtualMachine *vm, Type *elem_ty);

// Function generation
extern Obj *__builtin_ast_function(VirtualMachine *vm, const char *name, Type *return_type);
extern void __builtin_ast_function_add_param(VirtualMachine *vm, Obj *fn, const char *name,
                                    Type *type);
extern void __builtin_ast_function_set_body(VirtualMachine *vm, Obj *fn, Node *body);
extern void __builtin_ast_function_set_static(Obj *fn, bool is_static);
extern void __builtin_ast_function_set_inline(Obj *fn, bool is_inline);
extern void __builtin_ast_function_set_variadic(Obj *fn, bool is_variadic);
extern Node *__builtin_ast_publish(VirtualMachine *vm, Obj *obj, Token *tok);
extern Node *__builtin_ast_publish_type(VirtualMachine *vm, Type *ty, Token *tok);
extern Node *__builtin_ast_param_ref(VirtualMachine *vm, Obj *fn, const char *name);
extern void __builtin_emit_directive(VirtualMachine *vm, const char *line);

// Ticket #152: global variable generation
extern Obj  *__builtin_ast_global_var(VirtualMachine *vm, const char *name, Type *ty);
extern void  __builtin_ast_global_var_set_init_data(VirtualMachine *vm, Obj *var,
                                                const char *data, int len);
extern void  __builtin_ast_global_var_set_static(Obj *var, bool is_static);

// Ticket #148: function-building context (push/pop current_fn for Quote)
extern void __builtin_ast_push_fn(VirtualMachine *vm, Obj *fn);
extern void __builtin_ast_pop_fn(VirtualMachine *vm);
extern void __builtin_ast_push_block(VirtualMachine *vm, Node *block);
extern void __builtin_ast_pop_block(VirtualMachine *vm);
extern void __builtin_ast_push_struct(VirtualMachine *vm, Type *ty);
extern void __builtin_ast_pop_struct(VirtualMachine *vm);
extern void __builtin_ast_push_switch(VirtualMachine *vm, Node *switch_node);
extern void __builtin_ast_pop_switch(VirtualMachine *vm);
extern void __builtin_ast_push_enum(VirtualMachine *vm, Type *ty);
extern void __builtin_ast_pop_enum(VirtualMachine *vm);

// Ticket #58: AST dump
extern void __builtin_dump_tree(VirtualMachine *vm, Node *node);
extern const char *__builtin_dump_tree_to_string(VirtualMachine *vm, Node *node);
extern void __builtin_dump_ast_gen(VirtualMachine *vm, Node *node);
extern const char *__builtin_dump_ast_gen_to_string(VirtualMachine *vm, Node *node);

void cc_record_emit_source(VirtualMachine *vm, const char *source) {
    if (!vm || !source || !*source)
        return;
    EmitEvent *ev = arena_alloc(&vm->compiler.parser_arena, sizeof(*ev));
    memset(ev, 0, sizeof(*ev));
    ev->kind = CCCC_EMIT_SOURCE;
    ev->source = arena_strdup(vm, source);
    if (vm->compiler.emit_events_tail)
        vm->compiler.emit_events_tail->next = ev;
    else
        vm->compiler.emit_events_head = ev;
    vm->compiler.emit_events_tail = ev;
}

void cc_record_emit_object(VirtualMachine *vm, Obj *obj) {
    if (!vm || !obj || !obj->is_macro_generated)
        return;
    for (EmitEvent *ev = vm->compiler.emit_events_head; ev; ev = ev->next)
        if (ev->kind == CCCC_EMIT_OBJECT && ev->obj == obj)
            return;
    EmitEvent *ev = arena_alloc(&vm->compiler.parser_arena, sizeof(*ev));
    memset(ev, 0, sizeof(*ev));
    ev->kind = CCCC_EMIT_OBJECT;
    ev->obj = obj;
    if (vm->compiler.emit_events_tail)
        vm->compiler.emit_events_tail->next = ev;
    else
        vm->compiler.emit_events_head = ev;
    vm->compiler.emit_events_tail = ev;
}

// Ticket #78: source-located macro diagnostics
extern void __builtin_macro_error_at(VirtualMachine *vm, Node *node, const char *fmt, ...);
extern void __builtin_macro_warning_at(VirtualMachine *vm, Node *node, const char *fmt, ...);

// Previously unregistered statement builders (existing functions, now exposed)
extern Node *__builtin_ast_block(VirtualMachine *vm, Node **stmts, int count);
extern Node *__builtin_ast_expr_stmt(VirtualMachine *vm, Node *expr);

// Ticket #77: hygienic local variable injection
extern Node *__builtin_ast_local_var(VirtualMachine *vm, const char *name, Type *ty);
extern Node *__builtin_ast_local_var_unique(VirtualMachine *vm, Type *ty);

// Struct/union member introspection (previously unregistered)
extern int     __builtin_ast_struct_member_count(VirtualMachine *vm, Type *struct_type);
extern Member *__builtin_ast_struct_member_at(VirtualMachine *vm, Type *struct_type, int index);
extern Member *__builtin_ast_struct_member_find(VirtualMachine *vm, Type *struct_type,
                                              const char *name);
extern const char *__builtin_ast_member_name(Member *m);
extern Type    *__builtin_ast_member_type(Member *m);
extern int      __builtin_ast_member_offset(Member *m);
extern bool     __builtin_ast_member_is_bitfield(Member *m);
extern int      __builtin_ast_member_bitfield_width(Member *m);
extern int64_t  __builtin_ast_offsetof_chain(VirtualMachine *vm, Type *ty,
                                          const char **names, int n);

// Ticket #235: serialization
extern Node *__builtin_ast_serialize(VirtualMachine *vm, Type *ty, Node *expr, Node *buf);
extern Node *__builtin_ast_deserialize(VirtualMachine *vm, Type *ty, Node *buf);

// Ticket #235: enum <-> string conversion
extern Node *__builtin_ast_enum_to_string_switch(VirtualMachine *vm, Type *ty, Node *expr);
extern Node *__builtin_ast_enum_from_string_chain(VirtualMachine *vm, Type *ty, Node *expr);

// Ticket #51: new expression/statement builders
extern Node *__builtin_ast_assign(VirtualMachine *vm, Node *target, Node *value);
extern Node *__builtin_ast_member(VirtualMachine *vm, Node *obj, const char *name);
extern Node *__builtin_ast_funcall(VirtualMachine *vm, Node *callee, Node **args, int n);
extern Node *__builtin_ast_while(VirtualMachine *vm, Node *cond, Node *body);
extern Node *__builtin_ast_for(VirtualMachine *vm, Node *init, Node *cond, Node *inc, Node *body);
extern Node *__builtin_ast_do_while(VirtualMachine *vm, Node *body, Node *cond);

// Ticket #1: quasi-quoting; Ticket #172: list splice helper
extern Node *__builtin_quote(VirtualMachine *vm, const char *tmpl, ...);
extern Node *__builtin_quote_n(VirtualMachine *vm, const char *tmpl, Node **nodes, int count);
extern Node *__builtin_node_list(VirtualMachine *vm, Node **nodes, int count);

// Ticket #171: new expression builders
extern Node  *__builtin_ast_cond(VirtualMachine *vm, Node *cond, Node *then_expr, Node *else_expr);
extern Node  *__builtin_ast_null(VirtualMachine *vm);
extern Node  *__builtin_ast_sizeof_type(VirtualMachine *vm, Type *ty);
extern Node  *__builtin_ast_alignof_type(VirtualMachine *vm, Type *ty);
extern Node  *__builtin_ast_sizeof_expr(VirtualMachine *vm, Node *expr);
extern Node  *__builtin_ast_subscript(VirtualMachine *vm, Node *arr, Node *idx);
extern Node  *__builtin_ast_comma(VirtualMachine *vm, Node *lhs, Node *rhs);

// Ticket #171: qualified type builders
extern Type  *__builtin_ast_make_const(VirtualMachine *vm, Type *ty);
extern Type  *__builtin_ast_make_volatile(VirtualMachine *vm, Type *ty);

// Ticket #171: function prototype builder
extern Obj   *__builtin_ast_function_prototype(VirtualMachine *vm, const char *name,
                                            Type *return_type);

// Ticket #171: struct/union/enum/typedef type builders
extern Type  *__builtin_ast_make_struct(VirtualMachine *vm, const char *name);
extern Type  *__builtin_ast_make_union(VirtualMachine *vm, const char *name);
extern Type  *__builtin_ast_struct_add_field(VirtualMachine *vm, Type *ty, const char *name,
                                          Type *field_type);
extern Type  *__builtin_ast_struct_add_current_field(VirtualMachine *vm, const char *name,
                                                  Type *field_type);
extern Type  *__builtin_ast_make_enum(VirtualMachine *vm, const char *name);
extern void   __builtin_ast_enum_add_constant(VirtualMachine *vm, Type *ty, const char *name,
                                           int value);
extern void   __builtin_ast_enum_add_current_constant(VirtualMachine *vm, const char *name,
                                                   int value);
extern Type  *__builtin_ast_make_typedef(VirtualMachine *vm, const char *name, Type *underlying);

// Ticket #188: comptime variable access
extern int64_t __builtin_get_comptime_int(VirtualMachine *vm, const char *name);
extern double  __builtin_get_comptime_float(VirtualMachine *vm, const char *name);
extern Node   *__builtin_get_comptime_var(VirtualMachine *vm, const char *name);
extern Node   *__builtin_get_comptime_ptr(VirtualMachine *vm, const char *name);
extern Node   *__builtin_get_comptime_member(VirtualMachine *vm, const char *var_name,
                                         const char *field);

// Ticket #189: constexpr variable access
extern Node   *__builtin_get_constexpr_value(VirtualMachine *vm, const char *name);

// Ticket #296: initializer builders
extern Node   *__builtin_ast_compound_literal(VirtualMachine *vm, Type *ty, Node **inits, int n);
extern Node   *__builtin_ast_init_array(VirtualMachine *vm, Type *elem_ty, Node **elems, int n);
extern Node   *__builtin_ast_init_struct(VirtualMachine *vm, Type *ty, const char **fields,
                                     Node **values, int n);

// Ticket #277: Lisp-style single-macro expansion
extern Node   *__builtin_macroexpand_1(VirtualMachine *vm, Node *node);
extern Node   *__builtin_macroexpand(VirtualMachine *vm, Node *node);
extern int     __builtin_ast_vararg_count(VirtualMachine *vm);
extern Node   *__builtin_ast_vararg_at(VirtualMachine *vm, int index);
extern Node   **__builtin_ast_varargs_as_array(VirtualMachine *vm);
extern const char *__builtin_ast_vararg_str_at(VirtualMachine *vm, int index);

int __builtin_ast_vararg_count(VirtualMachine *vm) {
    return vm ? vm->compiler.macro_vararg_count : 0;
}

int VarargCount(void) {
    return __builtin_ast_vararg_count(__builtin_get_vm());
}

Node *__builtin_ast_vararg_at(VirtualMachine *vm, int index) {
    if (!vm)
        return NULL;
    if (vm->compiler.macro_vararg_string_mode)
        error_tok(vm, vm->compiler.macro_call_tok,
                  "VarargAt is only valid for inline AST macros");
    if (index < 0 || index >= vm->compiler.macro_vararg_count)
        error_tok(vm, vm->compiler.macro_call_tok,
                  "VarargAt index %d out of range (count %d)",
                  index, vm->compiler.macro_vararg_count);
    return vm->compiler.macro_vararg_nodes[index];
}

Node *VarargAt(int index) {
    return __builtin_ast_vararg_at(__builtin_get_vm(), index);
}

Node **__builtin_ast_varargs_as_array(VirtualMachine *vm) {
    if (!vm)
        return NULL;
    if (vm->compiler.macro_vararg_string_mode)
        error_tok(vm, vm->compiler.macro_call_tok,
                  "VarargAsArray is only valid for inline AST macros");
    if (vm->compiler.macro_vararg_count == 0)
        return NULL;
    return vm->compiler.macro_vararg_nodes;
}

Node **VarargAsArray(void) {
    return __builtin_ast_varargs_as_array(__builtin_get_vm());
}

const char *__builtin_ast_vararg_str_at(VirtualMachine *vm, int index) {
    if (!vm)
        return NULL;
    if (!vm->compiler.macro_vararg_string_mode)
        error_tok(vm, vm->compiler.macro_call_tok,
                  "VarargStrAt is only valid for global-generation string macros");
    if (index < 0 || index >= vm->compiler.macro_vararg_count)
        error_tok(vm, vm->compiler.macro_call_tok,
                  "VarargStrAt index %d out of range (count %d)",
                  index, vm->compiler.macro_vararg_count);
    return vm->compiler.macro_vararg_strs[index];
}

const char *VarargStrAt(int index) {
    return __builtin_ast_vararg_str_at(__builtin_get_vm(), index);
}

// Register reflection API functions as FFI
static void register_reflection_ffi(VirtualMachine *vm) {
    // VM accessor
    cc_register_cfunc(vm, "__builtin_get_vm", (void *)__builtin_get_vm, 0, 0);
    cc_register_cfunc(vm, "__builtin_gensym", (void *)__builtin_gensym, 2, 0);

    // Source location helpers
    cc_register_cfunc(vm, "__builtin_ast_current_token",
                      (void *)__builtin_ast_current_token, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_synthetic_token",
                      (void *)__builtin_ast_synthetic_token, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_token_from_node",
                      (void *)__builtin_ast_token_from_node, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_set_token",
                      (void *)__builtin_ast_set_token, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_copy_location",
                      (void *)__builtin_ast_copy_location, 2, 0);

    // Type lookup
    cc_register_cfunc(vm, "__builtin_ast_find_type", (void *)__builtin_ast_find_type, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_type_exists", (void *)__builtin_ast_type_exists, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_get_type", (void *)__builtin_ast_get_type, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_type_kind", (void *)__builtin_ast_type_kind, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_type_size", (void *)__builtin_ast_type_size, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_type_align", (void *)__builtin_ast_type_align, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_type_is_unsigned",
                      (void *)__builtin_ast_type_is_unsigned, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_type_is_const",
                      (void *)__builtin_ast_type_is_const, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_type_base", (void *)__builtin_ast_type_base, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_type_array_len",
                      (void *)__builtin_ast_type_array_len, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_type_return_type",
                      (void *)__builtin_ast_type_return_type, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_type_param_count",
                      (void *)__builtin_ast_type_param_count, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_type_param_at",
                      (void *)__builtin_ast_type_param_at, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_type_is_variadic",
                      (void *)__builtin_ast_type_is_variadic, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_type_name", (void *)__builtin_ast_type_name, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_type_c_name", (void *)__builtin_ast_type_c_name, 2, 0);

    // Type construction
    cc_register_cfunc(vm, "__builtin_ast_make_pointer", (void *)__builtin_ast_make_pointer, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_make_array", (void *)__builtin_ast_make_array, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_make_func_ptr_type", (void *)__builtin_ast_make_func_ptr_type, 4, 0);
    cc_register_cfunc(vm, "__builtin_generate_sum", (void *)__builtin_generate_sum, 2, 0);
    cc_register_cfunc(vm, "__builtin_generate_map", (void *)__builtin_generate_map, 2, 0);
    cc_register_cfunc(vm, "__builtin_generate_reduce", (void *)__builtin_generate_reduce, 2, 0);
    cc_register_cfunc(vm, "__builtin_generate_filter", (void *)__builtin_generate_filter, 2, 0);

    // Literal construction
    cc_register_cfunc(vm, "__builtin_ast_int_literal", (void *)__builtin_ast_int_literal, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_float_literal", (void *)__builtin_ast_float_literal, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_string_literal", (void *)__builtin_ast_string_literal, 2,
                      0);
    cc_register_cfunc(vm, "__builtin_ast_var_ref", (void *)__builtin_ast_var_ref, 2, 0);

    // Expression construction
    cc_register_cfunc(vm, "__builtin_ast_binary", (void *)__builtin_ast_binary, 4, 0);
    cc_register_cfunc(vm, "__builtin_ast_unary", (void *)__builtin_ast_unary, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_cast", (void *)__builtin_ast_cast, 3, 0);

    // Statement construction
    cc_register_cfunc(vm, "__builtin_ast_return", (void *)__builtin_ast_return, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_if", (void *)__builtin_ast_if, 4, 0);
    cc_register_cfunc(vm, "__builtin_ast_switch", (void *)__builtin_ast_switch, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_switch_add_case", (void *)__builtin_ast_switch_add_case, 4,
                      0);
    cc_register_cfunc(vm, "__builtin_ast_switch_set_default",
                      (void *)__builtin_ast_switch_set_default, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_switch_add_current_case",
                      (void *)__builtin_ast_switch_add_current_case, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_switch_set_current_default",
                      (void *)__builtin_ast_switch_set_current_default, 2, 0);

    // Ticket #58: AST dump
    cc_register_cfunc(vm, "__builtin_dump_tree",              (void *)__builtin_dump_tree,              2, 0);
    cc_register_cfunc(vm, "__builtin_dump_tree_to_string",    (void *)__builtin_dump_tree_to_string,    2, 0);
    cc_register_cfunc(vm, "__builtin_dump_ast_gen",           (void *)__builtin_dump_ast_gen,           2, 0);
    cc_register_cfunc(vm, "__builtin_dump_ast_gen_to_string", (void *)__builtin_dump_ast_gen_to_string, 2, 0);
    cc_register_cfunc(vm, "__builtin_emit_directive",
                      (void *)__builtin_emit_directive, 2, 0);

    // Ticket #78: source-located macro diagnostics (variadic)
    cc_register_variadic_cfunc(vm, "__builtin_macro_error_at",   (void *)__builtin_macro_error_at,   3, 0);
    cc_register_variadic_cfunc(vm, "__builtin_macro_warning_at", (void *)__builtin_macro_warning_at, 3, 0);

    // Previously unregistered statement builders
    cc_register_cfunc(vm, "__builtin_ast_block",     (void *)__builtin_ast_block,     3, 0);
    cc_register_cfunc(vm, "__builtin_ast_block_add_stmt",
                      (void *)__builtin_ast_block_add_stmt, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_block_add_current_stmt",
                      (void *)__builtin_ast_block_add_current_stmt, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_expr_stmt", (void *)__builtin_ast_expr_stmt, 2, 0);

    // Ticket #77: hygienic local variable injection
    cc_register_cfunc(vm, "__builtin_ast_local_var",        (void *)__builtin_ast_local_var,        3, 0);
    cc_register_cfunc(vm, "__builtin_ast_local_var_unique",  (void *)__builtin_ast_local_var_unique,  2, 0);

    // Ticket #51: new expression/statement builders
    cc_register_cfunc(vm, "__builtin_ast_assign",   (void *)__builtin_ast_assign,   3, 0);
    cc_register_cfunc(vm, "__builtin_ast_member",   (void *)__builtin_ast_member,   3, 0);

    // Struct/union member introspection (previously unregistered)
    cc_register_cfunc(vm, "__builtin_ast_struct_member_count",
                      (void *)__builtin_ast_struct_member_count, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_struct_member_at",
                      (void *)__builtin_ast_struct_member_at, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_struct_member_find",
                      (void *)__builtin_ast_struct_member_find, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_member_name",
                      (void *)__builtin_ast_member_name, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_member_type",
                      (void *)__builtin_ast_member_type, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_member_offset",
                      (void *)__builtin_ast_member_offset, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_member_is_bitfield",
                      (void *)__builtin_ast_member_is_bitfield, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_member_bitfield_width",
                      (void *)__builtin_ast_member_bitfield_width, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_offsetof_chain",
                      (void *)__builtin_ast_offsetof_chain, 4, 0);

    // Ticket #235: serialization
    cc_register_cfunc(vm, "__builtin_ast_serialize",
                      (void *)__builtin_ast_serialize, 4, 0);
    cc_register_cfunc(vm, "__builtin_ast_deserialize",
                      (void *)__builtin_ast_deserialize, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_enum_to_string_switch",
                      (void *)__builtin_ast_enum_to_string_switch, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_enum_from_string_chain",
                      (void *)__builtin_ast_enum_from_string_chain, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_funcall",  (void *)__builtin_ast_funcall,  4, 0);
    cc_register_cfunc(vm, "__builtin_ast_while",    (void *)__builtin_ast_while,    3, 0);
    cc_register_cfunc(vm, "__builtin_ast_for",      (void *)__builtin_ast_for,      5, 0);
    cc_register_cfunc(vm, "__builtin_ast_do_while", (void *)__builtin_ast_do_while, 3, 0);

    // Enum reflection
    cc_register_cfunc(vm, "__builtin_ast_enum_count", (void *)__builtin_ast_enum_count, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_enum_at", (void *)__builtin_ast_enum_at, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_enum_constant_name",
                      (void *)__builtin_ast_enum_constant_name, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_enum_constant_value",
                      (void *)__builtin_ast_enum_constant_value, 1, 0);

    // Global/object and custom-attribute target introspection
    cc_register_cfunc(vm, "__builtin_ast_global_count",
                      (void *)__builtin_ast_global_count, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_global_at",
                      (void *)__builtin_ast_global_at, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_obj_name",
                      (void *)__builtin_ast_obj_name, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_obj_type",
                      (void *)__builtin_ast_obj_type, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_obj_is_function",
                      (void *)__builtin_ast_obj_is_function, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_obj_is_definition",
                      (void *)__builtin_ast_obj_is_definition, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_obj_is_static",
                      (void *)__builtin_ast_obj_is_static, 1, 0);
    cc_register_cfunc(vm, "__builtin_attr_target_kind",
                      (void *)__builtin_attr_target_kind, 1, 0);
    cc_register_cfunc(vm, "__builtin_attr_target_name",
                      (void *)__builtin_attr_target_name, 1, 0);
    cc_register_cfunc(vm, "__builtin_attr_target_type",
                      (void *)__builtin_attr_target_type, 1, 0);
    cc_register_cfunc(vm, "__builtin_attr_target_obj",
                      (void *)__builtin_attr_target_obj, 1, 0);
    cc_register_cfunc(vm, "__builtin_attr_target_token",
                      (void *)__builtin_attr_target_token, 1, 0);

    // Function generation
    cc_register_cfunc(vm, "__builtin_ast_function", (void *)__builtin_ast_function, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_function_add_param",
                      (void *)__builtin_ast_function_add_param, 4, 0);
    cc_register_cfunc(vm, "__builtin_ast_function_set_body",
                      (void *)__builtin_ast_function_set_body, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_function_set_static",
                      (void *)__builtin_ast_function_set_static, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_function_set_inline",
                      (void *)__builtin_ast_function_set_inline, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_function_set_variadic",
                      (void *)__builtin_ast_function_set_variadic, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_publish",
                      (void *)__builtin_ast_publish, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_publish_type",
                      (void *)__builtin_ast_publish_type, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_param_ref", (void *)__builtin_ast_param_ref, 3, 0);

    // Ticket #152: global variable generation
    cc_register_cfunc(vm, "__builtin_ast_global_var",
                      (void *)__builtin_ast_global_var, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_global_var_set_init_data",
                      (void *)__builtin_ast_global_var_set_init_data, 4, 0);
    cc_register_cfunc(vm, "__builtin_ast_global_var_set_static",
                      (void *)__builtin_ast_global_var_set_static, 2, 0);

    // Ticket #148: function-building context
    cc_register_cfunc(vm, "__builtin_ast_push_fn",
                      (void *)__builtin_ast_push_fn, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_pop_fn",
                      (void *)__builtin_ast_pop_fn, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_push_block",
                      (void *)__builtin_ast_push_block, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_pop_block",
                      (void *)__builtin_ast_pop_block, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_push_struct",
                      (void *)__builtin_ast_push_struct, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_pop_struct",
                      (void *)__builtin_ast_pop_struct, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_push_switch",
                      (void *)__builtin_ast_push_switch, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_pop_switch",
                      (void *)__builtin_ast_pop_switch, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_push_enum",
                      (void *)__builtin_ast_push_enum, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_pop_enum",
                      (void *)__builtin_ast_pop_enum, 1, 0);

    // Ticket #1: quasi-quoting
    cc_register_variadic_cfunc(vm, "__builtin_quote",      (void *)__builtin_quote,      2, 0);
    cc_register_cfunc(vm,          "__builtin_quote_n",    (void *)__builtin_quote_n,    4, 0);

    // Ticket #172: list splice helper
    cc_register_cfunc(vm,          "__builtin_node_list",  (void *)__builtin_node_list,  3, 0);

    // Ticket #171: new expression builders
    cc_register_cfunc(vm, "__builtin_ast_cond",         (void *)__builtin_ast_cond,         4, 0);
    cc_register_cfunc(vm, "__builtin_ast_null",         (void *)__builtin_ast_null,         1, 0);
    cc_register_cfunc(vm, "__builtin_ast_sizeof_type",  (void *)__builtin_ast_sizeof_type,  2, 0);
    cc_register_cfunc(vm, "__builtin_ast_alignof_type", (void *)__builtin_ast_alignof_type, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_sizeof_expr",  (void *)__builtin_ast_sizeof_expr,  2, 0);
    cc_register_cfunc(vm, "__builtin_ast_subscript",    (void *)__builtin_ast_subscript,    3, 0);
    cc_register_cfunc(vm, "__builtin_ast_comma",        (void *)__builtin_ast_comma,        3, 0);

    // Ticket #171: qualified type builders
    cc_register_cfunc(vm, "__builtin_ast_make_const",    (void *)__builtin_ast_make_const,    2, 0);
    cc_register_cfunc(vm, "__builtin_ast_make_volatile", (void *)__builtin_ast_make_volatile, 2, 0);

    // Ticket #171: function prototype builder
    cc_register_cfunc(vm, "__builtin_ast_function_prototype",
                      (void *)__builtin_ast_function_prototype, 3, 0);

    // Ticket #171: struct/union/enum/typedef type builders
    cc_register_cfunc(vm, "__builtin_ast_make_struct",
                      (void *)__builtin_ast_make_struct, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_make_union",
                      (void *)__builtin_ast_make_union, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_struct_add_field",
                      (void *)__builtin_ast_struct_add_field, 4, 0);
    cc_register_cfunc(vm, "__builtin_ast_struct_add_current_field",
                      (void *)__builtin_ast_struct_add_current_field, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_make_enum",
                      (void *)__builtin_ast_make_enum, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_enum_add_constant",
                      (void *)__builtin_ast_enum_add_constant, 4, 0);
    cc_register_cfunc(vm, "__builtin_ast_enum_add_current_constant",
                      (void *)__builtin_ast_enum_add_current_constant, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_make_typedef",
                      (void *)__builtin_ast_make_typedef, 3, 0);

    // Ticket #188: comptime variable access
    cc_register_cfunc(vm, "__builtin_get_comptime_int",
                      (void *)__builtin_get_comptime_int, 2, 0);
    cc_register_cfunc(vm, "__builtin_get_comptime_float",
                      (void *)__builtin_get_comptime_float, 2, 1); // returns double
    cc_register_cfunc(vm, "__builtin_get_comptime_var",
                      (void *)__builtin_get_comptime_var, 2, 0);
    cc_register_cfunc(vm, "__builtin_get_comptime_ptr",
                      (void *)__builtin_get_comptime_ptr, 2, 0);
    cc_register_cfunc(vm, "__builtin_get_comptime_member",
                      (void *)__builtin_get_comptime_member, 3, 0);

    // Ticket #189: constexpr variable access
    cc_register_cfunc(vm, "__builtin_get_constexpr_value",
                      (void *)__builtin_get_constexpr_value, 2, 0);

    // Ticket #296: initializer builders
    cc_register_cfunc(vm, "__builtin_ast_compound_literal",
                      (void *)__builtin_ast_compound_literal, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_init_array",
                      (void *)__builtin_ast_init_array, 3, 0);
    cc_register_cfunc(vm, "__builtin_ast_init_struct",
                      (void *)__builtin_ast_init_struct, 4, 0);

    // Ticket #277: Lisp-style macro expansion
    cc_register_cfunc(vm, "__builtin_macroexpand_1",
                      (void *)__builtin_macroexpand_1, 2, 0);
    cc_register_cfunc(vm, "__builtin_macroexpand",
                      (void *)__builtin_macroexpand, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_vararg_count",
                      (void *)__builtin_ast_vararg_count, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_vararg_at",
                      (void *)__builtin_ast_vararg_at, 2, 0);
    cc_register_cfunc(vm, "__builtin_ast_varargs_as_array",
                      (void *)__builtin_ast_varargs_as_array, 1, 0);
    cc_register_cfunc(vm, "__builtin_ast_vararg_str_at",
                      (void *)__builtin_ast_vararg_str_at, 2, 0);
    cc_register_cfunc(vm, "VarargCount",
                      (void *)VarargCount, 0, 0);
    cc_register_cfunc(vm, "VarargAt",
                      (void *)VarargAt, 1, 0);
    cc_register_cfunc(vm, "VarargAsArray",
                      (void *)VarargAsArray, 0, 0);
    cc_register_cfunc(vm, "VarargStrAt",
                      (void *)VarargStrAt, 1, 0);
}

static void init_vm_segments_for_macros(VirtualMachine *vm);

static Token *copy_macro_token(VirtualMachine *vm, Token *tok) {
    Token *copy = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    *copy = *tok;
    copy->next = NULL;
    copy->origin = tok;
    return copy;
}

static Token *new_macro_punct(VirtualMachine *vm, char *str, Token *tmpl) {
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

static Token *new_macro_eof(VirtualMachine *vm, Token *tmpl) {
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

// Synthesize a marker token consumed by preprocess2 to snapshot/restore
// vm->compiler.macros around a single comptime function body's tokens,
// isolating its #define/#undef from sibling comptime functions (#283).
static Token *new_macro_scope_marker(VirtualMachine *vm, TokenKind kind, Token *tmpl) {
    Token *tok = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    memset(tok, 0, sizeof(Token));
    tok->kind = kind;
    tok->loc = "";
    tok->len = 0;
    if (tmpl) {
        tok->file = tmpl->file;
        tok->filename = tmpl->filename;
        tok->line_no = tmpl->line_no;
        tok->col_no = tmpl->col_no;
    }
    return tok;
}

static Token *append_macro_prototype(VirtualMachine *vm, Token *cur, MacroFn *pm) {
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

static Token *append_macro_definition(VirtualMachine *vm, Token *cur, MacroFn *pm) {
    Token *last = pm->body_tokens;
    for (Token *tok = pm->body_tokens; tok && tok->kind != TK_EOF;
         tok = tok->next) {
        last = tok;
        cur = cur->next = copy_macro_token(vm, tok);
    }
    (void)last;
    return cur;
}

static Token *append_token_list(VirtualMachine *vm, Token *cur, Token *tokens) {
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
static Token *build_macro_context_tokens(VirtualMachine *vm, Token **input_tokens,
                                         int count) {
    Token head = {};
    Token *cur = &head;

    // Derive the main source file name from the first input token stream.
    // When --strict-comptime-includes is set, only declarations whose tokens
    // originate from this file are forwarded to the comptime pass.
    char *main_filename = NULL;
    if (vm->compiler.strict_comptime_includes && count > 0 && input_tokens[0]) {
        Token *t = input_tokens[0];
        while (t && t->kind == TK_EOF)
            t = t->next;
        if (t)
            main_filename = t->filename;
    }

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
                            bool skip = vm->compiler.strict_comptime_includes &&
                                         main_filename &&
                                         start->filename &&
                                         strcmp(start->filename, main_filename) != 0;
                            if (!skip) {
                                for (Token *t = start; t != tok->next &&
                                     t && t->kind != TK_EOF; t = t->next)
                                    cur = cur->next = copy_macro_token(vm, t);
                            }
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

typedef enum {
    COMPTIME_AGG_CAST_NONE,
    COMPTIME_AGG_CAST_TAGGED,
    COMPTIME_AGG_CAST_TYPEDEF,
    COMPTIME_AGG_CAST_TYPEOF,
} ComptimeAggregateCastKind;

typedef struct {
    ComptimeAggregateCastKind kind;
    Token *kw;   // "struct" or "union" for tagged aggregates
    Token *name; // tag or typedef name
} ComptimeAggregateCast;

static bool token_matches_name(Token *tok, const char *name) {
    return tok && tok->kind == TK_IDENT &&
           strlen(name) == (size_t)tok->len &&
           strncmp(tok->loc, name, tok->len) == 0;
}

// Determine which compound-literal cast can initialize a comptime aggregate.
// Handles:
//   - tagged:    struct Dims { ... } dims -> (struct Dims){ ... }
//   - typedef'd: Dims dims                 -> (Dims){ ... }
//   - anonymous: struct { ... } dims       -> (typeof(dims)){ ... }
static ComptimeAggregateCast comptime_aggregate_cast(ComptimeVar *cv) {
    ComptimeAggregateCast cast = { COMPTIME_AGG_CAST_NONE, NULL, NULL };
    Token *typedef_name = NULL;
    int brace_depth = 0, bracket_depth = 0, paren_depth = 0;

    for (Token *t = cv->decl_tokens; t && t->kind != TK_EOF; t = t->next) {
        if (equal(t, "{")) brace_depth++;
        else if (equal(t, "}")) brace_depth--;
        else if (equal(t, "[")) bracket_depth++;
        else if (equal(t, "]")) bracket_depth--;
        else if (equal(t, "(")) paren_depth++;
        else if (equal(t, ")")) paren_depth--;

        if (brace_depth != 0 || bracket_depth != 0 || paren_depth != 0)
            continue;
        if (equal(t, "=") || equal(t, ";"))
            break;

        if (equal(t, "struct") || equal(t, "union")) {
            Token *next = t->next;
            if (next && next->kind == TK_IDENT) {
                cast.kind = COMPTIME_AGG_CAST_TAGGED;
                cast.kw = t;
                cast.name = next;
                return cast;
            }
            cast.kind = COMPTIME_AGG_CAST_TYPEOF;
            return cast;
        }

        if (t->kind == TK_IDENT && !token_matches_name(t, cv->name) &&
            !typedef_name)
            typedef_name = t;
    }

    if (typedef_name) {
        cast.kind = COMPTIME_AGG_CAST_TYPEDEF;
        cast.name = typedef_name;
    }
    return cast;
}

// True if this comptime var's initializer should be evaluated via
// __builtin_comptime_init (ticket #191/#192/#193) rather than the constant init_data
// path. Both build_combined_macro_tokens and build_comptime_init_fn_tokens
// call this so they agree on which vars are routed through the init fn.
//
// Scalar initializers (= <non-brace-expr>): always routed.
// Aggregate initializers (= { ... }): routed iff a compound-literal cast can
// be synthesized from a tag, typedef name, or typeof(var).
static bool comptime_var_uses_init_fn(ComptimeVar *cv) {
    Token *eq = find_top_level_eq(cv->decl_tokens);
    if (!eq)
        return false; // No initializer at all.
    if (!eq->next || !equal(eq->next, "{"))
        return true;  // Scalar expression init.
    // Aggregate init: routable only if a cast form can be synthesized.
    return comptime_aggregate_cast(cv).kind != COMPTIME_AGG_CAST_NONE;
}

// Inject decl_tokens up to (not including) eq_tok, then emit ';'.
// Produces a declaration without its initializer, e.g. "int buf_size ;"
static Token *append_decl_stripped(VirtualMachine *vm, Token *cur, Token *decl_tokens,
                                   Token *eq_tok) {
    for (Token *t = decl_tokens; t && t->kind != TK_EOF && t != eq_tok;
         t = t->next)
        cur = cur->next = copy_macro_token(vm, t);
    cur = cur->next = new_macro_punct(vm, ";", eq_tok);
    return cur;
}

// Build a __builtin_comptime_init function that assigns each comptime var's
// initializer expression in source order. Handles:
//   - scalar expression inits:  name = expr ;
//   - aggregate inits:          name = (<aggregate type>){ ... } ;
// Returns a token list via tokenize_string, or NULL when there are no vars
// routed through the init fn.
static Token *build_comptime_init_fn_tokens(VirtualMachine *vm,
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

    p += snprintf(p, end - p, "void __builtin_comptime_init(void){\n");

    for (int i = 0; i < count; i++) {
        ComptimeVar *cv = vars[i];
        if (!comptime_var_uses_init_fn(cv)) continue;

        Token *eq = find_top_level_eq(cv->decl_tokens);
        if (!eq) continue; // Should not happen if predicate is true, but be safe.

        if (eq->next && equal(eq->next, "{")) {
            // Aggregate init: emit  name = (<aggregate type>){ ... } ;
            ComptimeAggregateCast cast = comptime_aggregate_cast(cv);
            if (cast.kind == COMPTIME_AGG_CAST_TAGGED) {
                p += snprintf(p, end - p, "%s=(%.*s %.*s)",
                              cv->name,
                              cast.kw->len, cast.kw->loc,
                              cast.name->len, cast.name->loc);
            } else if (cast.kind == COMPTIME_AGG_CAST_TYPEDEF) {
                p += snprintf(p, end - p, "%s=(%.*s)",
                              cv->name, cast.name->len, cast.name->loc);
            } else if (cast.kind == COMPTIME_AGG_CAST_TYPEOF) {
                p += snprintf(p, end - p, "%s=(typeof(%s))",
                              cv->name, cv->name);
            } else {
                continue;
            }

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

static Token *implicit_reflection_tokens(VirtualMachine *vm) {
    char *header = get_std_header("reflection.h");
    if (!header)
        error("could not load embedded reflection.h");

    // Sanity-check that the embedded string wasn't truncated (e.g. by a
    // C array that was too small in std.c after reflection.h grew).
    // A truncated string silently drops the closing #endif, producing the
    // cryptic "unterminated conditional directive" error.  Run `make stdlib`
    // to re-embed the header after editing it.
    if (!strstr(header, "#endif // CCCC_REFLECTION_H"))
        error("embedded reflection.h appears truncated — run `make stdlib` to regenerate src/std.c");

    // The user's translation unit may already have included stdbool.h,
    // stddef.h, or stdint.h. Temporarily clear those guards so reflection.h's
    // private macro API is processed completely in the macro compilation
    // scope. We restore the guards afterwards so subsequent compilation phases
    // are unaffected.
    static const char *guards[] = {
        "CCCC_REFLECTION_H", "__STDBOOL_H", "__STDDEF_H", "__STDINT_H", "__STRING_H", NULL
    };
    void *saved_guards[5] = {};
    for (int i = 0; guards[i]; i++) {
        saved_guards[i] = hashmap_get(&vm->compiler.macros, (char *)guards[i]);
        if (saved_guards[i])
            hashmap_delete(&vm->compiler.macros, (char *)guards[i]);
    }

    // Preprocessing reflection.h can trigger lookahead declaration-parsing
    // (e.g. while scanning @macro bodies for locals) over expanded VM/$...
    // tokens, which spuriously warns about CCCC's own internal API surface.
    // Suppress all warnings/werrors for the duration of this internal
    // preprocess pass; restore afterwards so the user's TU is unaffected.
    uint64_t saved_warnings = vm->compiler.warnings;
    uint64_t saved_werror = vm->compiler.warning_errors;
    vm->compiler.warnings = 0;
    vm->compiler.warning_errors = 0;

    Token *tokens = tokenize_string(vm, "<implicit-reflection.h>", header);
    Token *result = preprocess(vm, tokens);

    vm->compiler.warnings = saved_warnings;
    vm->compiler.warning_errors = saved_werror;

    for (int i = 0; guards[i]; i++)
        if (saved_guards[i])
            hashmap_put(&vm->compiler.macros, (char *)guards[i], saved_guards[i]);

    return result;
}

// Ticket #235: idempotently preprocess reflection.h so that its built-in
// @macro(attribute(...)) handlers (e.g. __builtin_attr_serialize) are registered
// into vm->compiler.macro_fns before the first attribute-dispatch lookup
// (find_attribute_macro / run_custom_attrs in parse.c). Without this, a
// translation unit with no @macro definitions of its own never triggers
// implicit_reflection_tokens() until compile_macro_program() — too late for
// the very first @serialize/@deserialize/etc. dispatch to find a handler.
// Safe to call mid-parse: implicit_reflection_tokens only tokenizes and
// preprocesses reflection.h and temporarily toggles include-guard macros.
void ensure_reflection_attrs_registered(VirtualMachine *vm) {
    if (vm->compiler.reflection_attrs_registered)
        return;
    vm->compiler.reflection_attrs_registered = true;
    implicit_reflection_tokens(vm);
    __builtin_ensure_string_h_decls(vm);
}

static Token *build_combined_macro_tokens(VirtualMachine *vm, Token *reflection_tokens,
                                          MacroFn **macros, int count) {
    Token head = {};
    Token *cur = &head;

    cur = append_token_list(vm, cur, reflection_tokens);

    // Inject routed comptime directives so they are processed by the comptime
    // preprocessing pass (#196/#368).
    for (int i = 0; i < vm->compiler.comptime_pending_includes.len; i++) {
        char *src = arena_format(vm, "%s\n",
                                 vm->compiler.comptime_pending_includes.data[i]);
        Token *inc_toks = tokenize_string(vm, "<comptime-include>", src);
        cur = append_token_list(vm, cur, inc_toks);
    }

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
    // Vars routed through __builtin_comptime_init (ticket #191/#192) have their
    // initializer stripped so the parser never sees a non-constant expression
    // as a global initializer (which would hard-error via eval2). The stripped
    // var is declared as a zero-initialized global; __builtin_comptime_init fills
    // it in at VM run time.
    // Uninitialised vars, and aggregate vars whose initializer is constant or
    // whose tag cannot be synthesized, are injected as-is (constant path).
    for (int i = 0; i < nv; i++) {
        ComptimeVar *cv = vars[i];
        if (comptime_var_uses_init_fn(cv)) {
            // Strip initializer; __builtin_comptime_init will assign it.
            Token *eq = find_top_level_eq(cv->decl_tokens);
            cur = append_decl_stripped(vm, cur, cv->decl_tokens, eq);
        } else {
            cur = append_token_list(vm, cur, cv->decl_tokens);
        }
    }

    for (int i = 0; i < count; i++)
        cur = append_macro_prototype(vm, cur, macros[i]);
    for (int i = 0; i < count; i++) {
        if (!vm->compiler.allow_comptime_pp_bleed)
            cur = cur->next = new_macro_scope_marker(vm, TK_MACRO_SCOPE_PUSH, macros[i]->body_tokens);
        cur = append_macro_definition(vm, cur, macros[i]);
        if (!vm->compiler.allow_comptime_pp_bleed)
            cur = cur->next = new_macro_scope_marker(vm, TK_MACRO_SCOPE_POP, macros[i]->body_tokens);
    }

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

static Obj *find_macro_obj(Obj *prog, const char *name) {
    size_t len = strlen(name);
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->name && strlen(obj->name) == len &&
            strncmp(obj->name, name, len) == 0)
            return obj;
    }
    return NULL;
}

// Read a scalar value from the macro VM's data segment.
// Valid after init_macro_globals has allocated storage; the value may have
// been written by a constant-initializer memcpy, by __builtin_comptime_init, or
// left as zero (no initializer).
static bool read_comptime_scalar(VirtualMachine *vm, Obj *obj, bool *is_float_out,
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

static Obj *make_comptime_shadow_obj(VirtualMachine *vm, Obj *src) {
    if (!vm || !src || !src->ty || src->ty->size <= 0)
        return NULL;

    Obj *dst = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
    memset(dst, 0, sizeof(Obj));
    dst->name = arena_format(vm, ".L.comptime.%d",
                             vm->compiler.unique_name_counter++);
    dst->display_name = dst->name;
    dst->ty = src->ty;
    dst->align = src->ty->align;
    dst->tok = src->tok;
    dst->is_static = true;
    dst->is_definition = true;
    dst->is_macro_generated = true;
    dst->init_data = arena_alloc(&vm->compiler.parser_arena, src->ty->size);
    memcpy(dst->init_data, vm->data_seg + src->offset, src->ty->size);
    return dst;
}

static void link_comptime_shadow_objs(VirtualMachine *vm) {
    for (ComptimeVar *cv = vm->compiler.comptime_vars; cv; cv = cv->next) {
        Obj *obj = cv->ptr_obj;
        if (!obj || obj->next)
            continue;
        obj->next = vm->compiler.globals;
        vm->compiler.globals = obj;
    }
}

// Execute the synthesized __builtin_comptime_init function (if present) to
// evaluate scalar comptime variable initializers that call comptime
// functions. Must be called after gen_function + patch_macro_call_addresses
// so all bytecode and call targets are resolved.
static void run_comptime_var_initializers(VirtualMachine *vm, Obj *macro_prog) {
    Obj *init_fn = find_macro_function(macro_prog, "__builtin_comptime_init");
    if (!init_fn)
        return;

    if (vm->debug_vm)
        printf("Running __builtin_comptime_init for comptime variable initializers...\n");

    __builtin_current_vm = vm;

    Pc      saved_pc   = vm->pc;
    long long *saved_sp   = vm->sp;
    long long *saved_bp   = vm->bp;
    long long  saved_regs[NUM_REGS];
    memcpy(saved_regs, vm->regs, sizeof(saved_regs));
    Obj *saved_current_fn = vm->compiler.current_fn;

    vm->sp = vm->initial_sp;
    vm->bp = vm->initial_bp;
    *(--vm->sp) = 0; // sentinel return address

    vm->pc = (Pc)init_fn->code_addr;

    int saved_debug = vm->debug_vm;
    vm->debug_vm = 0;
    int eval_rc = vm_eval(vm);
    vm->debug_vm = saved_debug;

    __builtin_current_vm = NULL;

    vm->pc            = saved_pc;
    vm->sp            = saved_sp;
    vm->bp            = saved_bp;
    memcpy(vm->regs, saved_regs, sizeof(saved_regs));
    vm->compiler.current_fn = saved_current_fn;

    if (eval_rc == CCCC_HOST_SIGNAL_RC)
        error_tok(vm, init_fn->tok,
                  "compile-time execution terminated by host signal %d",
                  vm->dbg.host_fault_signal);

    if (vm->debug_vm)
        printf("__builtin_comptime_init completed.\n");
}

static void evaluate_comptime_vars(VirtualMachine *vm, Obj *macro_prog) {
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
        cv->ptr_obj = make_comptime_shadow_obj(vm, obj);
        if (kind == TY_STRUCT || kind == TY_UNION) {
            // Routed vars: __builtin_comptime_init wrote the bytes
            // into the data segment, so init_data is NULL — that is expected.
            // Non-routed vars (constant path): init_data must be present.
            if (!obj->init_data && !comptime_var_uses_init_fn(cv)) {
                fprintf(stderr,
                        "Warning: comptime struct/union var '%s' has a "
                        "non-constant initializer that could not be routed "
                        "through __builtin_comptime_init\n",
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
            // __builtin_comptime_init (ticket #191 non-constant path), or is zero
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

static void init_macro_globals(VirtualMachine *vm, Obj *macro_prog) {
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

        vm->data_ptr += var->ty->size;
    }
}

// Apply relocations for macro global initializers that reference other
// globals or comptime functions. Must run after gen_function has assigned
// code_addr to every macro function (Step 2), since function-pointer table
// entries resolve to text-segment addresses. Patches vm->data_seg directly
// and never touches vm->compiler.data_relocs — these relocations are
// VM-internal and must not pollute the runtime bytecode relocation table.
static void apply_macro_global_relocations(VirtualMachine *vm, Obj *macro_prog) {
    for (Obj *var = macro_prog; var; var = var->next) {
        if (var->is_function)
            continue;

        for (Relocation *rel = var->rel; rel; rel = rel->next) {
            if (!rel->label || !*rel->label)
                error("invalid macro function global relocation");

            Obj *target = find_macro_obj(macro_prog, *rel->label);
            if (!target)
                error("undefined macro function global relocation target: %s",
                      *rel->label);

            long long data_offset = var->offset + rel->offset;
            if (target->is_function) {
                if (!target->body)
                    error("unsupported macro relocation to undefined function: %s",
                          target->name);
                *(long long *)(vm->data_seg + data_offset) =
                    cc_pc_to_byte_offset((Pc)target->code_addr) + rel->addend;
            } else {
                *(long long *)(vm->data_seg + data_offset) =
                    (long long)(vm->data_seg + target->offset + rel->addend);
            }
        }
    }
}

static void patch_macro_call_addresses(VirtualMachine *vm, Obj *macro_prog) {
    for (int i = 0; i < vm->compiler.num_call_patches; i++) {
        Obj *fn = vm->compiler.call_patches[i].function;
        Pc loc = vm->compiler.call_patches[i].location;
        Obj *fn_def = find_macro_function(macro_prog, fn->name);
        if (!fn_def)
            error("undefined function in macro bytecode: %s", fn->name);
        vm->text_seg[loc] = (Pc)fn_def->code_addr;
    }

    for (int i = 0; i < vm->compiler.num_func_addr_patches; i++) {
        Obj *fn = vm->compiler.func_addr_patches[i].function;
        Pc loc = vm->compiler.func_addr_patches[i].location;
        Obj *fn_def = find_macro_function(macro_prog, fn->name);
        if (!fn_def)
            error("undefined function address in macro bytecode: %s",
                  fn->name);
        cc_write_i64_at(vm, loc, cc_pc_to_byte_offset((Pc)fn_def->code_addr));
    }
}

// Compile all macro functions and comptime helpers as one compile-time program so
// macro bytecode can make ordinary function calls across the whole set.
static bool compile_macro_program(VirtualMachine *vm) {
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
    // Snapshot the preprocessor macro table so that #define directives emitted
    // by reflection.h, comptime-only includes, or comptime function bodies do
    // not persist into the runtime translation unit after this pass completes.
    // Main-file defines remain visible inside comptime bodies because the
    // snapshot is taken after the main preprocessing pass has run (ticket #283).
    vm->compiler.macro_snapshot_backup = hashmap_snapshot(&vm->compiler.macros);
    vm->compiler.has_macro_snapshot = true;
    HashMap saved_macros = vm->compiler.macro_snapshot_backup;

    vm->compiler.in_macro_mode = true;
    vm->compiler.locals = NULL;
    vm->compiler.globals = NULL;
    vm->compiler.num_call_patches = 0;
    vm->compiler.num_func_addr_patches = 0;

    Token *reflection_tokens = implicit_reflection_tokens(vm);
    Token *tokens =
        build_combined_macro_tokens(vm, reflection_tokens, macros, count);

    // Re-stamping during this preprocess pass would otherwise apply the
    // user TU's -W flags to reflection.h's internal implementation tokens
    // (e.g. VM expansions), producing warnings the user can't fix. Hard
    // errors (error_tok) are unaffected since they aren't gated by
    // vm->compiler.warnings.
    uint64_t saved_warnings = vm->compiler.warnings;
    uint64_t saved_werror = vm->compiler.warning_errors;
    vm->compiler.warnings = 0;
    vm->compiler.warning_errors = 0;
    tokens = preprocess(vm, tokens);
    Obj *macro_prog = parse(vm, tokens);
    vm->compiler.warnings = saved_warnings;
    vm->compiler.warning_errors = saved_werror;
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
        vm->compiler.has_macro_snapshot = false;
        hashmap_restore(&vm->compiler.macros, saved_macros);
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
            for (Scope *sc = vm->compiler.scope; sc != saved_scope; sc = sc->next) {
                hashmap_deinit_borrowed(&sc->var_map);
                hashmap_deinit_borrowed(&sc->tag_map);
            }
            vm->compiler.scope = saved_scope;
            vm->compiler.in_macro_mode = false;
            vm->compiler.num_call_patches = saved_num_call_patches;
            vm->compiler.num_func_addr_patches = saved_num_func_addr_patches;
            vm->compiler.has_macro_snapshot = false;
        hashmap_restore(&vm->compiler.macros, saved_macros);
            return false;
        }
        macros[i]->compiled_fn = func;
        macros[i]->is_compiled = true;
    }

    // Step 1: allocate data segment storage for all globals and memcpy any
    //         constant initializer bytes (init_data path).
    init_macro_globals(vm, macro_prog);

    // Step 2: generate bytecode for all functions, including the synthesized
    //         __builtin_comptime_init helper produced by build_combined_macro_tokens.
    for (Obj *fn = macro_prog; fn; fn = fn->next) {
        if (fn->is_function && fn->body)
            gen_function(vm, fn);
    }

    // Step 3: apply global relocations now that every macro function has a
    //         code_addr, so static initializer tables can reference comptime
    //         function addresses (ticket #309).
    apply_macro_global_relocations(vm, macro_prog);

    // Step 4: patch call addresses so __builtin_comptime_init can call comptime fns.
    patch_macro_call_addresses(vm, macro_prog);

    // Step 5: run __builtin_comptime_init to evaluate scalar comptime var
    //         initializers (ticket #191). This writes results into the data
    //         segment via normal VM store instructions.
    run_comptime_var_initializers(vm, macro_prog);

    // Step 6: read comptime var values out of the data segment.
    evaluate_comptime_vars(vm, macro_prog);

    vm->compiler.locals = saved_locals;
    vm->compiler.current_fn = saved_current_fn;
    vm->compiler.globals = saved_globals;
    vm->compiler.scope = saved_scope;
    vm->compiler.in_macro_mode = false;
    vm->compiler.num_call_patches = saved_num_call_patches;
    vm->compiler.num_func_addr_patches = saved_num_func_addr_patches;
    vm->compiler.has_macro_snapshot = false;
    hashmap_restore(&vm->compiler.macros, saved_macros);
    link_comptime_shadow_objs(vm);

    if (vm->debug_vm) {
        for (int i = 0; i < count; i++) {
            printf("Compiled compile-time function '%s' at code address %lld\n",
                   macros[i]->name, macros[i]->compiled_fn->code_addr);
        }
    }

    return true;
}

// Compile all macro functions and comptime helpers (idempotent)
static void compile_all_macros(VirtualMachine *vm) {
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

static void setup_macro_call_slots(VirtualMachine *vm, long long *fixed_args,
                                   int fixed_count) {
    for (int i = fixed_count - 1; i >= 8; i--)
        *(--vm->sp) = fixed_args[i];
    for (int i = 0; i < fixed_count && i < 8; i++)
        vm->regs[REG_A0 + i] = fixed_args[i];
}

// Execute a macro function and return the generated AST node
static Node *execute_macro_fn(VirtualMachine *vm, MacroFn *pm, Token *call_tok,
                              Node *args, int arg_count,
                              long long *fixed_arg_values,
                              int fixed_arg_count) {
    if (!pm || !pm->is_compiled || !pm->compiled_fn)
        return NULL;

    if (pm->is_variadic && arg_count < pm->fixed_param_count)
        error_tok(vm, call_tok,
                  "macro '%s' called with %d arguments; expected at least %d",
                  pm->name, arg_count, pm->fixed_param_count);

    if (vm->debug_vm)
        printf("Executing macro function '%s' with %d args...\n", pm->name,
               arg_count);

    // Set global VM pointer for __builtin_get_vm()
    __builtin_current_vm = vm;

    // Save VM execution state (including current_fn so a macro that calls
    // __builtin_ast_push_fn without a matching pop cannot leak context).
    Pc saved_pc = vm->pc;
    long long *saved_sp = vm->sp;
    long long *saved_bp = vm->bp;
    long long saved_regs[NUM_REGS];
    memcpy(saved_regs, vm->regs, sizeof(saved_regs));
    Obj *saved_current_fn = vm->compiler.current_fn;
    Token *saved_macro_call_tok = vm->compiler.macro_call_tok;
    Node **saved_vararg_nodes = vm->compiler.macro_vararg_nodes;
    char **saved_vararg_strs = vm->compiler.macro_vararg_strs;
    int saved_vararg_count = vm->compiler.macro_vararg_count;
    bool saved_vararg_string_mode = vm->compiler.macro_vararg_string_mode;
    vm->compiler.macro_call_tok = call_tok;

    // Reset stack for macro execution
    vm->sp = vm->initial_sp;
    vm->bp = vm->initial_bp;

    // Pass fixed arguments using the VM calling convention.
    // Inline macro arguments are Node* pointers to the AST nodes. Global
    // generation callers preload char* fixed arguments and set string-mode
    // varargs before entering this helper.
    if (fixed_arg_values) {
        setup_macro_call_slots(vm, fixed_arg_values, fixed_arg_count);
    } else if (args) {
        int fixed_count = pm->is_variadic ? pm->fixed_param_count : arg_count;
        long long *fixed_args =
            fixed_count > 0 ? alloca((size_t)fixed_count * sizeof(long long))
                            : NULL;
        Node *arg = args;
        for (int i = 0; i < fixed_count; i++) {
            fixed_args[i] = (long long)arg;
            if (arg)
                arg = arg->next;
        }
        setup_macro_call_slots(vm, fixed_args, fixed_count);

        if (pm->is_variadic) {
            int var_count = arg_count - pm->fixed_param_count;
            Node **var_nodes = var_count > 0 ? alloca(var_count * sizeof(Node *)) : NULL;
            for (int i = 0; i < var_count; i++) {
                var_nodes[i] = arg;
                if (arg)
                    arg = arg->next;
            }
            vm->compiler.macro_vararg_nodes = var_nodes;
            vm->compiler.macro_vararg_strs = NULL;
            vm->compiler.macro_vararg_count = var_count;
            vm->compiler.macro_vararg_string_mode = false;
        } else {
            vm->compiler.macro_vararg_nodes = NULL;
            vm->compiler.macro_vararg_strs = NULL;
            vm->compiler.macro_vararg_count = 0;
            vm->compiler.macro_vararg_string_mode = false;
        }
    }

    // Push sentinel return address (0) so we can detect when function
    // returns
    *(--vm->sp) = 0;

    // Set PC to function entry point
    vm->pc = (Pc)pm->compiled_fn->code_addr;

    // Execute the macro function
    int saved_debug = vm->debug_vm;
    vm->debug_vm = 0; // Disable debug output during macro execution
    int eval_rc = vm_eval(vm);
    vm->debug_vm = saved_debug;

    // Get the returned Node* from regs[REG_A0]. Void macros do not produce a
    // meaningful VM return value; treat them as side-effect-only.
    Node *result = pm->is_void_macro ? NULL : (Node *)vm->regs[REG_A0];

    // Clear VM pointer
    __builtin_current_vm = NULL;

    // Restore VM execution state (current_fn last so it overrides any leaked
    // push_fn call that wasn't matched by a pop_fn inside the macro).
    vm->pc = saved_pc;
    vm->sp = saved_sp;
    vm->bp = saved_bp;
    memcpy(vm->regs, saved_regs, sizeof(saved_regs));
    vm->compiler.current_fn = saved_current_fn;
    vm->compiler.macro_call_tok = saved_macro_call_tok;
    vm->compiler.macro_vararg_nodes = saved_vararg_nodes;
    vm->compiler.macro_vararg_strs = saved_vararg_strs;
    vm->compiler.macro_vararg_count = saved_vararg_count;
    vm->compiler.macro_vararg_string_mode = saved_vararg_string_mode;

    if (eval_rc == CCCC_HOST_SIGNAL_RC)
        error_tok(vm, call_tok,
                  "compile-time macro execution terminated by host signal %d",
                  vm->dbg.host_fault_signal);

    if (vm->debug_vm && result)
        printf("Macro function '%s' returned node of kind %d\n", pm->name,
               result->kind);

    return result;
}

void cc_execute_attribute_macro(VirtualMachine *vm, MacroFn *pm, Token *tok,
                                AttrTarget *target, Node *args,
                                int arg_count) {
    if (!vm || !pm || !target)
        return;
    if (!pm->is_attribute_handler) {
        error_tok(vm, tok, "macro '%s' is not a custom attribute handler",
                  pm->name);
        return;
    }

    init_vm_segments_for_macros(vm);
    compile_all_macros(vm);

    if (!pm->is_compiled) {
        error_tok(vm, tok, "attribute macro '%s' failed to compile",
                  pm->name);
        return;
    }

    int total_args = arg_count + 1;
    if (!pm->is_variadic && pm->fixed_param_count != total_args)
        error_tok(vm, tok,
                  "attribute macro '%s' called with %d arguments; expected %d",
                  pm->name, total_args, pm->fixed_param_count);
    if (pm->is_variadic && total_args < pm->fixed_param_count)
        error_tok(vm, tok,
                  "attribute macro '%s' called with %d arguments; expected at least %d",
                  pm->name, total_args, pm->fixed_param_count);

    int fixed_count = pm->is_variadic ? pm->fixed_param_count : total_args;
    long long *fixed_values =
        fixed_count > 0 ? alloca((size_t)fixed_count * sizeof(long long)) : NULL;
    if (fixed_count > 0)
        fixed_values[0] = (long long)target;

    Node *arg = args;
    for (int i = 1; i < fixed_count; i++) {
        fixed_values[i] = (long long)arg;
        if (arg)
            arg = arg->next;
    }

    Node **saved_vararg_nodes = vm->compiler.macro_vararg_nodes;
    char **saved_vararg_strs = vm->compiler.macro_vararg_strs;
    int saved_vararg_count = vm->compiler.macro_vararg_count;
    bool saved_vararg_string_mode = vm->compiler.macro_vararg_string_mode;

    if (pm->is_variadic) {
        int var_count = total_args - pm->fixed_param_count;
        Node **var_nodes =
            var_count > 0 ? alloca((size_t)var_count * sizeof(Node *)) : NULL;
        for (int i = 0; i < var_count; i++) {
            var_nodes[i] = arg;
            if (arg)
                arg = arg->next;
        }
        vm->compiler.macro_vararg_nodes = var_nodes;
        vm->compiler.macro_vararg_strs = NULL;
        vm->compiler.macro_vararg_count = var_count;
        vm->compiler.macro_vararg_string_mode = false;
    } else {
        vm->compiler.macro_vararg_nodes = NULL;
        vm->compiler.macro_vararg_strs = NULL;
        vm->compiler.macro_vararg_count = 0;
        vm->compiler.macro_vararg_string_mode = false;
    }

    Node *result = execute_macro_fn(vm, pm, tok, NULL, total_args,
                                    fixed_values, fixed_count);
    (void)result;

    vm->compiler.macro_vararg_nodes = saved_vararg_nodes;
    vm->compiler.macro_vararg_strs = saved_vararg_strs;
    vm->compiler.macro_vararg_count = saved_vararg_count;
    vm->compiler.macro_vararg_string_mode = saved_vararg_string_mode;
}

// Find macro function by name
static MacroFn *find_macro_fn_by_name(VirtualMachine *vm, const char *name) {
    for (MacroFn *pm = vm->compiler.macro_fns; pm; pm = pm->next) {
        if (pm->is_macro_entry && strlen(pm->name) == strlen(name) &&
            strncmp(pm->name, name, strlen(name)) == 0)
            return pm;
    }
    return NULL;
}

// Ticket #277: Lisp-style single-step macro expansion (macroexpand-1).
// Expands the outermost ND_MACRO_CALL exactly once; identity for anything else.
Node *__builtin_macroexpand_1(VirtualMachine *vm, Node *node) {
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
                                    node->macro_arg_count, NULL, 0);
    vm->compiler.scope = saved_scope;
    return result ? result : node;
}

// Ticket #277: Lisp-style full macro expansion (macroexpand).
// Repeatedly expands the outermost node via macroexpand_1 until it is no
// longer an ND_MACRO_CALL (i.e. until the form is stable at the top level).
// Does not recurse into child nodes — only the top-level call is expanded.
Node *__builtin_macroexpand(VirtualMachine *vm, Node *node) {
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
        Node *next = __builtin_macroexpand_1(vm, current);
        if (next == current)
            break;
        current = next;
        depth++;
    }
    return current;
}

void cc_execute_top_level_macro(VirtualMachine *vm, char *name, Token *tok,
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

    init_vm_segments_for_macros(vm);
    compile_all_macros(vm);

    if (!pm->is_compiled) {
        error_tok(vm, tok, "macro '%s' failed to compile", name);
        return;
    }

    Node *result = execute_macro_fn(vm, pm, tok, args, arg_count, NULL, 0);
    // Declaration position: NULL (or void return) is legal — the macro may have
    // emitted definitions as side-effects without having a node to splice.
    (void)result;
}

// Recursively transform macro calls in an AST node
static Node *transform_node(VirtualMachine *vm, Node *node, int depth);

static Node *transform_node(VirtualMachine *vm, Node *node, int depth) {
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

        Scope *saved_scope = vm->compiler.scope;
        if (node->macro_scope)
            vm->compiler.scope = node->macro_scope;
        Node *result =
            execute_macro_fn(vm, pm, node->tok, node->args,
                             node->macro_arg_count, NULL, 0);
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
static void init_vm_segments_for_macros(VirtualMachine *vm) {
    if (vm->text_seg)
        return; // Already initialized

    // Reserve and commit all segments (base pointers will never move)
    vm_alloc_segments(vm);

    // Initialize codegen state
    vm->compiler.current_codegen_fn = NULL;
    // sp/bp/stack_base already set correctly by vm_alloc_segments

    // Initialize source map for debugger (if enabled). Mirrors the block in
    // cc_compile() (src/bytecode.c) -- macro/comptime compilation runs
    // before the main program's cc_compile(), so without this the first
    // emit_source_location() call during macro codegen finds
    // source_map_capacity == 0 and corrupts the heap (ticket #405).
    if (vm->flags & CCCC_ENABLE_DEBUGGER) {
        vm->dbg.source_map_capacity = 1024;
        vm->dbg.source_map = malloc(vm->dbg.source_map_capacity * sizeof(SourceMap));
        if (!vm->dbg.source_map) {
            error("could not malloc for source map");
        }
        vm->dbg.source_map_count = 0;
        vm->dbg.last_debug_file = NULL;
        vm->dbg.last_debug_line = -1;
        vm->dbg.source_index = NULL;
        vm->dbg.source_index_count = 0;
        vm->dbg.num_debug_symbols = 0;
        vm->dbg.num_watchpoints = 0;
    }
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
    case TY_NULLPTR_T:
        // nullptr_t has the same size/representation as a pointer.
        n += snprintf(buf + n, bufsize - n, "void *");
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
static Token *synthesize_forward_decl_tokens(VirtualMachine *vm, Obj *fn) {
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
static Token *synthesize_global_decl_tokens(VirtualMachine *vm, Obj *var) {
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
static void scan_and_execute_global_calls(VirtualMachine *vm, Token **tokens_ptr) {
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

        if (brace_depth == 0 && paren_depth == 0 &&
            tok->kind == TK_IDENT && tok->len == 18 &&
            strncmp(tok->loc, "__builtin_emit_line__", 18) == 0 &&
            tok->next && equal(tok->next, "(") &&
            tok->next->next && tok->next->next->kind == TK_STR &&
            tok->next->next->next && equal(tok->next->next->next, ")") &&
            tok->next->next->next->next &&
            equal(tok->next->next->next->next, ";")) {
            Token *next_tok = tok->next->next->next->next->next;
            cc_record_emit_source(vm, tok->next->next->str);
            if (prev)
                prev->next = next_tok;
            else
                *tokens_ptr = next_tok;
            tok = next_tok;
            continue;
        }

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

                        // Collect comma-separated arguments as char* strings.
                        // Each argument token sequence is stringified and
                        // placed directly in VM registers (REG_A0+) so that
                        // macro parameters declared as `char *` receive the
                        // actual string data, not a Node wrapper.
                        // TK_STR tokens pass their string value directly;
                        // keywords/idents/numbers pass their spelling.
                        int max_args = 0;
                        if (tok->next->next != after_paren) {
                            max_args = 1;
                            int count_depth = 0;
                            for (Token *t = tok->next->next;
                                 t && t != after_paren;
                                 t = t->next) {
                                if (equal(t, "("))
                                    count_depth++;
                                else if (equal(t, ")") && count_depth > 0)
                                    count_depth--;
                                else if (count_depth == 0 && equal(t, ","))
                                    max_args++;
                            }
                        }
                        char **arg_strs = max_args > 0
                            ? alloca(max_args * sizeof(char *))
                            : NULL;
                        int arg_count = 0;
                        Token *a = tok->next->next; // first token after '('
                        while (a && a != after_paren) {
                            int depth = 0;
                            Token *arg_start = a;
                            Token *arg_end = a;
                            while (a && a != after_paren) {
                                if (equal(a, "(")) depth++;
                                else if (equal(a, ")")) depth--;
                                if (depth == 0 && equal(a, ",")) {
                                    a = a->next;
                                    break;
                                }
                                arg_end = a;
                                a = a->next;
                            }
                            char *str;
                            if (arg_start == arg_end &&
                                arg_start->kind == TK_STR) {
                                str = arg_start->str;
                            } else {
                                int total = 0;
                                for (Token *t = arg_start;
                                     t && t != arg_end->next;
                                     t = t->next)
                                    total += t->len;
                                str = arena_alloc(
                                    &vm->compiler.parser_arena, total + 1);
                                int pos = 0;
                                for (Token *t = arg_start;
                                     t && t != arg_end->next;
                                     t = t->next) {
                                    memcpy(str + pos, t->loc, t->len);
                                    pos += t->len;
                                }
                                str[pos] = '\0';
                            }
                            arg_strs[arg_count++] = str;
                        }
                        if (pm->is_variadic && arg_count < pm->fixed_param_count) {
                            error_tok(vm, tok,
                                      "macro '%.*s' called with %d arguments; expected at least %d",
                                      tok->len, tok->loc, arg_count,
                                      pm->fixed_param_count);
                        }
                        // Place char* values using the VM calling convention
                        // before calling execute_macro_fn with NULL args so
                        // the arg-setup loop inside does not overwrite them.
                        int fixed_count = pm->is_variadic ? pm->fixed_param_count
                                                          : arg_count;
                        long long *fixed_args =
                            fixed_count > 0
                                ? alloca((size_t)fixed_count * sizeof(long long))
                                : NULL;
                        for (int i = 0; i < fixed_count; i++)
                            fixed_args[i] = (long long)arg_strs[i];

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

                        Node **saved_vararg_nodes =
                            vm->compiler.macro_vararg_nodes;
                        char **saved_vararg_strs =
                            vm->compiler.macro_vararg_strs;
                        int saved_vararg_count =
                            vm->compiler.macro_vararg_count;
                        bool saved_vararg_string_mode =
                            vm->compiler.macro_vararg_string_mode;
                        if (pm->is_variadic) {
                            vm->compiler.macro_vararg_nodes = NULL;
                            vm->compiler.macro_vararg_strs =
                                arg_strs + pm->fixed_param_count;
                            vm->compiler.macro_vararg_count =
                                arg_count - pm->fixed_param_count;
                            vm->compiler.macro_vararg_string_mode = true;
                        } else {
                            vm->compiler.macro_vararg_nodes = NULL;
                            vm->compiler.macro_vararg_strs = NULL;
                            vm->compiler.macro_vararg_count = 0;
                            vm->compiler.macro_vararg_string_mode = true;
                        }

                        bool saved_emit_recording =
                            vm->compiler.macro_emit_recording;
                        vm->compiler.macro_emit_recording = true;
                        Node *block_result =
                            execute_macro_fn(vm, pm, tok, NULL, arg_count,
                                             fixed_args, fixed_count);
                        vm->compiler.macro_emit_recording =
                            saved_emit_recording;
                        vm->compiler.macro_vararg_nodes = saved_vararg_nodes;
                        vm->compiler.macro_vararg_strs = saved_vararg_strs;
                        vm->compiler.macro_vararg_count = saved_vararg_count;
                        vm->compiler.macro_vararg_string_mode =
                            saved_vararg_string_mode;
                        if (scope_before)
                            scope_before->next = saved_scope_next;
                        vm->compiler.scope = scope_before;

                        // Drain newly prepended objects into macro_globals using
                        // a saved-next walk so we never overwrite a next pointer
                        // we still need to follow (which would create a cycle).
                        Obj *o = vm->compiler.globals;
                        while (o && o != globals_before) {
                            Obj *next_obj = o->next;
                            cc_record_emit_object(vm, o);
                            o->next = vm->compiler.macro_globals;
                            vm->compiler.macro_globals = o;
                            o = next_obj;
                        }
                        vm->compiler.globals = globals_before;

                        // Ticket #233: if the macro returned an ND_BLOCK, splice
                        // its body tokens into the stream so they are re-parsed
                        // at global scope instead of being silently discarded.
                        // The block came from Quote("{ ... }") so the token
                        // chain is: outer-'{' -> body tokens -> outer-'}' -> EOF.
                        // We start from block_result->tok->next (the token after
                        // the outer '{') and walk forward, tracking brace depth
                        // starting at 0, stopping just before the outer '}'.
                        // Note: compound_stmt adds declarations as side effects
                        // so block->body may be NULL; use tok-level injection.
                        if (block_result &&
                            block_result->kind == ND_BLOCK &&
                            block_result->tok &&
                            block_result->tok->kind != TK_EOF &&
                            !equal(block_result->tok, "}")) {
                            Token *body_first = block_result->tok;
                            Token *body_last  = NULL;
                            int bdepth = 0;
                            for (Token *t = body_first;
                                 t && t->kind != TK_EOF;
                                 t = t->next) {
                                if (equal(t, "{"))
                                    bdepth++;
                                else if (equal(t, "}")) {
                                    if (bdepth == 0)
                                        break; // outer closing brace
                                    bdepth--;
                                }
                                body_last = t;
                            }
                            if (body_last) {
                                body_last->next = next_tok;
                                if (prev)
                                    prev->next = body_first;
                                else
                                    *tokens_ptr = body_first;
                                tok = body_first;
                                continue;
                            }
                        }

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
//   2. Execute the macro (it calls __builtin_ast_function etc.).
//   3. Drain newly-added Objs into vm->compiler.macro_globals immediately
//      (safe saved-next walk; globals is restored to its pre-call state).
//   4. Remove the call tokens so the parser never sees them.
//   5. Synthesize forward-declaration token streams for each generated
//      function/global and prepend them to every input_tokens[i].
//
// After this runs, vm->compiler.macro_globals contains the generated
// definitions. main.c appends them to the merged program before codegen.
void cc_execute_inline_macros(VirtualMachine *vm, Token **input_tokens, int count) {
    if (!vm)
        return;

    if (!vm->compiler.macro_fns) {
        for (int fi = 0; fi < count; fi++)
            if (input_tokens[fi])
                scan_and_execute_global_calls(vm, &input_tokens[fi]);
        return;
    }

    if (!vm->compiler.macro_context_tokens)
        vm->compiler.macro_context_tokens =
            build_macro_context_tokens(vm, input_tokens, count);

    // Quick check: any non-inline macros?
    bool any_global = false;
    for (MacroFn *pm = vm->compiler.macro_fns; pm; pm = pm->next)
        if (!pm->is_inline) { any_global = true; break; }
    if (!any_global) {
        for (int fi = 0; fi < count; fi++)
            if (input_tokens[fi])
                scan_and_execute_global_calls(vm, &input_tokens[fi]);
        return;
    }

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
void cc_expand_macros(VirtualMachine *vm, Obj *prog) {
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
        // any new_lvar() calls inside __builtin_quote (e.g. pointer temps for
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
