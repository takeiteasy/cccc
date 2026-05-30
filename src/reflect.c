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

// Reflection API for pragma macros
// Provides type introspection and AST construction functions

#include "./internal.h"

// Aliases so the public API types match reflection.h
typedef Type JCC_Type;
typedef Node JCC_Node;
typedef Obj JCC_Obj;
typedef Member JCC_Member;
typedef EnumConstant JCC_EnumConstant;
typedef Token JCC_Token;
typedef TypeKind JCC_TypeKind;
typedef NodeKind JCC_NodeKind;

// Global VM pointer for jcc_get_vm() builtin
// Set during pragma macro execution, cleared after
JCC *__jcc_current_vm = NULL;

// Builtin function to get the current VM context
JCC *jcc_get_vm(void) { return __jcc_current_vm; }

// ============================================================================
// Internal Helpers (replicate static functions from parse.c)
// ============================================================================

static Obj *reflect_new_var(JCC *vm, char *name, int name_len, Type *ty) {
    Obj *var = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
    memset(var, 0, sizeof(Obj));
    var->name = name;
    var->ty = ty;
    var->align = ty->align;
    return var;
}

static char *reflect_unique_name(JCC *vm) {
    return arena_format(vm, ".L..%d", vm->compiler.unique_name_counter++);
}

static Obj *reflect_new_gvar(JCC *vm, char *name, int name_len, Type *ty) {
    Obj *var = reflect_new_var(vm, name, name_len, ty);
    var->next = vm->compiler.globals;
    var->is_static = true;
    var->is_definition = true;
    vm->compiler.globals = var;
    return var;
}

static Obj *reflect_new_anon_gvar(JCC *vm, Type *ty) {
    char *name = reflect_unique_name(vm);
    return reflect_new_gvar(vm, name, strlen(name), ty);
}

// ============================================================================
// Type Lookup and Introspection
// ============================================================================

JCC_Type *jcc_ast_find_type(JCC *vm, const char *name) {
    if (!vm || !name)
        return NULL;

    size_t name_len = strlen(name);

    // Search through all scopes, starting from innermost
    for (Scope *sc = vm->compiler.scope; sc; sc = sc->next) {
        // First search struct/union/enum tags
        for (TagScopeNode *node = sc->tags; node; node = node->next) {
            if (node->name_len == (int)name_len &&
                strncmp(node->name, name, name_len) == 0) {
                return node->ty;
            }
        }
        // Also search typedefs (stored in vars with type_def set)
        for (VarScopeNode *node = sc->vars; node; node = node->next) {
            if (node->type_def && node->name_len == (int)name_len &&
                strncmp(node->name, name, name_len) == 0) {
                return node->type_def;
            }
        }
    }

    return NULL;
}

bool jcc_ast_type_exists(JCC *vm, const char *name) {
    return jcc_ast_find_type(vm, name) != NULL;
}

JCC_Type *jcc_ast_get_type(JCC *vm, const char *name) {
    if (!name)
        return NULL;

    const struct {
        const char *name;
        Type *type;
    } builtins[] = {
        {"void",   ty_void},
        {"char",   ty_char},
        {"short",  ty_short},
        {"int",    ty_int},
        {"long",   ty_long},
        {"float",  ty_float},
        {"double", ty_double},
        {"_Bool",  ty_bool},
    };

    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++)
        if (strlen(name) == strlen(builtins[i].name) &&
            strncmp(name, builtins[i].name, strlen(builtins[i].name)) == 0)
            return builtins[i].type;

    return jcc_ast_find_type(vm, name);
}

JCC_TypeKind jcc_ast_type_kind(JCC_Type *ty) {
    return ty ? ty->kind : TY_VOID;
}

int jcc_ast_type_size(JCC_Type *ty) { return ty ? ty->size : 0; }

int jcc_ast_type_align(JCC_Type *ty) { return ty ? ty->align : 0; }

bool jcc_ast_type_is_unsigned(JCC_Type *ty) {
    return ty ? ty->is_unsigned : false;
}

bool jcc_ast_type_is_const(JCC_Type *ty) {
    return ty ? ty->is_const : false;
}

JCC_Type *jcc_ast_type_base(JCC_Type *ty) {
    if (!ty)
        return NULL;
    if (ty->kind != TY_PTR && ty->kind != TY_ARRAY && ty->kind != TY_VLA)
        return NULL;
    return ty->base;
}

int jcc_ast_type_array_len(JCC_Type *ty) {
    if (!ty || ty->kind != TY_ARRAY)
        return -1;
    return ty->array_len;
}

JCC_Type *jcc_ast_type_return_type(JCC_Type *ty) {
    if (!ty || ty->kind != TY_FUNC)
        return NULL;
    return ty->return_ty;
}

int jcc_ast_type_param_count(JCC_Type *ty) {
    if (!ty || ty->kind != TY_FUNC)
        return -1;

    int count = 0;
    for (Type *p = ty->params; p; p = p->next)
        count++;
    return count;
}

JCC_Type *jcc_ast_type_param_at(JCC_Type *ty, int index) {
    if (!ty || ty->kind != TY_FUNC || index < 0)
        return NULL;

    Type *p = ty->params;
    for (int i = 0; i < index && p; i++)
        p = p->next;
    return p;
}

bool jcc_ast_type_is_variadic(JCC_Type *ty) {
    if (!ty || ty->kind != TY_FUNC)
        return false;
    return ty->is_variadic;
}

const char *jcc_ast_type_name(JCC_Type *ty) {
    if (!ty || !ty->name)
        return NULL;

    // Extract string from token
    static char buffer[256];
    int len = ty->name->len;
    if (len >= (int)sizeof(buffer))
        len = sizeof(buffer) - 1;
    strncpy(buffer, ty->name->loc, len);
    buffer[len] = '\0';
    return buffer;
}

JCC_Type *jcc_ast_make_pointer(JCC *vm, JCC_Type *base) {
    if (!vm || !base)
        return NULL;
    return pointer_to(vm, base);
}

