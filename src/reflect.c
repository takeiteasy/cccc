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
typedef Type _Type;
typedef Node _Node;
typedef Obj _Obj;
typedef Member _Member;
typedef EnumConstant _EnumConstant;
typedef Token _Token;
typedef TypeKind _TypeKind;
typedef NodeKind _NodeKind;

// Global VM pointer for __jcc_get_vm() builtin
// Set during pragma macro execution, cleared after
JCC *__jcc_current_vm = NULL;

// Builtin function to get the current VM context
JCC *__jcc_get_vm(void) { return __jcc_current_vm; }

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

const char *__jcc_gensym(JCC *vm, const char *prefix) {
    if (!vm || !prefix)
        return NULL;
    return arena_format(vm, "%s__%d", prefix,
                        vm->compiler.macro_gensym_counter++);
}

void __jcc_forward_include(JCC *vm, const char *header) {
    if (!vm || !header)
        return;
    StringArray *arr = &vm->compiler.forward_includes;
    for (int i = 0; i < arr->len; i++)
        if (strcmp(arr->data[i], header) == 0)
            return;
    strarray_push(arr, strdup(header));
}

_Token *__jcc_ast_current_token(JCC *vm) {
    return vm ? vm->compiler.macro_call_tok : NULL;
}

_Token *__jcc_ast_synthetic_token(JCC *vm, const char *label) {
    if (!vm)
        return NULL;

    if (!label || !label[0])
        label = "generated";

    char *display_name = arena_format(vm, "<jcc macro: %s>", label);
    char *contents = arena_format(vm, "%s\n", label);
    File *file = new_file(vm, display_name, 0, contents);

    Token *tok = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    memset(tok, 0, sizeof(Token));
    tok->kind = TK_IDENT;
    tok->loc = contents;
    tok->len = (int)strlen(label);
    tok->file = file;
    tok->filename = file->display_name;
    tok->line_no = 1;
    tok->col_no = 1;
    return tok;
}

_Token *__jcc_ast_token_from_node(_Node *node) {
    return node ? node->tok : NULL;
}

_Node *__jcc_ast_set_token(_Node *node, _Token *tok) {
    if (node)
        node->tok = tok;
    return node;
}

