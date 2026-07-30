/* GENERATED FILE -- DO NOT EDIT.
 * Produced by tools/gen_reflection_ffi.py from include/cccc/reflection.h.
 * Regenerate with: python3 tools/gen_reflection_ffi.py
 */


extern const char * __builtin_gensym(const char *prefix);
extern Node * __builtin_macroexpand_1(Node *node);
extern Node * __builtin_macroexpand(Node *node);
extern int __builtin_ast_vararg_count(void);
extern Node * __builtin_ast_vararg_at(int index);
extern Node ** __builtin_ast_varargs_as_array(void);
extern const char * __builtin_ast_vararg_str_at(int index);
extern Token * __builtin_ast_current_token(void);
extern Token * __builtin_ast_synthetic_token(const char *label);
extern Token * __builtin_ast_token_from_node(Node *node);
extern Node * __builtin_ast_set_token(Node *node, Token *tok);
extern Node * __builtin_ast_copy_location(Node *dst, Node *src);
extern void __builtin_macro_error_at(Node *node, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
extern void __builtin_macro_warning_at(Node *node, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
extern Node * __builtin_quote(const char *tmpl, ...);
extern Node * __builtin_quote_n(const char *tmpl, Node **nodes, int count);
extern Node * __builtin_node_list(Node **nodes, int count);
extern Type * __builtin_ast_find_type(const char *name);
extern bool __builtin_ast_type_exists(const char *name);
extern Type * __builtin_ast_get_type(const char *name);
extern TypeKind __builtin_ast_type_kind(Type *ty);
extern int __builtin_ast_type_size(Type *ty);
extern int __builtin_ast_type_align(Type *ty);
extern bool __builtin_ast_type_is_unsigned(Type *ty);
extern bool __builtin_ast_type_is_const(Type *ty);
extern Type * __builtin_ast_type_base(Type *ty);
extern int __builtin_ast_type_array_len(Type *ty);
extern Type * __builtin_ast_type_return_type(Type *ty);
extern int __builtin_ast_type_param_count(Type *ty);
extern Type * __builtin_ast_type_param_at(Type *ty, int index);
extern bool __builtin_ast_type_is_variadic(Type *ty);
extern const char * __builtin_ast_type_name(Type *ty);
extern const char * __builtin_ast_type_c_name(Type *ty);
extern Type * __builtin_ast_make_pointer(Type *base);
extern Type * __builtin_ast_make_array(Type *base, int length);
extern Type * __builtin_ast_make_func_ptr_type(Type *return_ty,
                                        Type **param_types, int nparams);
extern Type * __builtin_ast_make_const(Type *ty);
extern Type * __builtin_ast_make_volatile(Type *ty);
extern int __builtin_ast_enum_count(Type *enum_type);
extern EnumConstant * __builtin_ast_enum_at(Type *enum_type, int index);
extern EnumConstant * __builtin_ast_enum_find(Type *enum_type,
                                    const char *name);
extern const char * __builtin_ast_enum_constant_name(EnumConstant *ec);
extern int64_t __builtin_ast_enum_constant_value(EnumConstant *ec);
extern const char * __builtin_ast_enum_name(Type *e);
extern int __builtin_ast_enum_value_count(Type *e);
extern const char * __builtin_ast_enum_value_name(Type *e, int index);
extern int64_t __builtin_ast_enum_value(Type *e, int index);
extern Node * __builtin_ast_enum_to_string_switch(Type *ty, Node *expr);
extern Node * __builtin_ast_enum_from_string_chain(Type *ty, Node *expr);
extern int __builtin_ast_struct_member_count(Type *struct_type);
extern Member * __builtin_ast_struct_member_at(Type *struct_type,
                                        int index);
extern Member * __builtin_ast_struct_member_find(Type *struct_type,
                                        const char *name);
extern const char * __builtin_ast_member_name(Member *m);
extern Type * __builtin_ast_member_type(Member *m);
extern int __builtin_ast_member_offset(Member *m);
extern bool __builtin_ast_member_is_bitfield(Member *m);
extern int __builtin_ast_member_bitfield_width(Member *m);
extern int64_t __builtin_ast_offsetof_chain(Type *ty,
                                   const char **names, int n);
extern Obj * __builtin_ast_find_global(const char *name);
extern int __builtin_ast_global_count(void);
extern Obj * __builtin_ast_global_at(int index);
extern const char * __builtin_ast_obj_name(Obj *obj);
extern Type * __builtin_ast_obj_type(Obj *obj);
extern bool __builtin_ast_obj_is_function(Obj *obj);
extern bool __builtin_ast_obj_is_definition(Obj *obj);
extern bool __builtin_ast_obj_is_static(Obj *obj);
extern int __builtin_attr_target_kind(AttrTarget *target);
extern const char * __builtin_attr_target_name(AttrTarget *target);
extern Type * __builtin_attr_target_type(AttrTarget *target);
extern Obj * __builtin_attr_target_obj(AttrTarget *target);
extern Token * __builtin_attr_target_token(AttrTarget *target);
extern Node * __builtin_ast_int_literal(int64_t value);
extern Node * __builtin_ast_float_literal(double value);
extern Node * __builtin_ast_string_literal(const char *str);
extern Node * __builtin_ast_var_ref(const char *name);
extern Node * __builtin_ast_param_ref(Obj *fn, const char *name);
extern Node * __builtin_ast_binary(NodeKind op, Node *left,
                            Node *right);
extern Node * __builtin_ast_unary(NodeKind op, Node *operand);
extern Node * __builtin_ast_cast(Node *expr, Type *target_type);
extern Node * __builtin_ast_cond(Node *cond, Node *then_expr,
                          Node *else_expr);
extern Node * __builtin_ast_null(void);
extern Node * __builtin_ast_sizeof_type(Type *ty);
extern Node * __builtin_ast_alignof_type(Type *ty);
extern Node * __builtin_ast_sizeof_expr(Node *expr);
extern Node * __builtin_ast_subscript(Node *arr, Node *idx);
extern Node * __builtin_ast_comma(Node *lhs, Node *rhs);
extern Node * __builtin_ast_return(Node *expr);
extern Node * __builtin_ast_block(Node **stmts, int count);
extern Node * __builtin_ast_block_add_stmt(Node *block, Node *stmt);
extern Node * __builtin_ast_if(Node *cond, Node *then_body,
                        Node *else_body);
extern Node * __builtin_ast_switch(Node *cond);
extern void __builtin_ast_switch_add_case(Node *switch_node,
                                Node *value, Node *body);
extern void __builtin_ast_switch_set_default(Node *switch_node,
                                    Node *body);
extern Node * __builtin_ast_expr_stmt(Node *expr);
extern Node * __builtin_ast_local_var(const char *name, Type *ty);
extern Node * __builtin_ast_local_var_unique(Type *ty);
extern Node * __builtin_ast_assign(Node *target, Node *value);
extern Node * __builtin_ast_member(Node *obj, const char *name);
extern Node * __builtin_ast_funcall(Node *callee, Node **args, int n);
extern Node * __builtin_ast_while(Node *cond, Node *body);
extern Node * __builtin_ast_for(Node *init, Node *cond,
                       Node *inc, Node *body);
extern Node * __builtin_ast_do_while(Node *body, Node *cond);
extern Obj * __builtin_ast_function(const char *name,
                            Type *return_type);
extern Node * __builtin_ast_publish(Obj *obj, Token *tok);
extern Node * __builtin_ast_publish_type(Type *ty, Token *tok);
extern void __builtin_emit_directive(const char *line);
extern void __builtin_ast_function_add_param(Obj *fn, const char *name,
                                Type *type);
extern void __builtin_ast_function_set_body(Obj *fn, Node *body);
extern void __builtin_ast_function_set_static(Obj *fn, bool is_static);
extern void __builtin_ast_function_set_inline(Obj *fn, bool is_inline);
extern void __builtin_ast_function_set_variadic(Obj *fn, bool is_variadic);
extern Obj * __builtin_ast_function_prototype(const char *name,
                                    Type *return_type);
extern void __builtin_ast_add_attribute(Obj *fn, const char *attr_text);
extern void __builtin_ast_add_build_target_attr(Obj *fn, const char *kind);
extern Type * __builtin_ast_make_struct(const char *name);
extern Type * __builtin_ast_make_union(const char *name);
extern Type * __builtin_ast_struct_add_field(Type *ty, const char *name,
                                   Type *field_type);
extern Type * __builtin_ast_make_enum(const char *name);
extern void __builtin_ast_enum_add_constant(Type *ty, const char *name,
                                  int64_t value);
extern Type * __builtin_ast_make_typedef(const char *name, Type *underlying);
extern Obj * __builtin_ast_global_var(const char *name, Type *ty);
extern void __builtin_ast_global_var_set_init_data(Obj *var,
                                        const char *data, int len);
extern void __builtin_ast_global_var_set_static(Obj *var, bool is_static);
extern void __builtin_ast_push_fn(Obj *fn);
extern void __builtin_ast_pop_fn(void);
extern void __builtin_ast_push_block(Node *block);
extern void __builtin_ast_pop_block(void);
extern Node * __builtin_ast_block_add_current_stmt(Node *stmt);
extern void __builtin_ast_push_struct(Type *ty);
extern void __builtin_ast_pop_struct(void);
extern Type * __builtin_ast_struct_add_current_field(const char *name,
                                            Type *field_type);
extern void __builtin_ast_push_switch(Node *switch_node);
extern void __builtin_ast_pop_switch(void);
extern void __builtin_ast_switch_add_current_case(Node *value,
                                       Node *body);
extern void __builtin_ast_switch_set_current_default(Node *body);
extern void __builtin_ast_push_enum(Type *ty);
extern void __builtin_ast_pop_enum(void);
extern void __builtin_ast_enum_add_current_constant(const char *name,
                                         int value);
extern void __builtin_dump_tree(Node *node);
extern const char * __builtin_dump_tree_to_string(Node *node);
extern void __builtin_dump_ast_gen(Node *node);
extern const char * __builtin_dump_ast_gen_to_string(Node *node);
extern int64_t __builtin_get_comptime_int(const char *name);
extern double __builtin_get_comptime_float(const char *name);
extern Node * __builtin_get_comptime_var(const char *name);
extern Node * __builtin_get_comptime_ptr(const char *name);
extern Node * __builtin_get_comptime_member(const char *var_name,
                                  const char *field);
extern Node * __builtin_get_constexpr_value(const char *name);
extern Node * __builtin_ast_compound_literal(Type *ty, Node **inits, int n);
extern Node * __builtin_ast_init_array(Type *elem_ty, Node **elems, int n);
extern Node * __builtin_ast_init_struct(Type *ty, const char **fields,
                                Node **values, int n);
extern Node * __builtin_ast_serialize(Type *ty, Node *expr, Node *buf);
extern Node * __builtin_ast_deserialize(Type *ty, Node *buf);
extern void __builtin_generate_sum(Type *elem_ty);
extern void __builtin_generate_map(Type *elem_ty);
extern void __builtin_generate_reduce(Type *elem_ty);
extern void __builtin_generate_filter(Type *elem_ty);