JCC_Type *jcc_ast_make_array(JCC *vm, JCC_Type *base, int len) {
    if (!vm || !base || len < 0)
        return NULL;
    return array_of(vm, base, len);
}

// ============================================================================
// Enum Reflection
// ============================================================================

int jcc_ast_enum_count(JCC *vm, JCC_Type *enum_type) {
    (void)vm; // Unused but kept for API consistency
    if (!enum_type || enum_type->kind != TY_ENUM)
        return -1;

    int count = 0;
    for (EnumConstant *ec = enum_type->enum_constants; ec; ec = ec->next)
        count++;
    return count;
}

JCC_EnumConstant *jcc_ast_enum_at(JCC *vm, JCC_Type *enum_type, int index) {
    (void)vm;
    if (!enum_type || enum_type->kind != TY_ENUM || index < 0)
        return NULL;

    EnumConstant *ec = enum_type->enum_constants;
    for (int i = 0; i < index && ec; i++)
        ec = ec->next;
    return ec;
}

JCC_EnumConstant *jcc_ast_enum_find(JCC *vm, JCC_Type *enum_type,
                                    const char *name) {
    (void)vm;
    if (!enum_type || enum_type->kind != TY_ENUM || !name)
        return NULL;

    for (EnumConstant *ec = enum_type->enum_constants; ec; ec = ec->next)
        if (strlen(ec->name) == strlen(name) &&
            strncmp(ec->name, name, strlen(name)) == 0)
            return ec;
    return NULL;
}

const char *jcc_ast_enum_constant_name(JCC_EnumConstant *ec) {
    return ec ? ec->name : NULL;
}

int jcc_ast_enum_constant_value(JCC_EnumConstant *ec) {
    return ec ? ec->value : 0;
}

const char *jcc_ast_enum_name(JCC_Type *e) { return jcc_ast_type_name(e); }

int jcc_ast_enum_value_count(JCC_Type *e) {
    int count = jcc_ast_enum_count(NULL, e);
    return count < 0 ? 0 : count;
}

const char *jcc_ast_enum_value_name(JCC_Type *e, int index) {
    JCC_EnumConstant *ec = jcc_ast_enum_at(NULL, e, index);
    return jcc_ast_enum_constant_name(ec);
}

int jcc_ast_enum_value(JCC_Type *e, int index) {
    JCC_EnumConstant *ec = jcc_ast_enum_at(NULL, e, index);
    return jcc_ast_enum_constant_value(ec);
}

// ============================================================================
// Struct/Union Member Introspection
// ============================================================================

int jcc_ast_struct_member_count(JCC *vm, JCC_Type *struct_type) {
    (void)vm;
    if (!struct_type)
        return -1;
    if (struct_type->kind != TY_STRUCT && struct_type->kind != TY_UNION)
        return -1;

    int count = 0;
    for (Member *m = struct_type->members; m; m = m->next)
        count++;
    return count;
}

JCC_Member *jcc_ast_struct_member_at(JCC *vm, JCC_Type *struct_type,
                                        int index) {
    (void)vm;
    if (!struct_type || index < 0)
        return NULL;
    if (struct_type->kind != TY_STRUCT && struct_type->kind != TY_UNION)
        return NULL;

    Member *m = struct_type->members;
    for (int i = 0; i < index && m; i++)
        m = m->next;
    return m;
}

JCC_Member *jcc_ast_struct_member_find(JCC *vm, JCC_Type *struct_type,
                                        const char *name) {
    (void)vm;
    if (!struct_type || !name)
        return NULL;
    if (struct_type->kind != TY_STRUCT && struct_type->kind != TY_UNION)
        return NULL;

    size_t name_len = strlen(name);
    for (Member *m = struct_type->members; m; m = m->next)
        if (m->name && m->name->len == (int)name_len &&
            strncmp(m->name->loc, name, name_len) == 0)
            return m;
    return NULL;
}

const char *jcc_ast_member_name(JCC_Member *m) {
    if (!m || !m->name)
        return NULL;

    // Extract string from token
    static char buffer[256];
    int len = m->name->len;
    if (len >= (int)sizeof(buffer))
        len = sizeof(buffer) - 1;
    strncpy(buffer, m->name->loc, len);
    buffer[len] = '\0';
    return buffer;
}

JCC_Type *jcc_ast_member_type(JCC_Member *m) { return m ? m->ty : NULL; }

int jcc_ast_member_offset(JCC_Member *m) { return m ? m->offset : 0; }

bool jcc_ast_member_is_bitfield(JCC_Member *m) {
    return m ? m->is_bitfield : false;
}

int jcc_ast_member_bitfield_width(JCC_Member *m) {
    return (m && m->is_bitfield) ? m->bit_width : 0;
}

// ============================================================================
// Global Symbol Introspection
// ============================================================================

JCC_Obj *jcc_ast_find_global(JCC *vm, const char *name) {
    if (!vm || !name)
        return NULL;

    size_t name_len = strlen(name);
    for (Obj *obj = vm->compiler.globals; obj; obj = obj->next) {
        if (strlen(obj->name) == name_len &&
            strncmp(obj->name, name, name_len) == 0)
            return obj;
    }
    return NULL;
}

int jcc_ast_global_count(JCC *vm) {
    if (!vm)
        return 0;

    int count = 0;
    for (Obj *obj = vm->compiler.globals; obj; obj = obj->next)
        count++;
    return count;
}

JCC_Obj *jcc_ast_global_at(JCC *vm, int index) {
    if (!vm || index < 0)
        return NULL;

    Obj *obj = vm->compiler.globals;
    for (int i = 0; i < index && obj; i++)
        obj = obj->next;
    return obj;
}

const char *jcc_ast_obj_name(JCC_Obj *obj) { return obj ? obj->name : NULL; }

JCC_Type *jcc_ast_obj_type(JCC_Obj *obj) { return obj ? obj->ty : NULL; }