_Node *__jcc_ast_copy_location(_Node *dst, _Node *src) {
    if (dst)
        dst->tok = src ? src->tok : NULL;
    return dst;
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

_Type *__jcc_ast_find_type(JCC *vm, const char *name) {
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

bool __jcc_ast_type_exists(JCC *vm, const char *name) {
    return __jcc_ast_find_type(vm, name) != NULL;
}

_Type *__jcc_ast_get_type(JCC *vm, const char *name) {
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

    return __jcc_ast_find_type(vm, name);
}

_TypeKind __jcc_ast_type_kind(_Type *ty) {
    return ty ? ty->kind : TY_VOID;
}

int __jcc_ast_type_size(_Type *ty) { return ty ? ty->size : 0; }

int __jcc_ast_type_align(_Type *ty) { return ty ? ty->align : 0; }

bool __jcc_ast_type_is_unsigned(_Type *ty) {
    return ty ? ty->is_unsigned : false;
}

bool __jcc_ast_type_is_const(_Type *ty) {
    return ty ? ty->is_const : false;
}

_Type *__jcc_ast_type_base(_Type *ty) {
    if (!ty)
        return NULL;
    if (ty->kind != TY_PTR && ty->kind != TY_ARRAY && ty->kind != TY_VLA)
        return NULL;
    return ty->base;
}

int __jcc_ast_type_array_len(_Type *ty) {
    if (!ty || ty->kind != TY_ARRAY)
        return -1;
    return ty->array_len;
}

_Type *__jcc_ast_type_return_type(_Type *ty) {
    if (!ty || ty->kind != TY_FUNC)
        return NULL;
    return ty->return_ty;
}

int __jcc_ast_type_param_count(_Type *ty) {
    if (!ty || ty->kind != TY_FUNC)
        return -1;

    int count = 0;
    for (Type *p = ty->params; p; p = p->next)
        count++;
    return count;
}

_Type *__jcc_ast_type_param_at(_Type *ty, int index) {
    if (!ty || ty->kind != TY_FUNC || index < 0)
        return NULL;

    Type *p = ty->params;
    for (int i = 0; i < index && p; i++)
        p = p->next;
    return p;
}

bool __jcc_ast_type_is_variadic(_Type *ty) {
    if (!ty || ty->kind != TY_FUNC)
        return false;
    return ty->is_variadic;
}

const char *__jcc_ast_type_name(_Type *ty) {
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

_Type *__jcc_ast_make_pointer(JCC *vm, _Type *base) {
    if (!vm || !base)
        return NULL;
    return pointer_to(vm, base);
}

_Type *__jcc_ast_make_array(JCC *vm, _Type *base, int len) {
    if (!vm || !base || len < 0)
        return NULL;
    return array_of(vm, base, len);
}

// ============================================================================
// Enum Reflection
// ============================================================================

int __jcc_ast_enum_count(JCC *vm, _Type *enum_type) {
    (void)vm; // Unused but kept for API consistency
    if (!enum_type || enum_type->kind != TY_ENUM)
        return -1;

    int count = 0;
    for (EnumConstant *ec = enum_type->enum_constants; ec; ec = ec->next)
        count++;
    return count;
}

_EnumConstant *__jcc_ast_enum_at(JCC *vm, _Type *enum_type, int index) {
    (void)vm;
    if (!enum_type || enum_type->kind != TY_ENUM || index < 0)
        return NULL;

    EnumConstant *ec = enum_type->enum_constants;
    for (int i = 0; i < index && ec; i++)
        ec = ec->next;
    return ec;
}

_EnumConstant *__jcc_ast_enum_find(JCC *vm, _Type *enum_type,
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

const char *__jcc_ast_enum_constant_name(_EnumConstant *ec) {
    return ec ? ec->name : NULL;
}

int __jcc_ast_enum_constant_value(_EnumConstant *ec) {
    return ec ? ec->value : 0;
}

const char *__jcc_ast_enum_name(_Type *e) { return __jcc_ast_type_name(e); }

int __jcc_ast_enum_value_count(_Type *e) {
    int count = __jcc_ast_enum_count(NULL, e);
    return count < 0 ? 0 : count;
}

const char *__jcc_ast_enum_value_name(_Type *e, int index) {
    _EnumConstant *ec = __jcc_ast_enum_at(NULL, e, index);
    return __jcc_ast_enum_constant_name(ec);
}

int __jcc_ast_enum_value(_Type *e, int index) {
    _EnumConstant *ec = __jcc_ast_enum_at(NULL, e, index);
    return __jcc_ast_enum_constant_value(ec);
}

// ============================================================================
// Struct/Union Member Introspection
// ============================================================================

int __jcc_ast_struct_member_count(JCC *vm, _Type *struct_type) {
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

_Member *__jcc_ast_struct_member_at(JCC *vm, _Type *struct_type,
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

_Member *__jcc_ast_struct_member_find(JCC *vm, _Type *struct_type,
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

const char *__jcc_ast_member_name(_Member *m) {
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

_Type *__jcc_ast_member_type(_Member *m) { return m ? m->ty : NULL; }

int __jcc_ast_member_offset(_Member *m) { return m ? m->offset : 0; }

bool __jcc_ast_member_is_bitfield(_Member *m) {
    return m ? m->is_bitfield : false;
}

int __jcc_ast_member_bitfield_width(_Member *m) {
    return (m && m->is_bitfield) ? m->bit_width : 0;
}

// ============================================================================
// Global Symbol Introspection
// ============================================================================

_Obj *__jcc_ast_find_global(JCC *vm, const char *name) {
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

int __jcc_ast_global_count(JCC *vm) {
    if (!vm)
        return 0;

    int count = 0;
    for (Obj *obj = vm->compiler.globals; obj; obj = obj->next)
        count++;
    return count;
}

_Obj *__jcc_ast_global_at(JCC *vm, int index) {
    if (!vm || index < 0)
        return NULL;

    Obj *obj = vm->compiler.globals;
    for (int i = 0; i < index && obj; i++)
        obj = obj->next;
    return obj;
}

const char *__jcc_ast_obj_name(_Obj *obj) { return obj ? obj->name : NULL; }

_Type *__jcc_ast_obj_type(_Obj *obj) { return obj ? obj->ty : NULL; }

bool __jcc_ast_obj_is_function(_Obj *obj) {
    return obj ? obj->is_function : false;
}

bool __jcc_ast_obj_is_definition(_Obj *obj) {
    return obj ? obj->is_definition : false;
}

bool __jcc_ast_obj_is_static(_Obj *obj) { return obj ? obj->is_static : false; }

// ============================================================================
// AST Node Construction - Helper
// ============================================================================

static _Node *alloc_node(JCC *vm, _NodeKind kind) {
    _Node *node = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    memset(node, 0, sizeof(Node));
    node->kind = kind;
    node->tok = vm ? vm->compiler.macro_call_tok : NULL;
    return node;
}

// ============================================================================
// AST Node Construction - Literals
// ============================================================================

_Node *__jcc_ast_int_literal(JCC *vm, int64_t value) {
    if (!vm)
        return NULL;

    _Node *node = alloc_node(vm, ND_NUM);
    node->val = value;
    node->ty = ty_long;
    return node;
}

_Node *__jcc_ast_float_literal(JCC *vm, double value) {
    if (!vm)
        return NULL;

    _Node *node = alloc_node(vm, ND_NUM);
    node->fval = value;
    node->ty = ty_double;
    return node;
}

_Node *__jcc_ast_string_literal(JCC *vm, const char *str) {
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
    vm->data_ptr = vm->data_seg + offset;
    if (vm_data_ensure(vm, (long long)(len + 1)) != 0)
        error("codegen: data segment overflow (limit: %d bytes)", vm->poolsize_max);

    var->offset = offset;
    var->init_data = (char *)vm->data_ptr; // Point directly to data segment

    // Copy string to data segment
    memcpy(vm->data_ptr, str, len + 1);
    vm->data_ptr += len + 1;

    // Create a variable reference node
    _Node *node = alloc_node(vm, ND_VAR);
    node->var = var;
    node->ty = ty;
    return node;
}

_Node *__jcc_ast_var_ref(JCC *vm, const char *name) {
    if (!vm || !name)
        return NULL;

    // Look up the variable in current scope
    size_t name_len = strlen(name);

    for (Scope *sc = vm->compiler.scope; sc; sc = sc->next) {
        for (VarScopeNode *node = sc->vars; node; node = node->next) {
            if (node->name_len == (int)name_len &&
                strncmp(node->name, name, name_len) == 0) {
                if (node->var) {
                    node->var->is_used = true;
                    _Node *n = alloc_node(vm, ND_VAR);
                    n->var = node->var;
                    n->ty = node->var->ty;
                    return n;
                }
            }
        }
    }

    // Also check globals
    Obj *global = __jcc_ast_find_global(vm, name);
    if (global) {
        global->is_used = true;
        _Node *n = alloc_node(vm, ND_VAR);
        n->var = global;
        n->ty = global->ty;
        return n;
    }

    return NULL;
}

_Node *__jcc_ast_param_ref(JCC *vm, _Obj *fn, const char *name) {
    if (!vm || !fn || !name)
        return NULL;

    // Find the parameter in the function's params list
    size_t name_len = strlen(name);
    for (Obj *param = fn->params; param; param = param->next) {
        if (strlen(param->name) == name_len &&
            strncmp(param->name, name, name_len) == 0) {
            param->is_used = true;
            _Node *n = alloc_node(vm, ND_VAR);
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

_Node *__jcc_ast_binary(JCC *vm, _NodeKind op, _Node *left,
                            _Node *right) {
    if (!vm || !left || !right)
        return NULL;

    _Node *node = alloc_node(vm, op);
    node->lhs = left;
    node->rhs = right;
    // Type will be determined by add_type pass
    return node;
}

_Node *__jcc_ast_unary(JCC *vm, _NodeKind op, _Node *operand) {
    if (!vm || !operand)
        return NULL;

    _Node *node = alloc_node(vm, op);
    node->lhs = operand;
    return node;
}

_Node *__jcc_ast_cast(JCC *vm, _Node *expr, _Type *target_type) {
    if (!vm || !expr || !target_type)
        return NULL;

    _Node *node = alloc_node(vm, ND_CAST);
    node->lhs = expr;
    node->ty = target_type;
    return node;
}

// ============================================================================
// AST Node Construction - Statements
// ============================================================================

_Node *__jcc_ast_return(JCC *vm, _Node *expr) {
    if (!vm)
        return NULL;

    _Node *node = alloc_node(vm, ND_RETURN);
    node->lhs = expr;
    return node;
}

_Node *__jcc_ast_block(JCC *vm, _Node **stmts, int count) {
    if (!vm)
        return NULL;

    _Node *node = alloc_node(vm, ND_BLOCK);

    // Link statements together
    Node head = {};
    Node *cur = &head;
    for (int i = 0; i < count && stmts[i]; i++) {
        cur = cur->next = stmts[i];
    }
    node->body = head.next;
    return node;
}

_Node *__jcc_ast_if(JCC *vm, _Node *cond, _Node *then_body,
                        _Node *else_body) {
    if (!vm || !cond)
        return NULL;

    _Node *node = alloc_node(vm, ND_IF);
    node->cond = cond;
    node->then = then_body;
    node->els = else_body;
    return node;
}

_Node *__jcc_ast_switch(JCC *vm, _Node *cond) {
    if (!vm || !cond)
        return NULL;

    _Node *node = alloc_node(vm, ND_SWITCH);
    node->cond = cond;
    node->case_next = NULL;
    node->default_case = NULL;
    return node;
}

void __jcc_ast_switch_add_case(JCC *vm, _Node *switch_node, _Node *value,
                                _Node *body) {
    if (!vm || !switch_node || !value || !body)
        return;

    if (switch_node->kind != ND_SWITCH)
        return;

    // Create a case node
    _Node *case_node = alloc_node(vm, ND_CASE);
    case_node->begin = value->val; // Assuming value is a numeric literal
    case_node->end = value->val;
    case_node->body = body;

    // Add to switch's case list
    case_node->case_next = switch_node->case_next;
    switch_node->case_next = case_node;
}

void __jcc_ast_switch_set_default(JCC *vm, _Node *switch_node,
                                    _Node *body) {
    if (!vm || !switch_node || !body)
        return;

    if (switch_node->kind != ND_SWITCH)
        return;

    _Node *def = alloc_node(vm, ND_CASE);
    def->body = body;
    switch_node->default_case = def;
}

// ============================================================================
// AST Node Construction - Declarations
// ============================================================================

_Node *__jcc_ast_expr_stmt(JCC *vm, _Node *expr) {
    if (!vm)
        return NULL;

    _Node *node = alloc_node(vm, ND_EXPR_STMT);
    node->lhs = expr;
    return node;
}

// ============================================================================
// Macro Diagnostics (ticket #78)
// ============================================================================

void __jcc_macro_error_at(JCC *vm, _Node *node, const char *fmt, ...) {
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

void __jcc_macro_warning_at(JCC *vm, _Node *node, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (node && node->tok) {
        warn_tok(vm, node->tok, JCC_WARN_JCC_MACRO, "%s", buf);
    } else if (vm && (vm->compiler.warnings & JCC_WARN_JCC_MACRO)) {
        bool as_error = (vm->warnings_as_errors &&
                         !(vm->compiler.warning_no_errors & JCC_WARN_JCC_MACRO)) ||
                        (vm->compiler.warning_errors & JCC_WARN_JCC_MACRO);
        if (as_error)
            error("%s", buf);
        else
            fprintf(stderr, "warning: %s [-Wjcc-macro]\n", buf);
    }
}

// ============================================================================
// AST Node Construction - Local Variable Injection (ticket #77)
// ============================================================================

// Allocate a local Obj and prepend it to the current function's locals list.
// Injected variables receive stack offsets later when cc_compile runs.
static _Node *make_local_var_node(JCC *vm, char *name, _Type *ty) {
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

    _Node *node = alloc_node(vm, ND_VAR);
    node->var = var;
    return node;
}

_Node *__jcc_ast_local_var(JCC *vm, const char *name, _Type *ty) {
    if (!vm || !name || !ty)
        return NULL;

    char *arena_name = arena_format(vm, "%s", name);
    return make_local_var_node(vm, arena_name, ty);
}

_Node *__jcc_ast_local_var_unique(JCC *vm, _Type *ty) {
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

_Node *__jcc_ast_assign(JCC *vm, _Node *target, _Node *value) {
    if (!vm || !target || !value)
        return NULL;

    _Node *node = alloc_node(vm, ND_ASSIGN);
    node->lhs = target;
    node->rhs = value;
    return node;
}

_Node *__jcc_ast_member(JCC *vm, _Node *obj, const char *name) {
    if (!vm || !obj || !name)
        return NULL;

    // Ensure lhs type is computed (mirrors struct_ref in parse.c:3796)
    add_type(vm, obj);

    Type *ty = obj->ty;
    if (!ty || (ty->kind != TY_STRUCT && ty->kind != TY_UNION))
        return NULL;

    Member *mem = (Member *)__jcc_ast_struct_member_find(vm, ty, name);
    if (!mem)
        return NULL;

    _Node *node = alloc_node(vm, ND_MEMBER);
    node->lhs = obj;
    node->member = mem;
    return node;
}

_Node *__jcc_ast_funcall(JCC *vm, _Node *callee, _Node **args, int n) {
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

    _Node *node = alloc_node(vm, ND_FUNCALL);
    node->lhs = callee;
    node->func_ty = ty;
    node->ty = ty->return_ty;

    // Chain argument nodes via ->next (mirrors __jcc_ast_block pattern)
    Node head = {};
    Node *cur = &head;
    for (int i = 0; i < n && args[i]; i++)
        cur = cur->next = args[i];
    node->args = head.next;
    return node;
}

// __jcc_ast_while: while(cond) body — represented as ND_FOR with init/inc NULL
_Node *__jcc_ast_while(JCC *vm, _Node *cond, _Node *body) {
    if (!vm || !cond)
        return NULL;

    _Node *node = alloc_node(vm, ND_FOR);
    node->cond = cond;
    node->then = body;
    // node->init and node->inc left NULL — this is a while loop
    return node;
}

_Node *__jcc_ast_for(JCC *vm, _Node *init, _Node *cond,
                       _Node *inc, _Node *body) {
    if (!vm)
        return NULL;

    _Node *node = alloc_node(vm, ND_FOR);
    node->init = init;
    node->cond = cond;
    node->inc = inc;
    node->then = body;
    return node;
}

_Node *__jcc_ast_do_while(JCC *vm, _Node *body, _Node *cond) {
    if (!vm || !cond)
        return NULL;

    _Node *node = alloc_node(vm, ND_DO);
    node->then = body;
    node->cond = cond;
    return node;
}

// ============================================================================
// AST Node Construction - New Expression Builders (ticket #171)
// ============================================================================

// align_to: round n up to the nearest multiple of align (mirrors parse.c)
static inline int reflect_align_to(int n, int align) {
    return (n + align - 1) / align * align;
}

// _AST_COND(cond, then, else) — ternary ?: expression
_Node *__jcc_ast_cond(JCC *vm, _Node *cond, _Node *then_expr,
                          _Node *else_expr) {
    if (!vm || !cond || !then_expr || !else_expr)
        return NULL;
    _Node *node = alloc_node(vm, ND_COND);
    node->cond = cond;
    node->then = then_expr;
    node->els = else_expr;
    // Type is resolved by add_type pass
    return node;
}

// _AST_NULL() — typed null pointer: (void *)0
_Node *__jcc_ast_null(JCC *vm) {
    if (!vm)
        return NULL;
    _Node *zero = __jcc_ast_int_literal(vm, 0);
    if (!zero)
        return NULL;
    _Node *node = alloc_node(vm, ND_CAST);
    node->lhs = zero;
    node->ty = pointer_to(vm, ty_void);
    return node;
}

// _AST_SIZEOF_TYPE(ty) — sizeof(ty) as a compile-time integer literal
_Node *__jcc_ast_sizeof_type(JCC *vm, _Type *ty) {
    if (!vm || !ty)
        return NULL;
    return __jcc_ast_int_literal(vm, ty->size);
}

// _AST_ALIGNOF_TYPE(ty) — _Alignof(ty) as a compile-time integer literal
_Node *__jcc_ast_alignof_type(JCC *vm, _Type *ty) {
    if (!vm || !ty)
        return NULL;
    return __jcc_ast_int_literal(vm, ty->align);
}

// _AST_SIZEOF_EXPR(expr) — sizeof(expr): run add_type then return the size
_Node *__jcc_ast_sizeof_expr(JCC *vm, _Node *expr) {
    if (!vm || !expr)
        return NULL;
    add_type(vm, expr);
    if (!expr->ty)
        return NULL;
    return __jcc_ast_int_literal(vm, expr->ty->size);
}

// _AST_SUBSCRIPT(arr, idx) — arr[idx], desugared as *(arr + idx * sizeof(*arr))
// Mirrors new_add() in parse.c: the index must be pre-scaled by the element
// size so that codegen emits a plain integer ADD (it does not scale internally).
_Node *__jcc_ast_subscript(JCC *vm, _Node *arr, _Node *idx) {
    if (!vm || !arr || !idx)
        return NULL;

    // Resolve types so we can inspect pointer/array base
    add_type(vm, arr);
    add_type(vm, idx);

    // Canonicalize: pointer must be on the left (handle idx + arr too)
    _Node *ptr = arr;
    _Node *num = idx;
    if (ptr->ty && !ptr->ty->base && num->ty && num->ty->base) {
        ptr = idx;
        num = arr;
    }

    // Scale index by element size for pointer arithmetic
    if (ptr->ty && ptr->ty->base) {
        int elem_size = ptr->ty->base->size;
        // Build: num * elem_size
        _Node *scale = alloc_node(vm, ND_NUM);
        scale->val = elem_size;
        scale->ty = ty_long;
        _Node *scaled = alloc_node(vm, ND_MUL);
        scaled->lhs = num;
        scaled->rhs = scale;
        // Build: ptr + scaled
        _Node *add = alloc_node(vm, ND_ADD);
        add->lhs = ptr;
        add->rhs = scaled;
        add->ty = ptr->ty; // pointer result type
        // Dereference: *(ptr + scaled)
        _Node *deref = alloc_node(vm, ND_DEREF);
        deref->lhs = add;
        return deref;
    }

    // Fallback: integer subscript (unusual but safe)
    _Node *add = alloc_node(vm, ND_ADD);
    add->lhs = ptr;
    add->rhs = num;
    _Node *deref = alloc_node(vm, ND_DEREF);
    deref->lhs = add;
    return deref;
}

// _AST_COMMA(lhs, rhs) — comma expression: evaluate lhs, discard, yield rhs
_Node *__jcc_ast_comma(JCC *vm, _Node *lhs, _Node *rhs) {
    return __jcc_ast_binary(vm, ND_COMMA, lhs, rhs);
}

// ============================================================================
// AST Type Construction - Qualified Types (ticket #171)
// ============================================================================

// _AST_MAKE_CONST(ty) — return a const-qualified copy of ty
_Type *__jcc_ast_make_const(JCC *vm, _Type *ty) {
    if (!vm || !ty)
        return NULL;
    Type *result = copy_type(vm, ty);
    result->is_const = true;
    return result;
}

// _AST_MAKE_VOLATILE(ty) — return a volatile-qualified copy of ty
_Type *__jcc_ast_make_volatile(JCC *vm, _Type *ty) {
    if (!vm || !ty)
        return NULL;
    Type *result = copy_type(vm, ty);
    result->is_volatile = true;
    return result;
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

_Obj *__jcc_ast_function(JCC *vm, const char *name,
                            _Type *return_type) {
    if (!vm || !name || !return_type)
        return NULL;

    // Check if there's already a forward declaration for this function
    size_t name_len = strlen(name);
    Obj *existing = NULL;

    Obj *lists[] = { vm->compiler.globals, vm->compiler.macro_globals };
    for (int i = 0; i < 2 && !existing; i++) {
        for (Obj *obj = lists[i]; obj; obj = obj->next) {
            if (obj->is_function && strlen(obj->name) == name_len &&
                strncmp(obj->name, name, name_len) == 0) {
                existing = obj;
                break;
            }
        }
    }

    if (existing) {
        if (existing->is_definition)
            error("expected unique generated function name, got existing "
                  "definition '%s'",
                  name);

        // Update existing forward declaration to be a definition
        existing->is_definition = true;
        existing->is_macro_generated = true;
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
    fn->is_macro_generated = true;

    // Add to globals list. The function is not made visible to the parser
    // until source declares it, inline macro prototype synthesis declares it,
    // or __jcc_ast_publish() publishes it explicitly.
    fn->next = vm->compiler.globals;
    vm->compiler.globals = fn;

    return fn;
}

static _Node *reflect_noop_node(JCC *vm) {
    _Node *noop = alloc_node(vm, ND_NULL_EXPR);
    noop->ty = ty_void;
    return noop;
}

_Node *__jcc_ast_publish(JCC *vm, _Obj *obj, _Token *tok) {
    if (!vm || !obj || !obj->ty)
        return NULL;
    if (!vm->compiler.scope)
        return NULL;

    if (tok)
        obj->tok = tok;

    int name_len = (int)strlen(obj->name);
    for (Scope *sc = vm->compiler.scope; sc; sc = sc->next) {
        for (VarScopeNode *node = sc->vars; node; node = node->next) {
            if (node->name_len != name_len ||
                strncmp(node->name, obj->name, name_len) != 0)
                continue;

            if (node->var == obj)
                return reflect_noop_node(vm);

            if (obj->is_function && node->var && node->var->is_function)
                return reflect_noop_node(vm);

            if (tok)
                error_tok(vm, tok,
                          "conflicting declaration for generated %s '%s'",
                          obj->is_function ? "function" : "global variable",
                          obj->name);
            error("conflicting declaration for generated %s '%s'",
                  obj->is_function ? "function" : "global variable",
                  obj->name);
        }
    }

    VarScopeNode *decl =
        arena_alloc(&vm->compiler.parser_arena, sizeof(VarScopeNode));
    memset(decl, 0, sizeof(VarScopeNode));
    decl->name = obj->name;
    decl->name_len = name_len;
    decl->var = obj;
    decl->next = vm->compiler.scope->vars;
    vm->compiler.scope->vars = decl;
    hashmap_put2_borrowed(&vm->compiler.scope->var_map, decl->name, decl->name_len, decl);

    return reflect_noop_node(vm);
}

_Node *__jcc_ast_publish_type(JCC *vm, _Type *ty, _Token *tok) {
    (void)ty;
    (void)tok;
    if (!vm)
        return NULL;
    return reflect_noop_node(vm);
}

_Node *__jcc_ast_forward_declare(JCC *vm, _Obj *fn) {
    if (!vm || !fn || !fn->is_function || !fn->ty || fn->ty->kind != TY_FUNC)
        return NULL;
    return __jcc_ast_publish(vm, fn, NULL);
}

void __jcc_ast_function_add_param(JCC *vm, _Obj *fn, const char *name,
                                _Type *type) {
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

void __jcc_ast_function_set_body(JCC *vm, _Obj *fn, _Node *body) {
    if (!vm || !fn || !body)
        return;

    // If body is not already a block, wrap it
    if (body->kind != ND_BLOCK) {
        _Node *block = alloc_node(vm, ND_BLOCK);
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

void __jcc_ast_function_set_static(_Obj *fn, bool is_static) {
    if (fn)
        fn->is_static = is_static;
}

void __jcc_ast_function_set_inline(_Obj *fn, bool is_inline) {
    if (fn)
        fn->is_inline = is_inline;
}

void __jcc_ast_function_set_variadic(_Obj *fn, bool is_variadic) {
    if (fn && fn->ty)
        fn->ty->is_variadic = is_variadic;
}

// _AST_FUNCTION_PROTOTYPE(name, ret) — create a forward declaration (no body).
// The same params API (_AST_FUNCTION_ADD_PARAM) applies; use
// _AST_PUBLISH to make it visible in scope.
_Obj *__jcc_ast_function_prototype(JCC *vm, const char *name,
                                    _Type *return_type) {
    if (!vm || !name || !return_type)
        return NULL;

    size_t name_len = strlen(name);

    // If a forward-declaration or prototype already exists, return it.
    Obj *lists[] = { vm->compiler.globals, vm->compiler.macro_globals };
    for (int i = 0; i < 2; i++) {
        for (Obj *obj = lists[i]; obj; obj = obj->next) {
            if (obj->is_function && strlen(obj->name) == name_len &&
                strncmp(obj->name, name, name_len) == 0) {
                return obj;
            }
        }
    }

    Type *func_type = make_func_type(vm, return_type);

    Obj *fn = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
    memset(fn, 0, sizeof(Obj));
    fn->name = arena_strdup(vm, name);
    fn->ty = func_type;
    fn->align = 8;
    fn->is_function = true;
    fn->is_definition = false; // prototype, no body
    fn->is_static = false;
    fn->is_macro_generated = true;

    fn->next = vm->compiler.globals;
    vm->compiler.globals = fn;
    return fn;
}

// ============================================================================
// Global Variable Generation (ticket #152)
// ============================================================================

// Create a new named global variable.  The type determines layout; use
// __jcc_ast_make_array(vm, char_ty, len) to get a char[len] type so that
// the codegen init_data copy (codegen.c) copies the right number of bytes.
_Obj *__jcc_ast_global_var(JCC *vm, const char *name, _Type *ty) {
    if (!vm || !name || !ty)
        return NULL;

    size_t name_len = strlen(name);

    // Reuse an existing forward declaration if present (same logic as
    // __jcc_ast_function).
    for (Obj *obj = vm->compiler.globals; obj; obj = obj->next) {
        if (!obj->is_function && strlen(obj->name) == name_len &&
            strncmp(obj->name, name, name_len) == 0) {
            if (obj->is_definition)
                error("expected unique generated global name, got existing "
                      "definition '%s'", name);
            obj->is_definition = true;
            obj->is_macro_generated = true;
            obj->ty = ty;
            obj->align = ty->align;
            return obj;
        }
    }

    Obj *var = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
    memset(var, 0, sizeof(Obj));
    var->name = arena_strdup(vm, name);
    var->ty = ty;
    var->align = ty->align;
    var->is_function = false;
    var->is_definition = true;
    var->is_static = false; // default: external linkage; call _set_static to change
    var->is_macro_generated = true;

    var->next = vm->compiler.globals;
    vm->compiler.globals = var;
    return var;
}

// Set the initial data for a global variable.  data[0..len-1] is copied into
// the arena.  The variable's type must have ty->size == len; use
// _AST_MAKE_ARRAY(char_ty, len) to ensure the sizes match.
void __jcc_ast_global_var_set_init_data(JCC *vm, _Obj *var,
                                        const char *data, int len) {
    if (!vm || !var || !data || len <= 0)
        return;
    char *buf = arena_alloc(&vm->compiler.parser_arena, len);
    memcpy(buf, data, len);
    var->init_data = buf;
}

// Set the static flag on a generated global (true = internal linkage).
void __jcc_ast_global_var_set_static(_Obj *var, bool is_static) {
    if (var)
        var->is_static = is_static;
}

// ============================================================================
// Function-building context (ticket #148): _AST_WITH_FN support
// ============================================================================

// Small internal save-stack so macros can push/pop current_fn cleanly.
// Macro execution is single-threaded so a module-level stack is fine.
#define JCC_FN_CONTEXT_STACK_DEPTH 16
static Obj *_fn_context_stack[JCC_FN_CONTEXT_STACK_DEPTH];
static int  _fn_context_depth = 0;

// Push a new function context: vm->compiler.current_fn is saved on the stack
// and replaced by fn.  Inside this context, _QUOTE("return x;") will find
// current_fn and apply the correct implicit return-type cast.
void __jcc_ast_push_fn(JCC *vm, _Obj *fn) {
    if (!vm)
        return;
    if (_fn_context_depth >= JCC_FN_CONTEXT_STACK_DEPTH) {
        error("__jcc_ast_push_fn: function context stack overflow (max %d)",
              JCC_FN_CONTEXT_STACK_DEPTH);
        return;
    }
    _fn_context_stack[_fn_context_depth++] = vm->compiler.current_fn;
    vm->compiler.current_fn = fn;
}

// Pop the most recently pushed function context, restoring current_fn to its
// previous value.
void __jcc_ast_pop_fn(JCC *vm) {
    if (!vm)
        return;
    if (_fn_context_depth <= 0) {
        // Unmatched pop — reset to NULL rather than crashing.
        vm->compiler.current_fn = NULL;
        return;
    }
    vm->compiler.current_fn = _fn_context_stack[--_fn_context_depth];
}

// ============================================================================
// AST Dump Functions (ticket #58)
// ============================================================================

// ---------------------------------------------------------------------------
// dumpTree: reuse the existing cc_dump_node text renderer
// ---------------------------------------------------------------------------

void __jcc_dump_tree(JCC *vm, _Node *node) {
    (void)vm;
    if (!node)
        return;
    cc_dump_node(stdout, node, /*verbose=*/0);
    fflush(stdout);
}

const char *__jcc_dump_tree_to_string(JCC *vm, _Node *node) {
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
// dumpAstGen: emit __jcc_ast_*() builder calls that reconstruct the node
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
    fprintf(f, "(_Node*[]){");
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
            fprintf(f, "__jcc_ast_float_literal(_VM, %Lg)", node->fval);
        else
            fprintf(f, "__jcc_ast_int_literal(_VM, %lld)", (long long)node->val);
        break;
    case ND_VAR:
        if (node->var && node->var->name)
            fprintf(f, "__jcc_ast_var_ref(_VM, \"%s\")", node->var->name);
        else
            fprintf(f, "/* VAR(?) */");
        break;
    case ND_ASSIGN:
        fprintf(f, "__jcc_ast_assign(_VM, ");
        emit_ast_gen(f, node->lhs);
        fprintf(f, ", ");
        emit_ast_gen(f, node->rhs);
        fprintf(f, ")");
        break;
    case ND_MEMBER:
        fprintf(f, "__jcc_ast_member(_VM, ");
        emit_ast_gen(f, node->lhs);
        // Extract member name from the Token stored in member->name
        if (node->member && node->member->name)
            fprintf(f, ", \"%.*s\")", node->member->name->len,
                    node->member->name->loc);
        else
            fprintf(f, ", \"?\")");
        break;
    case ND_FUNCALL:
        fprintf(f, "__jcc_ast_funcall(_VM, ");
        emit_ast_gen(f, node->lhs);
        fprintf(f, ", ");
        emit_ast_gen_list(f, node->args);
        fprintf(f, ")");
        break;
    case ND_FOR:
        if (!node->init && !node->inc) {
            // Looks like a while loop
            fprintf(f, "__jcc_ast_while(_VM, ");
            emit_ast_gen(f, node->cond);
            fprintf(f, ", ");
            emit_ast_gen(f, node->then);
            fprintf(f, ")");
        } else {
            fprintf(f, "__jcc_ast_for(_VM, ");
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
        fprintf(f, "__jcc_ast_do_while(_VM, ");
        emit_ast_gen(f, node->then);
        fprintf(f, ", ");
        emit_ast_gen(f, node->cond);
        fprintf(f, ")");
        break;
    case ND_RETURN:
        fprintf(f, "__jcc_ast_return(_VM, ");
        emit_ast_gen(f, node->lhs);
        fprintf(f, ")");
        break;
    case ND_IF:
        fprintf(f, "__jcc_ast_if(_VM, ");
        emit_ast_gen(f, node->cond);
        fprintf(f, ", ");
        emit_ast_gen(f, node->then);
        fprintf(f, ", ");
        emit_ast_gen(f, node->els);
        fprintf(f, ")");
        break;
    case ND_BLOCK:
        fprintf(f, "__jcc_ast_block(_VM, (_Node*[]){");
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
        fprintf(f, "__jcc_ast_expr_stmt(_VM, ");
        emit_ast_gen(f, node->lhs);
        fprintf(f, ")");
        break;
    case ND_CAST:
        // The target type cannot be fully reconstructed from the AST alone —
        // emit a placeholder comment for the type argument.
        fprintf(f, "__jcc_ast_cast(_VM, ");
        emit_ast_gen(f, node->lhs);
        fprintf(f, ", /* type */ NULL)");
        break;
    default:
        // Binary / unary operators: emit via __jcc_ast_binary / __jcc_ast_unary
        if (node->lhs && node->rhs) {
            fprintf(f, "__jcc_ast_binary(_VM, _%s, ",
                    cc_node_kind_name(node->kind));
            emit_ast_gen(f, node->lhs);
            fprintf(f, ", ");
            emit_ast_gen(f, node->rhs);
            fprintf(f, ")");
        } else if (node->lhs) {
            fprintf(f, "__jcc_ast_unary(_VM, _%s, ",
                    cc_node_kind_name(node->kind));
            emit_ast_gen(f, node->lhs);
            fprintf(f, ")");
        } else {
            fprintf(f, "/* %s */", cc_node_kind_name(node->kind));
        }
        break;
    }
}

void __jcc_dump_ast_gen(JCC *vm, _Node *node) {
    (void)vm;
    if (!node)
        return;
    emit_ast_gen(stdout, node);
    fprintf(stdout, "\n");
    fflush(stdout);
}

const char *__jcc_dump_ast_gen_to_string(JCC *vm, _Node *node) {
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
// Quasi-quoting: __jcc_quote / __jcc_quote_n (ticket #1)
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
//   - detects mixing of $N/$@N positional and $$/$@ incremental (error on mix)
//   - rewrites the k-th $$ or $@ to a canonical $k or $@k token
//     (collapsing the multi-token $@N / $@ sequences in-place)
//   - sets *splice_mask (bit k-1) for each $@k splice index seen
//   - returns the maximum index referenced (0 if no splice points)
//   - returns -1 on mixing error
static int quote_scan_and_rewrite(JCC *vm, Token *toks, uint64_t *splice_mask) {
    bool has_positional = false;
    bool has_incremental = false;
    int max_index = 0;
    int incr_counter = 0;
    if (splice_mask) *splice_mask = 0;

    for (Token *t = toks; t && t->kind != TK_EOF; t = t->next) {
        // Check for $@ splice syntax: a lone '$' ident followed by '@' punct.
        // $@N  → positional splice (index N)
        // $@   → incremental splice (next sequential index)
        if (t->kind == TK_IDENT && t->len == 1 && t->loc[0] == '$' &&
            t->next && t->next->kind == TK_PUNCT &&
            t->next->len == 1 && t->next->loc[0] == '@') {
            Token *at_tok  = t->next;
            Token *num_tok = at_tok->next;
            int k;

            if (num_tok && num_tok->kind == TK_NUM) {
                // $@N positional splice
                has_positional = true;
                if (has_incremental) {
                    error("__jcc_quote: cannot mix positional ($@N) and incremental "
                          "($@ / $$) splice syntax in one template");
                    return -1;
                }
                // Parse the index from the number token's text
                int numlen = (num_tok->len < 20) ? num_tok->len : 20;
                char buf[32];
                memcpy(buf, num_tok->loc, numlen);
                buf[numlen] = '\0';
                k = atoi(buf);
                if (k <= 0) {
                    error("__jcc_quote: $@0 is not a valid splice index "
                          "(splice indices start at 1)");
                    return -1;
                }
                // Collapse three tokens ($ @ N) into one named $@k
                char *newname = arena_format(vm, "$@%d", k);
                t->loc = newname;
                t->len = (int)strlen(newname);
                t->next = num_tok->next; // skip @ and N tokens
            } else {
                // $@ incremental splice
                has_incremental = true;
                if (has_positional) {
                    error("__jcc_quote: cannot mix positional ($@N) and incremental "
                          "($@ / $$) splice syntax in one template");
                    return -1;
                }
                incr_counter++;
                k = incr_counter;
                // Collapse two tokens ($ @) into one named $@k
                char *newname = arena_format(vm, "$@%d", k);
                t->loc = newname;
                t->len = (int)strlen(newname);
                t->next = at_tok->next; // skip @ token
            }

            if (k > max_index) max_index = k;
            if (splice_mask && k >= 1 && k <= 64)
                *splice_mask |= (uint64_t)1 << (k - 1);
            continue;
        }

        // Regular $N / $$ scalar splice points
        int k = quote_splice_kind(t);
        if (k == 0) continue;

        if (k == -1) {
            // $$ incremental
            has_incremental = true;
            if (has_positional) {
                error("__jcc_quote: cannot mix $N positional and $$ incremental "
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
                error("__jcc_quote: cannot mix $N positional and $$ incremental "
                      "splice syntax in one template");
                return -1;
            }
            if (k > max_index)
                max_index = k;
        }
    }
    return max_index;
}

// Push a placeholder variable with the given name into a Scope's var list
// (arena-allocated).  Typed from the corresponding argument node if
// available, else ty_long.
static Obj *quote_push_placeholder(JCC *vm, Scope *sc, char *name,
                                    _Node *arg_node) {
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
    Obj      *splice_vars[64];      // splice_vars[i]      = Obj for $@(i+1)
    _Node   **arg_nodes;
    int       n_args;
} QuoteSubstState;

// If stmt is an ND_EXPR_STMT whose sole expression is a reference to a splice
// placeholder $@k, return the caller's node chain for index k.  Otherwise NULL.
static _Node *splice_chain_for(QuoteSubstState *s, _Node *stmt);

// Like splice_chain_for but for a bare expression-position arg (ND_VAR directly,
// not wrapped in ND_EXPR_STMT).  Returns true and sets *out_chain if arg is a
// $@k placeholder; false otherwise.  *out_chain may be NULL for an empty splice.
static bool splice_chain_for_arg(QuoteSubstState *s, _Node *arg, _Node **out_chain) {
    if (!arg || arg->kind != ND_VAR || !arg->var) return false;
    for (int i = 0; i < s->n_args && i < 64; i++) {
        if (s->splice_vars[i] && arg->var == s->splice_vars[i]) {
            *out_chain = s->arg_nodes[i];
            return true;
        }
    }
    return false;
}

static _Node *splice_chain_for(QuoteSubstState *s, _Node *stmt) {
    if (!stmt || stmt->kind != ND_EXPR_STMT) return NULL;
    _Node *inner = stmt->lhs;
    if (!inner || inner->kind != ND_VAR || !inner->var) return NULL;
    for (int i = 0; i < s->n_args && i < 64; i++) {
        if (s->splice_vars[i] && inner->var == s->splice_vars[i])
            return s->arg_nodes[i]; // chain head (may be NULL = empty splice)
    }
    return NULL; // not a splice placeholder
}

// Walk the parsed tree and replace ND_VAR placeholder nodes with arg nodes.
// Mirrors the transform_node() field traversal in pragma.c.
// Splice placeholders ($@k) are expanded in statement-list positions (body).
// Using $@k outside a statement-list position is a compile-time error.
static _Node *quote_substitute(QuoteSubstState *s, _Node *node) {
    if (!node)
        return NULL;

    // If this is a var reference to one of our placeholders, substitute it
    if (node->kind == ND_VAR && node->var) {
        // Scalar placeholder $k → 1:1 replacement
        for (int i = 0; i < s->n_args && i < 64; i++) {
            if (s->placeholder_vars[i] && node->var == s->placeholder_vars[i])
                return s->arg_nodes[i];
        }
        // Splice placeholder $@k used as a sub-expression → error.
        // (Direct arg-list position is handled in the ->args block below.)
        for (int i = 0; i < s->n_args && i < 64; i++) {
            if (s->splice_vars[i] && node->var == s->splice_vars[i]) {
                error("__jcc_quote: $@%d is only valid in statement-list "
                      "position (inside a block { }) or as a direct call "
                      "argument; cannot be used as a sub-expression", i + 1);
                return node; // return unchanged to avoid a NULL crash
            }
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

    // body is a statement chain linked via ->next.
    // Splice placeholders in body position expand to N statements.
    if (node->body) {
        Node head_val = {};
        Node *cur = &head_val;
        for (_Node *st = node->body; st; ) {
            _Node *next_st = st->next;
            st->next = NULL; // isolate before recursing

            _Node *chain = splice_chain_for(s, st);
            if (chain) {
                // Append the entire caller-provided chain
                _Node *tail = chain;
                while (tail->next) tail = tail->next;
                cur->next = chain;
                cur = tail;
            } else {
                _Node *sub = quote_substitute(s, st);
                if (sub) { cur->next = sub; cur = sub; }
            }
            st = next_st;
        }
        cur->next = NULL;
        node->body = head_val.next;
    }

    // args is an argument chain linked via ->next.
    // Splice placeholders ($@k) in direct arg position expand to N expressions.
    if (node->args) {
        bool has_splice = false;
        for (_Node *a = node->args; a; a = a->next) {
            _Node *dummy;
            if (splice_chain_for_arg(s, a, &dummy)) { has_splice = true; break; }
        }

        if (has_splice) {
            Node head_val = {};
            Node *cur = &head_val;
            for (_Node *a = node->args; a; ) {
                _Node *next_a = a->next;
                a->next = NULL;

                _Node *chain;
                if (splice_chain_for_arg(s, a, &chain)) {
                    if (chain) {
                        _Node *tail = chain;
                        while (tail->next) tail = tail->next;
                        cur->next = chain;
                        cur = tail;
                    }
                    // Empty splice: arg disappears (chain == NULL → no-op)
                } else {
                    _Node *sub = quote_substitute(s, a);
                    if (sub) { cur->next = sub; cur = sub; }
                }
                cur->next = NULL;
                a = next_a;
            }
            node->args = head_val.next;
        } else {
            // No splice: existing scalar substitution (unchanged)
            node->args = quote_substitute(s, node->args);
            for (_Node *a = node->args; a && a->next; a = a->next)
                a->next = quote_substitute(s, a->next);
        }
    }

    // switch case chains
    for (_Node *c = node->case_next; c; c = c->case_next)
        c->body = quote_substitute(s, c->body);
    if (node->default_case)
        node->default_case->body =
            quote_substitute(s, node->default_case->body);

    return node;
}

static void quote_rebind_macro_scope(_Node *node, Scope *old_scope,
                                     Scope *new_scope) {
    if (!node)
        return;

    if (node->kind == ND_MACRO_CALL && node->macro_scope == old_scope)
        node->macro_scope = new_scope;

    quote_rebind_macro_scope(node->lhs, old_scope, new_scope);
    quote_rebind_macro_scope(node->rhs, old_scope, new_scope);
    quote_rebind_macro_scope(node->cond, old_scope, new_scope);
    quote_rebind_macro_scope(node->then, old_scope, new_scope);
    quote_rebind_macro_scope(node->els, old_scope, new_scope);
    quote_rebind_macro_scope(node->init, old_scope, new_scope);
    quote_rebind_macro_scope(node->inc, old_scope, new_scope);

    for (_Node *st = node->body; st; st = st->next)
        quote_rebind_macro_scope(st, old_scope, new_scope);

    for (_Node *a = node->args; a; a = a->next)
        quote_rebind_macro_scope(a, old_scope, new_scope);

    for (_Node *c = node->case_next; c; c = c->case_next)
        quote_rebind_macro_scope(c->body, old_scope, new_scope);
    if (node->default_case)
        quote_rebind_macro_scope(node->default_case->body, old_scope,
                                 new_scope);
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
static _Node *quote_core(JCC *vm, const char *tmpl,
                             _Node **nodes, int n) {
    if (!vm || !tmpl)
        return NULL;

    // 1. Tokenize the template string
    Token *toks = tokenize_string(vm, (char *)"<quote>", (char *)tmpl);
    if (!toks)
        return NULL;
    convert_pp_tokens(vm, toks);

    // 2. Scan, validate mixing, rewrite $$ / $@ / $@N
    uint64_t splice_mask = 0;
    int max_index = quote_scan_and_rewrite(vm, toks, &splice_mask);
    if (max_index < 0)
        return NULL; // mixing error already reported

    // 3. Validate count (the array form enforces this; variadic derives n)
    if (max_index > n) {
        error("__jcc_quote: template references $%d but only %d argument%s supplied",
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

    // Register scalar placeholders $k for all referenced indices
    for (int k = 1; k <= max_index; k++) {
        _Node *arg = (k - 1 < n) ? nodes[k - 1] : NULL;
        char *name = arena_format(vm, "$%d", k);
        Obj *var = quote_push_placeholder(vm, &quote_scope, name, arg);
        subst.placeholder_vars[k - 1] = var;
    }

    // Register splice placeholders $@k for each $@k / $@ index seen
    for (int k = 1; k <= max_index && k <= 64; k++) {
        if (!(splice_mask & ((uint64_t)1 << (k - 1)))) continue;
        char *name = arena_format(vm, "$@%d", k);
        // Type doesn't matter for splice placeholders — the whole
        // ND_EXPR_STMT wrapper is discarded during substitution.
        Obj *var = quote_push_placeholder(vm, &quote_scope, name, NULL);
        subst.splice_vars[k - 1] = var;
    }

    // 5. Parse (auto-detect expr vs stmt)
    Token *rest = NULL;
    _Node *result = NULL;
    if (quote_is_stmt(toks)) {
        result = cc_parse_stmt(vm, &rest, toks);
    } else {
        result = cc_parse_expr(vm, &rest, toks);
    }

    quote_rebind_macro_scope(result, &quote_scope, quote_scope.next);

    // 6. Restore outer scope, freeing any hashmap buckets accumulated in the
    //    quote scope (var_map from push_scope, tag_map from struct/enum tags).
    while (vm->compiler.scope != quote_scope.next) {
        hashmap_deinit_borrowed(&vm->compiler.scope->var_map);
        hashmap_deinit_borrowed(&vm->compiler.scope->tag_map);
        vm->compiler.scope = vm->compiler.scope->next;
    }

    if (!result)
        return NULL;

    // 7. Substitute placeholder vars with caller-provided argument nodes
    result = quote_substitute(&subst, result);

    // 8. Re-run add_type so spliced-in types propagate correctly
    add_type(vm, result);

    return result;
}

_Node *__jcc_quote_n(JCC *vm, const char *tmpl, _Node **nodes, int count) {
    if (!vm || !tmpl || (!nodes && count > 0))
        return NULL;
    return quote_core(vm, tmpl, nodes, count);
}

_Node *__jcc_quote(JCC *vm, const char *tmpl, ...) {
    if (!vm || !tmpl)
        return NULL;

    // Scan the raw template string to derive max splice index without
    // tokenising (avoids double arena allocation in quote_core).
    // Handles $N, $$, $@N, and $@ forms.
    int max_index = 0;
    int incr_count = 0;

    for (const char *p = tmpl; *p; p++) {
        if (*p != '$') continue;
        const char *q = p + 1;
        if (*q == '$') {
            // $$ incremental scalar splice
            incr_count++;
            if (incr_count > max_index)
                max_index = incr_count;
            p = q; // skip second $
        } else if (*q == '@') {
            // $@N positional splice or $@ incremental splice
            const char *r = q + 1;
            if (*r >= '1' && *r <= '9') {
                // $@N positional splice
                int idx = 0;
                while (*r >= '0' && *r <= '9')
                    idx = idx * 10 + (*r++ - '0');
                if (idx > max_index)
                    max_index = idx;
                p = r - 1; // loop will advance past last digit
            } else {
                // $@ incremental splice
                incr_count++;
                if (incr_count > max_index)
                    max_index = incr_count;
                p = q; // skip @
            }
        } else if (*q >= '1' && *q <= '9') {
            // $N positional scalar splice
            int n = 0;
            while (*q >= '0' && *q <= '9')
                n = n * 10 + (*q++ - '0');
            if (n > max_index)
                max_index = n;
            p = q - 1; // loop will increment past last digit
        }
        // lone $ followed by anything else: not a splice point, ignore
    }

    // Collect exactly max_index nodes from va_args
    int n = (max_index < 64) ? max_index : 64;
    _Node *arg_buf[64];
    memset(arg_buf, 0, sizeof(arg_buf));

    va_list ap;
    va_start(ap, tmpl);
    for (int i = 0; i < n; i++)
        arg_buf[i] = va_arg(ap, _Node *);
    va_end(ap);

    return quote_core(vm, tmpl, arg_buf, n);
}

// Build a ->next-linked chain from an array of nodes and return the head.
// Useful for constructing the list argument to a $@k splice.
// A single node is a chain of length 1; passing count==0 returns NULL.
_Node *__jcc_node_list(JCC *vm, _Node **nodes, int count) {
    if (!vm || !nodes || count <= 0)
        return NULL;

    Node *head = nodes[0];
    Node *cur  = head;
    for (int i = 1; i < count; i++) {
        if (!nodes[i]) break;
        cur->next = nodes[i];
        cur = cur->next;
    }
    if (cur) cur->next = NULL;
    return head;
}

// ============================================================================
// Type / Declaration Builders (ticket #171)
// ============================================================================

// Helper: synthesize a Token for use as a name field in Type/Member.
// The token's loc is an arena-allocated copy of name so it outlives the call.
static Token *reflect_make_name_token(JCC *vm, const char *name, int name_len) {
    Token *tok = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    memset(tok, 0, sizeof(Token));
    tok->kind = TK_IDENT;
    tok->loc = arena_strdup(vm, name);
    tok->len = name_len;
    return tok;
}

// Helper: expose a struct/union/enum type by tag name so _AST_FIND_TYPE(name)
// resolves it. Mirrors push_tag_scope + record_type_name in parse.c.
static void reflect_push_tag_scope(JCC *vm, const char *name, int name_len,
                                   Type *ty) {
    if (!vm || !vm->compiler.scope)
        return;

    // Insert into tag scope linked list + hashmap
    TagScopeNode *node =
        arena_alloc(&vm->compiler.parser_arena, sizeof(TagScopeNode));
    memset(node, 0, sizeof(TagScopeNode));
    node->name = arena_strdup(vm, name);
    node->name_len = name_len;
    node->ty = ty;
    node->next = vm->compiler.scope->tags;
    vm->compiler.scope->tags = node;
    hashmap_put2_borrowed(&vm->compiler.scope->tag_map, node->name,
                          node->name_len, node);

    // Record for type_names list (-M support)
    TypeNameRecord *rec =
        arena_alloc(&vm->compiler.parser_arena, sizeof(TypeNameRecord));
    memset(rec, 0, sizeof(TypeNameRecord));
    rec->ty = ty;
    rec->name = node->name;
    rec->name_len = name_len;
    rec->owner_fn = vm->compiler.current_fn;
    rec->is_tag = true;
    rec->next = vm->compiler.type_names;
    vm->compiler.type_names = rec;
}

// Helper: expose a typedef by name so _AST_FIND_TYPE(name) resolves it.
// Mirrors push_scope(...)->type_def = ty + record_type_name in parse.c.
static void reflect_push_typedef_scope(JCC *vm, const char *name, int name_len,
                                       Type *ty) {
    if (!vm || !vm->compiler.scope)
        return;

    VarScopeNode *node =
        arena_alloc(&vm->compiler.parser_arena, sizeof(VarScopeNode));
    memset(node, 0, sizeof(VarScopeNode));
    node->name = arena_strdup(vm, name);
    node->name_len = name_len;
    node->type_def = ty;
    node->next = vm->compiler.scope->vars;
    vm->compiler.scope->vars = node;
    hashmap_put2_borrowed(&vm->compiler.scope->var_map, node->name,
                          node->name_len, node);

    // Record for type_names list
    TypeNameRecord *rec =
        arena_alloc(&vm->compiler.parser_arena, sizeof(TypeNameRecord));
    memset(rec, 0, sizeof(TypeNameRecord));
    rec->ty = ty;
    rec->name = node->name;
    rec->name_len = name_len;
    rec->owner_fn = vm->compiler.current_fn;
    rec->is_tag = false;
    rec->next = vm->compiler.type_names;
    vm->compiler.type_names = rec;
}

// _AST_MAKE_STRUCT(name) — create and expose a new struct type.
// Fields are added with _AST_STRUCT_ADD_FIELD.
_Type *__jcc_ast_make_struct(JCC *vm, const char *name) {
    if (!vm || !name)
        return NULL;
    Type *ty = struct_type(vm);
    int name_len = (int)strlen(name);
    ty->name = reflect_make_name_token(vm, name, name_len);
    ty->size = 0;
    ty->align = 1;
    reflect_push_tag_scope(vm, name, name_len, ty);
    return ty;
}

// _AST_MAKE_UNION(name) — create and expose a new union type.
_Type *__jcc_ast_make_union(JCC *vm, const char *name) {
    if (!vm || !name)
        return NULL;
    Type *ty = union_type(vm);
    int name_len = (int)strlen(name);
    ty->name = reflect_make_name_token(vm, name, name_len);
    ty->size = 0;
    ty->align = 1;
    reflect_push_tag_scope(vm, name, name_len, ty);
    return ty;
}

// _AST_STRUCT_ADD_FIELD(ty, name, field_type) — append a field to a struct or
// union type and recompute the aggregate size/alignment.
_Type *__jcc_ast_struct_add_field(JCC *vm, _Type *ty, const char *name,
                                   _Type *field_type) {
    if (!vm || !ty || !name || !field_type)
        return NULL;
    if (ty->kind != TY_STRUCT && ty->kind != TY_UNION)
        return NULL;

    int name_len = (int)strlen(name);

    Member *mem = arena_alloc(&vm->compiler.parser_arena, sizeof(Member));
    memset(mem, 0, sizeof(Member));
    mem->ty = field_type;
    mem->name = reflect_make_name_token(vm, name, name_len);
    mem->align = field_type->align;

    // Append to the end of the members list to maintain declaration order
    if (!ty->members) {
        ty->members = mem;
    } else {
        Member *last = ty->members;
        while (last->next)
            last = last->next;
        last->next = mem;
    }

    // Recompute layout for all fields from scratch
    if (ty->kind == TY_STRUCT) {
        int bits = 0;
        int new_align = 1;
        for (Member *m = ty->members; m; m = m->next) {
            bits = reflect_align_to(bits, m->align * 8);
            m->offset = bits / 8;
            bits += m->ty->size * 8;
            if (new_align < m->align)
                new_align = m->align;
        }
        ty->align = new_align;
        ty->size = reflect_align_to(bits, ty->align * 8) / 8;
        if (ty->size == 0) ty->size = 1; // empty struct -> 1 byte
    } else {
        // Union: all members at offset 0, size = max member size
        int new_size = 0;
        int new_align = 1;
        for (Member *m = ty->members; m; m = m->next) {
            m->offset = 0;
            if (new_align < m->align) new_align = m->align;
            if (new_size < m->ty->size) new_size = m->ty->size;
        }
        ty->align = new_align;
        ty->size = reflect_align_to(new_size, new_align);
        if (ty->size == 0) ty->size = 1;
    }

    return ty;
}

// _AST_MAKE_ENUM(name) — create and expose a new enum type.
// Constants are added with _AST_ENUM_ADD_CONSTANT.
_Type *__jcc_ast_make_enum(JCC *vm, const char *name) {
    if (!vm || !name)
        return NULL;
    Type *ty = enum_type(vm);
    int name_len = (int)strlen(name);
    ty->name = reflect_make_name_token(vm, name, name_len);
    reflect_push_tag_scope(vm, name, name_len, ty);
    return ty;
}

// _AST_ENUM_ADD_CONSTANT(ty, name, value) — add a named constant to an enum
// type and expose it as an integer constant in current scope.
void __jcc_ast_enum_add_constant(JCC *vm, _Type *ty, const char *name,
                                  int value) {
    if (!vm || !ty || !name || ty->kind != TY_ENUM)
        return;

    int name_len = (int)strlen(name);

    // Append constant to type's enum_constants list
    EnumConstant *ec =
        arena_alloc(&vm->compiler.parser_arena, sizeof(EnumConstant));
    memset(ec, 0, sizeof(EnumConstant));
    ec->name = arena_strdup(vm, name);
    ec->value = value;
    if (!ty->enum_constants) {
        ty->enum_constants = ec;
    } else {
        EnumConstant *last = ty->enum_constants;
        while (last->next)
            last = last->next;
        last->next = ec;
    }

    // Expose as a compile-time integer constant in current scope
    // (mirrors the push_scope + enum_ty/enum_val assignment in parse.c:1137)
    if (!vm->compiler.scope)
        return;
    VarScopeNode *sc =
        arena_alloc(&vm->compiler.parser_arena, sizeof(VarScopeNode));
    memset(sc, 0, sizeof(VarScopeNode));
    sc->name = arena_strdup(vm, name);
    sc->name_len = name_len;
    sc->enum_ty = ty;
    sc->enum_val = value;
    sc->next = vm->compiler.scope->vars;
    vm->compiler.scope->vars = sc;
    hashmap_put2_borrowed(&vm->compiler.scope->var_map, sc->name, sc->name_len,
                          sc);
}

// _AST_MAKE_TYPEDEF(name, underlying) — register name as a typedef alias for
// underlying so that _AST_FIND_TYPE(name) and C code can use it.
_Type *__jcc_ast_make_typedef(JCC *vm, const char *name, _Type *underlying) {
    if (!vm || !name || !underlying)
        return NULL;
    int name_len = (int)strlen(name);
    // Give the underlying type this name if it has none
    if (!underlying->name)
        underlying->name = reflect_make_name_token(vm, name, name_len);
    reflect_push_typedef_scope(vm, name, name_len, underlying);
    return underlying;
}

// ============================================================================
// Ticket #188: Comptime Variable Access
// ============================================================================

static ComptimeVar *find_comptime_var(JCC *vm, const char *name) {
    size_t len = strlen(name);
    for (ComptimeVar *cv = vm->compiler.comptime_vars; cv; cv = cv->next) {
        if (strlen(cv->name) == len && strncmp(cv->name, name, len) == 0)
            return cv;
    }
    return NULL;
}

int64_t __jcc_get_comptime_int(JCC *vm, const char *name) {
    if (!vm || !name) return 0;
    ComptimeVar *cv = find_comptime_var(vm, name);
    if (!cv || !cv->is_evaluated || cv->is_struct) return 0;
    return cv->is_float ? (int64_t)cv->float_val : cv->int_val;
}

double __jcc_get_comptime_float(JCC *vm, const char *name) {
    if (!vm || !name) return 0.0;
    ComptimeVar *cv = find_comptime_var(vm, name);
    if (!cv || !cv->is_evaluated || cv->is_struct) return 0.0;
    return cv->is_float ? cv->float_val : (double)cv->int_val;
}

_Node *__jcc_get_comptime_var(JCC *vm, const char *name) {
    if (!vm || !name) return NULL;
    ComptimeVar *cv = find_comptime_var(vm, name);
    if (!cv || !cv->is_evaluated || cv->is_struct) return NULL;
    if (cv->is_float)
        return __jcc_ast_float_literal(vm, cv->float_val);
    return __jcc_ast_int_literal(vm, cv->int_val);
}

_Node *__jcc_get_comptime_member(JCC *vm, const char *var_name,
                                  const char *field) {
    if (!vm || !var_name || !field) return NULL;
    ComptimeVar *cv = find_comptime_var(vm, var_name);
    if (!cv || !cv->is_evaluated || !cv->is_struct) return NULL;
    size_t flen = strlen(field);
    for (ComptimeVarMember *m = cv->members; m; m = m->next) {
        if (m->name && strlen(m->name) == flen &&
            strncmp(m->name, field, flen) == 0) {
            if (m->is_float)
                return __jcc_ast_float_literal(vm, m->float_val);
            return __jcc_ast_int_literal(vm, m->int_val);
        }
    }
    return NULL;
}

_Node *__jcc_get_constexpr_value(JCC *vm, const char *name) {
    if (!vm || !name) return NULL;
    Obj *obj = (Obj *)__jcc_ast_find_global(vm, name);
    if (!obj || !obj->is_constexpr || !obj->init_expr) return NULL;
    if (obj->ty->kind >= TY_FLOAT)
        return __jcc_ast_float_literal(vm, cc_eval_double(vm, obj->init_expr));
    return __jcc_ast_int_literal(vm, cc_eval(vm, obj->init_expr));
}