bool jcc_ast_obj_is_function(JCC_Obj *obj) {
    return obj ? obj->is_function : false;
}

bool jcc_ast_obj_is_definition(JCC_Obj *obj) {
    return obj ? obj->is_definition : false;
}

bool jcc_ast_obj_is_static(JCC_Obj *obj) { return obj ? obj->is_static : false; }

// ============================================================================
// AST Node Construction - Helper
// ============================================================================

static JCC_Node *alloc_node(JCC *vm, JCC_NodeKind kind) {
    JCC_Node *node = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    memset(node, 0, sizeof(Node));
    node->kind = kind;
    return node;
}

// ============================================================================
// AST Node Construction - Literals
// ============================================================================

JCC_Node *jcc_ast_int_literal(JCC *vm, int64_t value) {
    if (!vm)
        return NULL;

    JCC_Node *node = alloc_node(vm, ND_NUM);
    node->val = value;
    node->ty = ty_long;
    return node;
}

JCC_Node *jcc_ast_float_literal(JCC *vm, double value) {
    if (!vm)
        return NULL;

    JCC_Node *node = alloc_node(vm, ND_NUM);
    node->fval = value;
    node->ty = ty_double;
    return node;
}

JCC_Node *jcc_ast_string_literal(JCC *vm, const char *str) {
    if (!vm || !str)
        return NULL;

    int len = strlen(str);

    // Create type for the string
    Type *ty = array_of(vm, ty_char, len + 1);

    // Create an anonymous global variable for the string
    Obj *var = reflect_new_anon_gvar(vm, ty);

    // Allocate space in the data segment and copy the string data
    // This is critical: we must place the data in the segment NOW,
    // not just set init_data (which is only used during initial emit_program)
    long long offset = vm->data_ptr - vm->data_seg;
    offset = (offset + 7) & ~7; // Align to 8 bytes
    if (vm->data_seg + offset + len + 1 > vm->data_seg + vm->poolsize)
        error("codegen: data segment overflow");
    vm->data_ptr = vm->data_seg + offset;

    var->offset = offset;
    var->init_data = (char *)vm->data_ptr; // Point directly to data segment

    // Copy string to data segment
    memcpy(vm->data_ptr, str, len + 1);
    vm->data_ptr += len + 1;

    // Create a variable reference node
    JCC_Node *node = alloc_node(vm, ND_VAR);
    node->var = var;
    node->ty = ty;
    return node;
}

JCC_Node *jcc_ast_var_ref(JCC *vm, const char *name) {
    if (!vm || !name)
        return NULL;

    // Look up the variable in current scope
    size_t name_len = strlen(name);

    for (Scope *sc = vm->compiler.scope; sc; sc = sc->next) {
        for (VarScopeNode *node = sc->vars; node; node = node->next) {
            if (node->name_len == (int)name_len &&
                strncmp(node->name, name, name_len) == 0) {
                if (node->var) {
                    JCC_Node *n = alloc_node(vm, ND_VAR);
                    n->var = node->var;
                    n->ty = node->var->ty;
                    return n;
                }
            }
        }
    }

    // Also check globals
    Obj *global = jcc_ast_find_global(vm, name);
    if (global) {
        JCC_Node *n = alloc_node(vm, ND_VAR);
        n->var = global;
        n->ty = global->ty;
        return n;
    }

    return NULL;
}

JCC_Node *jcc_ast_param_ref(JCC *vm, JCC_Obj *fn, const char *name) {
    if (!vm || !fn || !name)
        return NULL;

    // Find the parameter in the function's params list
    size_t name_len = strlen(name);
    for (Obj *param = fn->params; param; param = param->next) {
        if (strlen(param->name) == name_len &&
            strncmp(param->name, name, name_len) == 0) {
            JCC_Node *n = alloc_node(vm, ND_VAR);
            n->var = param;
            n->ty = param->ty;
            return n;
        }
    }

    return NULL;
}

// ============================================================================
// AST Node Construction - Expressions
// ============================================================================

JCC_Node *jcc_ast_binary(JCC *vm, JCC_NodeKind op, JCC_Node *left,
                            JCC_Node *right) {
    if (!vm || !left || !right)
        return NULL;

    JCC_Node *node = alloc_node(vm, op);
    node->lhs = left;
    node->rhs = right;
    // Type will be determined by add_type pass
    return node;
}

JCC_Node *jcc_ast_unary(JCC *vm, JCC_NodeKind op, JCC_Node *operand) {
    if (!vm || !operand)
        return NULL;

    JCC_Node *node = alloc_node(vm, op);
    node->lhs = operand;
    return node;
}

JCC_Node *jcc_ast_cast(JCC *vm, JCC_Node *expr, JCC_Type *target_type) {
    if (!vm || !expr || !target_type)
        return NULL;

    JCC_Node *node = alloc_node(vm, ND_CAST);
    node->lhs = expr;
    node->ty = target_type;
    return node;
}

// ============================================================================
// AST Node Construction - Statements
// ============================================================================

JCC_Node *jcc_ast_return(JCC *vm, JCC_Node *expr) {
    if (!vm)
        return NULL;

    JCC_Node *node = alloc_node(vm, ND_RETURN);
    node->lhs = expr;
    return node;
}

JCC_Node *jcc_ast_block(JCC *vm, JCC_Node **stmts, int count) {
    if (!vm)
        return NULL;

    JCC_Node *node = alloc_node(vm, ND_BLOCK);

    // Link statements together
    Node head = {};
    Node *cur = &head;
    for (int i = 0; i < count && stmts[i]; i++) {
        cur = cur->next = stmts[i];
    }
    node->body = head.next;
    return node;
}

JCC_Node *jcc_ast_if(JCC *vm, JCC_Node *cond, JCC_Node *then_body,
                        JCC_Node *else_body) {
    if (!vm || !cond)
        return NULL;

    JCC_Node *node = alloc_node(vm, ND_IF);
    node->cond = cond;
    node->then = then_body;
    node->els = else_body;
    return node;
}

JCC_Node *jcc_ast_switch(JCC *vm, JCC_Node *cond) {
    if (!vm || !cond)
        return NULL;

    JCC_Node *node = alloc_node(vm, ND_SWITCH);
    node->cond = cond;
    node->case_next = NULL;
    node->default_case = NULL;
    return node;
}

void jcc_ast_switch_add_case(JCC *vm, JCC_Node *switch_node, JCC_Node *value,
                                JCC_Node *body) {
    if (!vm || !switch_node || !value || !body)
        return;

    if (switch_node->kind != ND_SWITCH)
        return;

    // Create a case node
    JCC_Node *case_node = alloc_node(vm, ND_CASE);
    case_node->begin = value->val; // Assuming value is a numeric literal
    case_node->end = value->val;
    case_node->body = body;

    // Add to switch's case list
    case_node->case_next = switch_node->case_next;
    switch_node->case_next = case_node;
}

void jcc_ast_switch_set_default(JCC *vm, JCC_Node *switch_node,
                                    JCC_Node *body) {
    if (!vm || !switch_node || !body)
        return;

    if (switch_node->kind != ND_SWITCH)
        return;

    JCC_Node *def = alloc_node(vm, ND_CASE);
    def->body = body;
    switch_node->default_case = def;
}

// ============================================================================
// AST Node Construction - Declarations
// ============================================================================

JCC_Node *jcc_ast_expr_stmt(JCC *vm, JCC_Node *expr) {
    if (!vm)
        return NULL;

    JCC_Node *node = alloc_node(vm, ND_EXPR_STMT);
    node->lhs = expr;
    return node;
}

// ============================================================================
// Macro Diagnostics (ticket #78)
// ============================================================================

void jcc_error_at(JCC *vm, JCC_Node *node, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (node && node->tok)
        error_tok(vm, node->tok, "%s", buf);
    else
        error("%s", buf); // no source location available
}

void jcc_warning_at(JCC *vm, JCC_Node *node, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (node && node->tok)
        warn_tok(vm, node->tok, "%s", buf);
    else
        fprintf(stderr, "warning: %s\n", buf);
}

// ============================================================================
// AST Node Construction - Local Variable Injection (ticket #77)
// ============================================================================

// Allocate a local Obj and prepend it to the current function's locals list.
// Injected variables receive stack offsets later when cc_compile runs.
static JCC_Node *make_local_var_node(JCC *vm, char *name, JCC_Type *ty) {
    if (!vm || !ty)
        return NULL;

    Obj *fn = vm->compiler.current_fn;
    if (!fn) {
        // Not inside a function body — cannot inject a local
        return NULL;
    }

    Obj *var = reflect_new_var(vm, name, strlen(name), ty);
    var->is_local = true;
    // Prepend to the current function's locals list (same as new_lvar)
    var->next = fn->locals;
    fn->locals = var;

    JCC_Node *node = alloc_node(vm, ND_VAR);
    node->var = var;
    return node;
}

JCC_Node *jcc_ast_local_var(JCC *vm, const char *name, JCC_Type *ty) {
    if (!vm || !name || !ty)
        return NULL;

    char *arena_name = arena_format(vm, "%s", name);
    return make_local_var_node(vm, arena_name, ty);
}

JCC_Node *jcc_ast_local_var_unique(JCC *vm, JCC_Type *ty) {
    if (!vm || !ty)
        return NULL;

    // Use the existing gensym to produce a name that can't be captured by
    // user-written code (prefix .L.. is not valid in user identifiers)
    char *name = reflect_unique_name(vm);
    return make_local_var_node(vm, name, ty);
}

// ============================================================================
// AST Node Construction - New Expressions (ticket #51)
// ============================================================================

JCC_Node *jcc_ast_assign(JCC *vm, JCC_Node *target, JCC_Node *value) {
    if (!vm || !target || !value)
        return NULL;

    JCC_Node *node = alloc_node(vm, ND_ASSIGN);
    node->lhs = target;
    node->rhs = value;
    return node;
}

JCC_Node *jcc_ast_member(JCC *vm, JCC_Node *obj, const char *name) {
    if (!vm || !obj || !name)
        return NULL;

    // Ensure lhs type is computed (mirrors struct_ref in parse.c:3796)
    add_type(vm, obj);

    Type *ty = obj->ty;
    if (!ty || (ty->kind != TY_STRUCT && ty->kind != TY_UNION))
        return NULL;

    Member *mem = (Member *)jcc_ast_struct_member_find(vm, ty, name);
    if (!mem)
        return NULL;

    JCC_Node *node = alloc_node(vm, ND_MEMBER);
    node->lhs = obj;
    node->member = mem;
    return node;
}

JCC_Node *jcc_ast_funcall(JCC *vm, JCC_Node *callee, JCC_Node **args, int n) {
    if (!vm || !callee)
        return NULL;

    // Ensure callee's type is resolved (mirrors funcall() in parse.c:3967)
    add_type(vm, callee);

    Type *ty = callee->ty;
    if (!ty)
        return NULL;

    // Unwrap pointer-to-function (mirrors parse.c:3973)
    if (ty->kind == TY_PTR && ty->base && ty->base->kind == TY_FUNC)
        ty = ty->base;

    if (ty->kind != TY_FUNC)
        return NULL; // callee is not a function type

    JCC_Node *node = alloc_node(vm, ND_FUNCALL);
    node->lhs = callee;
    node->func_ty = ty;
    node->ty = ty->return_ty;

    // Chain argument nodes via ->next (mirrors jcc_ast_block pattern)
    Node head = {};
    Node *cur = &head;
    for (int i = 0; i < n && args[i]; i++)
        cur = cur->next = args[i];
    node->args = head.next;
    return node;
}

// jcc_ast_while: while(cond) body — represented as ND_FOR with init/inc NULL
JCC_Node *jcc_ast_while(JCC *vm, JCC_Node *cond, JCC_Node *body) {
    if (!vm || !cond)
        return NULL;

    JCC_Node *node = alloc_node(vm, ND_FOR);
    node->cond = cond;
    node->then = body;
    // node->init and node->inc left NULL — this is a while loop
    return node;
}

JCC_Node *jcc_ast_for(JCC *vm, JCC_Node *init, JCC_Node *cond,
                       JCC_Node *inc, JCC_Node *body) {
    if (!vm)
        return NULL;

    JCC_Node *node = alloc_node(vm, ND_FOR);
    node->init = init;
    node->cond = cond;
    node->inc = inc;
    node->then = body;
    return node;
}

JCC_Node *jcc_ast_do_while(JCC *vm, JCC_Node *body, JCC_Node *cond) {
    if (!vm || !cond)
        return NULL;

    JCC_Node *node = alloc_node(vm, ND_DO);
    node->then = body;
    node->cond = cond;
    return node;
}

// ============================================================================
// Function Generation
// ============================================================================

// Helper to create a function type
static Type *make_func_type(JCC *vm, Type *return_type) {
    Type *ty = arena_alloc(&vm->compiler.parser_arena, sizeof(Type));
    memset(ty, 0, sizeof(Type));
    ty->kind = TY_FUNC;
    ty->return_ty = return_type;
    ty->size = 8;
    ty->align = 8;
    return ty;
}

JCC_Obj *jcc_ast_function(JCC *vm, const char *name,
                            JCC_Type *return_type) {
    if (!vm || !name || !return_type)
        return NULL;

    // Check if there's already a forward declaration for this function
    size_t name_len = strlen(name);
    Obj *existing = NULL;

    for (Obj *obj = vm->compiler.globals; obj; obj = obj->next) {
        if (obj->is_function && strlen(obj->name) == name_len &&
            strncmp(obj->name, name, name_len) == 0) {
            existing = obj;
            break;
        }
    }

    if (existing) {
        // Update existing forward declaration to be a definition
        existing->is_definition = true;
        // Update return type if needed
        if (existing->ty && existing->ty->kind == TY_FUNC) {
            existing->ty->return_ty = return_type;
        }
        return existing;
    }

    // Create function type
    Type *func_type = make_func_type(vm, return_type);

    // Create the function object
    Obj *fn = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
    memset(fn, 0, sizeof(Obj));
    fn->name = arena_strdup(vm, name);
    fn->ty = func_type;
    fn->align = 8;
    fn->is_function = true;
    fn->is_definition = true;
    fn->is_static = false;

    // Add to globals list
    fn->next = vm->compiler.globals;
    vm->compiler.globals = fn;

    // Push to scope so it can be found by name
    VarScopeNode *sc =
        arena_alloc(&vm->compiler.parser_arena, sizeof(VarScopeNode));
    memset(sc, 0, sizeof(VarScopeNode));
    sc->name = fn->name;
    sc->name_len = strlen(fn->name);
    sc->var = fn;
    sc->next = vm->compiler.scope->vars;
    vm->compiler.scope->vars = sc;

    return fn;
}

void jcc_ast_function_add_param(JCC *vm, JCC_Obj *fn, const char *name,
                                JCC_Type *type) {
    if (!vm || !fn || !name || !type)
        return;

    // Create parameter local variable
    Obj *param = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
    memset(param, 0, sizeof(Obj));
    param->name = arena_strdup(vm, name);
    param->ty = type;
    param->align = type->align;
    param->is_local = true;
    param->is_param = true;

    // Add to function's params list (append to maintain order)
    // The first param added should be first in the list (offset -1),
    // second param should be second (offset -2), etc.
    // This matches the calling convention where arg1 is at bp[-1], arg2 at
    // bp[-2].

    // Count existing params to compute correct offset
    int param_count = 0;
    for (Obj *p = fn->params; p; p = p->next)
        param_count++;
    param->offset = -(param_count + 1);
    if (fn->params == NULL) {
        fn->params = param;
    } else {
        Obj *last = fn->params;
        while (last->next)
            last = last->next;
        last->next = param;
    }

    // Add parameter type to function type (also append)
    Type *param_type = arena_alloc(&vm->compiler.parser_arena, sizeof(Type));
    memcpy(param_type, type, sizeof(Type));
    param_type->next = NULL;
    if (fn->ty->params == NULL) {
        fn->ty->params = param_type;
    } else {
        Type *last = fn->ty->params;
        while (last->next)
            last = last->next;
        last->next = param_type;
    }
}

void jcc_ast_function_set_body(JCC *vm, JCC_Obj *fn, JCC_Node *body) {
    if (!vm || !fn || !body)
        return;

    // If body is not already a block, wrap it
    if (body->kind != ND_BLOCK) {
        JCC_Node *block = alloc_node(vm, ND_BLOCK);
        block->body = body;
        fn->body = block;
    } else {
        fn->body = body;
    }

    // CRITICAL: Run add_type on the body to assign types to all nodes
    // This is necessary for code generation to work correctly
    add_type(vm, fn->body);

    // Mark as having a definition
    fn->is_definition = true;
}

void jcc_ast_function_set_static(JCC_Obj *fn, bool is_static) {
    if (fn)
        fn->is_static = is_static;
}

void jcc_ast_function_set_inline(JCC_Obj *fn, bool is_inline) {
    if (fn)
        fn->is_inline = is_inline;
}

void jcc_ast_function_set_variadic(JCC_Obj *fn, bool is_variadic) {
    if (fn && fn->ty)
        fn->ty->is_variadic = is_variadic;
}

// ============================================================================
// AST Dump Functions (ticket #58)
// ============================================================================

// ---------------------------------------------------------------------------
// dumpTree: reuse the existing cc_dump_node text renderer
// ---------------------------------------------------------------------------

void jcc_dump_tree(JCC *vm, JCC_Node *node) {
    (void)vm;
    if (!node)
        return;
    cc_dump_node(stdout, node, /*verbose=*/0);
    fflush(stdout);
}

const char *jcc_dump_tree_to_string(JCC *vm, JCC_Node *node) {
    if (!vm || !node)
        return NULL;

    char *buf = NULL;
    size_t size = 0;
    FILE *f = open_memstream(&buf, &size);
    if (!f)
        return NULL;
    cc_dump_node(f, node, /*verbose=*/0);
    fclose(f);

    // Copy into arena so the caller owns a stable pointer
    char *result = arena_alloc(&vm->compiler.parser_arena, size + 1);
    memcpy(result, buf, size);
    result[size] = '\0';
    free(buf);
    return result;
}

// ---------------------------------------------------------------------------
// dumpAstGen: emit jcc_ast_*() builder calls that reconstruct the node
// ---------------------------------------------------------------------------

// Forward declaration (mutually recursive with emit_ast_gen_list)
static void emit_ast_gen(FILE *f, Node *node);

static void emit_ast_gen_list(FILE *f, Node *node) {
    if (!node) {
        fprintf(f, "NULL, 0");
        return;
    }
    // Count nodes
    int n = 0;
    for (Node *p = node; p; p = p->next) n++;
    fprintf(f, "(JCC_Node*[]){");
    for (Node *p = node; p; p = p->next) {
        emit_ast_gen(f, p);
        if (p->next) fprintf(f, ", ");
    }
    fprintf(f, "}, %d", n);
}

static void emit_ast_gen(FILE *f, Node *node) {
    if (!node) {
        fprintf(f, "NULL");
        return;
    }

    switch (node->kind) {
    case ND_NUM:
        if (node->ty && (node->ty->kind == TY_FLOAT ||
                         node->ty->kind == TY_DOUBLE ||
                         node->ty->kind == TY_LDOUBLE))
            fprintf(f, "jcc_ast_float_literal(JCC_VM, %Lg)", node->fval);
        else
            fprintf(f, "jcc_ast_int_literal(JCC_VM, %lld)", (long long)node->val);
        break;
    case ND_VAR:
        if (node->var && node->var->name)
            fprintf(f, "jcc_ast_var_ref(JCC_VM, \"%s\")", node->var->name);
        else
            fprintf(f, "/* VAR(?) */");
        break;
    case ND_ASSIGN:
        fprintf(f, "jcc_ast_assign(JCC_VM, ");
        emit_ast_gen(f, node->lhs);
        fprintf(f, ", ");
        emit_ast_gen(f, node->rhs);
        fprintf(f, ")");
        break;
    case ND_MEMBER:
        fprintf(f, "jcc_ast_member(JCC_VM, ");
        emit_ast_gen(f, node->lhs);
        // Extract member name from the Token stored in member->name
        if (node->member && node->member->name)
            fprintf(f, ", \"%.*s\")", node->member->name->len,
                    node->member->name->loc);
        else
            fprintf(f, ", \"?\")");
        break;
    case ND_FUNCALL:
        fprintf(f, "jcc_ast_funcall(JCC_VM, ");
        emit_ast_gen(f, node->lhs);
        fprintf(f, ", ");
        emit_ast_gen_list(f, node->args);
        fprintf(f, ")");
        break;
    case ND_FOR:
        if (!node->init && !node->inc) {
            // Looks like a while loop
            fprintf(f, "jcc_ast_while(JCC_VM, ");
            emit_ast_gen(f, node->cond);
            fprintf(f, ", ");
            emit_ast_gen(f, node->then);
            fprintf(f, ")");
        } else {
            fprintf(f, "jcc_ast_for(JCC_VM, ");
            emit_ast_gen(f, node->init);
            fprintf(f, ", ");
            emit_ast_gen(f, node->cond);
            fprintf(f, ", ");
            emit_ast_gen(f, node->inc);
            fprintf(f, ", ");
            emit_ast_gen(f, node->then);
            fprintf(f, ")");
        }
        break;
    case ND_DO:
        fprintf(f, "jcc_ast_do_while(JCC_VM, ");
        emit_ast_gen(f, node->then);
        fprintf(f, ", ");
        emit_ast_gen(f, node->cond);
        fprintf(f, ")");
        break;
    case ND_RETURN:
        fprintf(f, "jcc_ast_return(JCC_VM, ");
        emit_ast_gen(f, node->lhs);
        fprintf(f, ")");
        break;
    case ND_IF:
        fprintf(f, "jcc_ast_if(JCC_VM, ");
        emit_ast_gen(f, node->cond);
        fprintf(f, ", ");
        emit_ast_gen(f, node->then);
        fprintf(f, ", ");
        emit_ast_gen(f, node->els);
        fprintf(f, ")");
        break;
    case ND_BLOCK:
        fprintf(f, "jcc_ast_block(JCC_VM, (JCC_Node*[]){");
        {
            int i = 0;
            for (Node *s = node->body; s; s = s->next) {
                if (i++) fprintf(f, ", ");
                emit_ast_gen(f, s);
            }
        }
        fprintf(f, "}, %d)", ({
            int n = 0; for (Node *s = node->body; s; s = s->next) n++; n;
        }));
        break;
    case ND_EXPR_STMT:
        fprintf(f, "jcc_ast_expr_stmt(JCC_VM, ");
        emit_ast_gen(f, node->lhs);
        fprintf(f, ")");
        break;
    case ND_CAST:
        // The target type cannot be fully reconstructed from the AST alone —
        // emit a placeholder comment for the type argument.
        fprintf(f, "jcc_ast_cast(JCC_VM, ");
        emit_ast_gen(f, node->lhs);
        fprintf(f, ", /* type */ NULL)");
        break;
    default:
        // Binary / unary operators: emit via jcc_ast_binary / jcc_ast_unary
        if (node->lhs && node->rhs) {
            fprintf(f, "jcc_ast_binary(JCC_VM, JCC_ND_%s, ",
                    cc_node_kind_name(node->kind));
            emit_ast_gen(f, node->lhs);
            fprintf(f, ", ");
            emit_ast_gen(f, node->rhs);
            fprintf(f, ")");
        } else if (node->lhs) {
            fprintf(f, "jcc_ast_unary(JCC_VM, JCC_ND_%s, ",
                    cc_node_kind_name(node->kind));
            emit_ast_gen(f, node->lhs);
            fprintf(f, ")");
        } else {
            fprintf(f, "/* %s */", cc_node_kind_name(node->kind));
        }
        break;
    }
}

void jcc_dump_ast_gen(JCC *vm, JCC_Node *node) {
    (void)vm;
    if (!node)
        return;
    emit_ast_gen(stdout, node);
    fprintf(stdout, "\n");
    fflush(stdout);
}

const char *jcc_dump_ast_gen_to_string(JCC *vm, JCC_Node *node) {
    if (!vm || !node)
        return NULL;

    char *buf = NULL;
    size_t size = 0;
    FILE *f = open_memstream(&buf, &size);
    if (!f)
        return NULL;
    emit_ast_gen(f, node);
    fclose(f);

    char *result = arena_alloc(&vm->compiler.parser_arena, size + 1);
    memcpy(result, buf, size);
    result[size] = '\0';
    free(buf);
    return result;
}

// ============================================================================
// Quasi-quoting: jcc_quote / jcc_quote_n (ticket #1)
// ============================================================================

// Classify a token as a splice point:
//   returns  N>0 if the token is $N (positional, index N)
//   returns -1   if the token is $$ (incremental sugar)
//   returns  0   if not a splice point
static int quote_splice_kind(Token *tok) {
    if (tok->kind != TK_IDENT || tok->len < 2 || tok->loc[0] != '$')
        return 0;
    // "$$" — incremental sugar
    if (tok->len == 2 && tok->loc[1] == '$')
        return -1;
    // "$N..." — all remaining chars must be decimal digits
    for (int i = 1; i < tok->len; i++)
        if (tok->loc[i] < '0' || tok->loc[i] > '9')
            return 0;
    char buf[32];
    int numlen = tok->len - 1;
    if (numlen > 20)
        return 0; // absurdly large index: not a splice point
    memcpy(buf, tok->loc + 1, numlen);
    buf[numlen] = '\0';
    int n = atoi(buf);
    return n > 0 ? n : 0;
}

// Scan the token stream:
//   - detects mixing of $N and $$ (calls error() on mix)
//   - rewrites the k-th $$ to $k in the token stream (arena-allocated)
//   - returns the maximum index referenced (0 if no splice points)
//   - returns -1 on mixing error
static int quote_scan_and_rewrite(JCC *vm, Token *toks) {
    bool has_positional = false;
    bool has_incremental = false;
    int max_index = 0;
    int incr_counter = 0;

    for (Token *t = toks; t && t->kind != TK_EOF; t = t->next) {
        int k = quote_splice_kind(t);
        if (k == 0) continue;

        if (k == -1) {
            // $$ incremental
            has_incremental = true;
            if (has_positional) {
                error("jcc_quote: cannot mix $N positional and $$ incremental "
                      "splice syntax in one template");
                return -1;
            }
            incr_counter++;
            // Rewrite this token's loc/len to "$<incr_counter>"
            char *newname = arena_format(vm, "$%d", incr_counter);
            t->loc = newname;
            t->len = (int)strlen(newname);
            if (incr_counter > max_index)
                max_index = incr_counter;
        } else {
            // $N positional
            has_positional = true;
            if (has_incremental) {
                error("jcc_quote: cannot mix $N positional and $$ incremental "
                      "splice syntax in one template");
                return -1;
            }
            if (k > max_index)
                max_index = k;
        }
    }
    return max_index;
}

// Push a placeholder variable $k into a Scope's var list (arena-allocated).
// Typed from the corresponding argument node if available, else ty_long.
static Obj *quote_push_placeholder(JCC *vm, Scope *sc, int k,
                                    JCC_Node *arg_node) {
    char *name = arena_format(vm, "$%d", k);
    int name_len = (int)strlen(name);

    // Derive type from the argument node if available
    Type *ty = ty_long; // safe fallback
    if (arg_node) {
        add_type(vm, arg_node);
        if (arg_node->ty)
            ty = arg_node->ty;
    }

    Obj *var = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
    memset(var, 0, sizeof(Obj));
    var->name = name;
    var->ty = ty;
    var->align = ty->align;

    VarScopeNode *snode =
        arena_alloc(&vm->compiler.parser_arena, sizeof(VarScopeNode));
    memset(snode, 0, sizeof(VarScopeNode));
    snode->var = var;
    snode->name = name;
    snode->name_len = name_len;
    snode->next = sc->vars;
    sc->vars = snode;

    return var;
}

// Substitution walk state
typedef struct {
    Obj      *placeholder_vars[64]; // placeholder_vars[i] = Obj for $(i+1)
    JCC_Node **arg_nodes;
    int       n_args;
} QuoteSubstState;

// Walk the parsed tree and replace ND_VAR placeholder nodes with arg nodes.
// Mirrors the transform_node() field traversal in pragma.c.
static JCC_Node *quote_substitute(QuoteSubstState *s, JCC_Node *node) {
    if (!node)
        return NULL;

    // If this is a var reference to one of our placeholders, substitute it
    if (node->kind == ND_VAR && node->var) {
        for (int i = 0; i < s->n_args && i < 64; i++) {
            if (s->placeholder_vars[i] && node->var == s->placeholder_vars[i])
                return s->arg_nodes[i];
        }
    }

    // Recurse into all child fields (same set as transform_node in pragma.c)
    node->lhs  = quote_substitute(s, node->lhs);
    node->rhs  = quote_substitute(s, node->rhs);
    node->cond = quote_substitute(s, node->cond);
    node->then = quote_substitute(s, node->then);
    node->els  = quote_substitute(s, node->els);
    node->init = quote_substitute(s, node->init);
    node->inc  = quote_substitute(s, node->inc);

    // body is a statement chain linked via ->next
    if (node->body) {
        node->body = quote_substitute(s, node->body);
        for (JCC_Node *st = node->body; st; st = st->next)
            if (st->next)
                st->next = quote_substitute(s, st->next);
    }

    // args is an argument chain linked via ->next
    if (node->args) {
        node->args = quote_substitute(s, node->args);
        for (JCC_Node *a = node->args; a && a->next; a = a->next)
            a->next = quote_substitute(s, a->next);
    }

    // switch case chains
    for (JCC_Node *c = node->case_next; c; c = c->case_next)
        c->body = quote_substitute(s, c->body);
    if (node->default_case)
        node->default_case->body =
            quote_substitute(s, node->default_case->body);

    return node;
}

// Determine lexically whether the token stream should be parsed as a statement.
// Uses the first token kind/text and, as a fallback, whether the last
// non-EOF token is ';' (expression-statement form).
static bool quote_is_stmt(Token *tok) {
    if (!tok || tok->kind == TK_EOF)
        return false;

    // Compound statement starting with '{'
    if (tok->kind == TK_PUNCT && tok->len == 1 && tok->loc[0] == '{')
        return true;

    // Statement-initiating keywords
    if (tok->kind == TK_KEYWORD) {
        static const char *stmt_kws[] = {
            "return", "if", "while", "for", "do", "switch",
            "break", "continue", "goto", "case", "default", NULL
        };
        for (int i = 0; stmt_kws[i]; i++)
            if (equal(tok, (char *)stmt_kws[i]))
                return true;
    }

    // If the last non-EOF token is ';' it is an expression-statement
    for (Token *t = tok; t && t->kind != TK_EOF; t = t->next)
        if (!t->next || t->next->kind == TK_EOF)
            if (t->kind == TK_PUNCT && t->len == 1 && t->loc[0] == ';')
                return true;

    return false;
}

// Shared implementation for both public entry points.
static JCC_Node *quote_core(JCC *vm, const char *tmpl,
                             JCC_Node **nodes, int n) {
    if (!vm || !tmpl)
        return NULL;

    // 1. Tokenize the template string
    Token *toks = tokenize_string(vm, (char *)"<quote>", (char *)tmpl);
    if (!toks)
        return NULL;
    convert_pp_tokens(vm, toks);

    // 2. Scan, validate mixing, rewrite $$
    int max_index = quote_scan_and_rewrite(vm, toks);
    if (max_index < 0)
        return NULL; // mixing error already reported

    // 3. Validate count (the array form enforces this; variadic derives n)
    if (max_index > n) {
        error("jcc_quote: template references $%d but only %d argument%s supplied",
              max_index, n, n == 1 ? "" : "s");
        return NULL;
    }

    // 4. Build placeholder scope on the stack
    Scope quote_scope;
    memset(&quote_scope, 0, sizeof(Scope));
    quote_scope.next = vm->compiler.scope;
    vm->compiler.scope = &quote_scope;

    QuoteSubstState subst;
    memset(&subst, 0, sizeof(subst));
    subst.arg_nodes = nodes;
    subst.n_args = (n < 64) ? n : 64;

    for (int k = 1; k <= max_index; k++) {
        JCC_Node *arg = (k - 1 < n) ? nodes[k - 1] : NULL;
        Obj *var = quote_push_placeholder(vm, &quote_scope, k, arg);
        subst.placeholder_vars[k - 1] = var;
    }

    // 5. Parse (auto-detect expr vs stmt)
    Token *rest = NULL;
    JCC_Node *result = NULL;
    if (quote_is_stmt(toks)) {
        result = cc_parse_stmt(vm, &rest, toks);
    } else {
        result = cc_parse_expr(vm, &rest, toks);
    }

    // 6. Restore outer scope unconditionally
    vm->compiler.scope = quote_scope.next;

    if (!result)
        return NULL;

    // 7. Substitute placeholder vars with caller-provided argument nodes
    result = quote_substitute(&subst, result);

    // 8. Re-run add_type so spliced-in types propagate correctly
    add_type(vm, result);

    return result;
}

JCC_Node *jcc_quote_n(JCC *vm, const char *tmpl, JCC_Node **nodes, int count) {
    if (!vm || !tmpl || (!nodes && count > 0))
        return NULL;
    return quote_core(vm, tmpl, nodes, count);
}

JCC_Node *jcc_quote(JCC *vm, const char *tmpl, ...) {
    if (!vm || !tmpl)
        return NULL;

    // Scan the raw template string to derive max splice index without
    // tokenising (avoids double arena allocation in quote_core).
    int max_index = 0;
    int incr_count = 0;

    for (const char *p = tmpl; *p; p++) {
        if (*p != '$') continue;
        const char *q = p + 1;
        if (*q == '$') {
            // $$ incremental
            incr_count++;
            if (incr_count > max_index)
                max_index = incr_count;
            p = q; // skip second $
        } else if (*q >= '1' && *q <= '9') {
            // $N positional
            int n = 0;
            while (*q >= '0' && *q <= '9')
                n = n * 10 + (*q++ - '0');
            if (n > max_index)
                max_index = n;
            p = q - 1; // loop will increment past last digit
        }
        // lone $ followed by non-digit/zero: not a splice point, ignore
    }

    // Collect exactly max_index nodes from va_args
    int n = (max_index < 64) ? max_index : 64;
    JCC_Node *arg_buf[64];
    memset(arg_buf, 0, sizeof(arg_buf));

    va_list ap;
    va_start(ap, tmpl);
    for (int i = 0; i < n; i++)
        arg_buf[i] = va_arg(ap, JCC_Node *);
    va_end(ap);

    return quote_core(vm, tmpl, arg_buf, n);
}
