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

// Reflection API for pragma macros
// Provides type introspection and AST construction functions

#include "./internal.h"

// Prototypes generated from include/cccc/reflection.h (see
// tools/gen_reflection_ffi.py). Including them here -- not just in
// macros.c, where they're used to take function addresses for FFI
// registration -- turns any drift between a reflection.h prototype and this
// file's definition into a compile error (conflicting declaration),
// instead of the two silently going out of sync.
#include "reflection_ffi_protos.inc"

// Aliases so the public API types match reflection.h
typedef Type         Type;
typedef Node         Node;
typedef Obj          Obj;
typedef Member       Member;
typedef EnumConstant EnumConstant;
typedef Token        Token;
typedef TypeKind     TypeKind;
typedef NodeKind     NodeKind;
typedef AttrTarget   AttrTarget;

// Compiler-internal global tracking the VM live for the current compile.
// Seeded once in cc_init and save/restored around macro execution windows
// (see src/macros.c) -- no builtin exposes it directly any more; every
// __builtin_* below reads it internally instead of taking a VirtualMachine*
// parameter.
VirtualMachine *__builtin_current_vm = NULL;

// ============================================================================
// Internal Helpers (replicate static functions from parse.c)
// ============================================================================

static Obj *reflect_new_var(VirtualMachine *vm, char *name, int name_len,
                            Type *ty) {
    Obj *var = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
    memset(var, 0, sizeof(Obj));
    var->name  = name;
    var->ty    = ty;
    var->align = ty->align;
    return var;
}

static char *reflect_unique_name(VirtualMachine *vm) {
    return arena_format(vm, ".L..%d", vm->compiler.unique_name_counter++);
}

const char *__builtin_gensym(const char *prefix) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !prefix)
        return NULL;
    return arena_format(vm, "%s__%d", prefix,
                        vm->compiler.macro_gensym_counter++);
}

Token *__builtin_ast_current_token(void) {
    VirtualMachine *vm = __builtin_current_vm;
    return vm ? vm->compiler.macro_call_tok : NULL;
}

Token *__builtin_ast_synthetic_token(const char *label) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm)
        return NULL;

    if (!label || !label[0])
        label = "generated";

    char  *display_name = arena_format(vm, "<cccc macro: %s>", label);
    char  *contents     = arena_format(vm, "%s\n", label);
    File  *file         = new_file(vm, display_name, 0, contents);

    Token *tok = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    memset(tok, 0, sizeof(Token));
    tok->kind     = TK_IDENT;
    tok->loc      = contents;
    tok->len      = (int)strlen(label);
    tok->file     = file;
    tok->filename = file->display_name;
    tok->line_no  = 1;
    tok->col_no   = 1;
    // #966: stamp the live expansion chain so a diagnostic against this
    // synthetic token (which otherwise carries no source position at all)
    // still gets an "in expansion of ..." backtrace.
    tok->expansion = vm->compiler.expansion_stack;
    return tok;
}

Token *__builtin_ast_token_from_node(Node *node) {
    return node ? node->tok : NULL;
}

Node *__builtin_ast_set_token(Node *node, Token *tok) {
    if (node)
        node->tok = tok;
    return node;
}

Node *__builtin_ast_copy_location(Node *dst, Node *src) {
    if (dst)
        dst->tok = src ? src->tok : NULL;
    return dst;
}

static Obj *reflect_new_gvar(VirtualMachine *vm, char *name, int name_len,
                             Type *ty) {
    Obj *var             = reflect_new_var(vm, name, name_len, ty);
    var->next            = vm->compiler.globals;
    var->is_static       = true;
    var->is_definition   = true;
    vm->compiler.globals = var;
    return var;
}

static Obj *reflect_new_anon_gvar(VirtualMachine *vm, Type *ty) {
    char *name = reflect_unique_name(vm);
    return reflect_new_gvar(vm, name, strlen(name), ty);
}

// ============================================================================
// Type Lookup and Introspection
// ============================================================================

// Walk the current scope chain for a struct/union/enum tag OR typedef
// registered under `name`. Factored out of __builtin_ast_find_type so it
// can be retried once after a #894 demand-driven splice attempt.
static Type *find_type_in_scope(VirtualMachine *vm, const char *name,
                                size_t name_len) {
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

Type *__builtin_ast_find_type(const char *name) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !name)
        return NULL;

    size_t name_len = strlen(name);

    Type  *ty       = find_type_in_scope(vm, name, name_len);
    if (ty)
        return ty;

    // #894: GetType("Name") (and FindType et al) run at comptime *execution*
    // time -- there is no identifier token to intercept on a miss the way
    // is_typename()/find_tag() do during the comptime *parse*, so retry
    // once here by plain name against the same demand-driven declaration
    // index instead. Not gated on in_macro_mode: this runs after
    // compile_macro_program has already reset it to false (see
    // comptime_index_splice's comment, src/macros.c).
    if (cc_comptime_resolve_type_name(vm, name, (int)name_len))
        return find_type_in_scope(vm, name, name_len);

    return NULL;
}

bool __builtin_ast_type_exists(const char *name) {
    return __builtin_ast_find_type(name) != NULL;
}

Type *__builtin_ast_get_type(const char *name) {
    if (!name)
        return NULL;

    const struct {
        const char *name;
        Type       *type;
    } builtins[] = {
        {"void", ty_void},
        {"char", ty_char},
        {"short", ty_short},
        {"int", ty_int},
        {"long", ty_long},
        {"float", ty_float},
        {"double", ty_double},
        {"_Bool", ty_bool},
        {"_Decimal32", ty_decimal32},
        {"_Decimal64", ty_decimal64},
        {"_Decimal128", ty_decimal128},
    };

    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++)
        if (strlen(name) == strlen(builtins[i].name) &&
            strncmp(name, builtins[i].name, strlen(builtins[i].name)) == 0)
            return builtins[i].type;

    return __builtin_ast_find_type(name);
}

TypeKind __builtin_ast_type_kind(Type *ty) {
    return ty ? ty->kind : TY_VOID;
}

int __builtin_ast_type_size(Type *ty) {
    return ty ? ty->size : 0;
}

int __builtin_ast_type_align(Type *ty) {
    return ty ? ty->align : 0;
}

bool __builtin_ast_type_is_unsigned(Type *ty) {
    return ty ? ty->is_unsigned : false;
}

bool __builtin_ast_type_is_const(Type *ty) {
    return ty ? ty->is_const : false;
}

Type *__builtin_ast_type_base(Type *ty) {
    if (!ty)
        return NULL;
    if (ty->kind != TY_PTR && ty->kind != TY_ARRAY && ty->kind != TY_VLA)
        return NULL;
    return ty->base;
}

int __builtin_ast_type_array_len(Type *ty) {
    if (!ty || ty->kind != TY_ARRAY)
        return -1;
    return ty->array_len;
}

Type *__builtin_ast_type_return_type(Type *ty) {
    if (!ty || ty->kind != TY_FUNC)
        return NULL;
    return ty->return_ty;
}

int __builtin_ast_type_param_count(Type *ty) {
    if (!ty || ty->kind != TY_FUNC)
        return -1;

    int count = 0;
    for (Type *p = ty->params; p; p = p->next)
        count++;
    return count;
}

Type *__builtin_ast_type_param_at(Type *ty, int index) {
    if (!ty || ty->kind != TY_FUNC || index < 0)
        return NULL;

    Type *p = ty->params;
    for (int i = 0; i < index && p; i++)
        p = p->next;
    return p;
}

bool __builtin_ast_type_is_variadic(Type *ty) {
    if (!ty || ty->kind != TY_FUNC)
        return false;
    return ty->is_variadic;
}

// #900: struct_union_decl/enum_specifier (src/parse.c) set a fresh tag
// Type's ->name to the tag token, but a *reference* to an existing tag
// (e.g. a second function parameter of the same struct type) returns the
// canonical, shared Type -- and declarator() then overwrites that shared
// type's ->name with the declarator's own identifier (parameter/variable
// name), not the tag. #892 added Type.struct_tag/Type.enum_tag to survive
// that overwrite (see src/cccc.h); the serializer (src/serialize_type.c)
// already prefers them. Prefer the same fields here so TypeName()/TypeCName()
// report the real tag instead of whichever declarator last clobbered the shared
// Type.
static Token *type_name_token(Type *ty) {
    if (!ty)
        return NULL;
    if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && ty->struct_tag)
        return ty->struct_tag;
    if (ty->kind == TY_ENUM && ty->enum_tag)
        return ty->enum_tag;
    return ty->name;
}

const char *__builtin_ast_type_name(Type *ty) {
    Token *name = type_name_token(ty);
    if (!name)
        return NULL;

    // Extract string from token into an arena-allocated buffer -- a shared
    // static buffer would alias across calls (e.g. for callers building a
    // name array across multiple types).
    VirtualMachine *vm     = __builtin_current_vm;
    int             len    = name->len;
    char           *buffer = arena_alloc(&vm->compiler.parser_arena, len + 1);
    memcpy(buffer, name->loc, len);
    buffer[len] = '\0';
    return buffer;
}

// __builtin_ast_type_c_name: returns a valid C identifier fragment for a type,
// suitable for naming generated functions like sum_<T>/map_<T>.
// For builtin scalar types, we always use the kind-based name (e.g. "int",
// "ulong") regardless of ty->name, because typedef aliases (e.g.
// `typedef int wchar_t`) mutate the singleton type's name field and would
// otherwise produce misleading names like `sum_wchar_t` for `int`.
// For non-scalar named types (structs, enums, user typedefs), ty->name is used.
const char *__builtin_ast_type_c_name(Type *ty) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !ty)
        return NULL;

    const char *base = NULL;
    switch (ty->kind) {
        case TY_BOOL:
            base = "bool";
            break;
        case TY_CHAR:
            base = ty->is_unsigned ? "uchar" : "char";
            break;
        case TY_SHORT:
            base = ty->is_unsigned ? "ushort" : "short";
            break;
        case TY_INT:
            base = ty->is_unsigned ? "uint" : "int";
            break;
        case TY_LONG:
            base = ty->is_unsigned ? "ulong" : "long";
            break;
        case TY_FLOAT:
            base = "float";
            break;
        case TY_DOUBLE:
            base = "double";
            break;
        case TY_LDOUBLE:
            base = "ldouble";
            break;
        case TY_DECIMAL32:
            base = "_Decimal32";
            break;
        case TY_DECIMAL64:
            base = "_Decimal64";
            break;
        case TY_DECIMAL128:
            base = "_Decimal128";
            break;
        default:
            break;
    }
    if (base)
        return arena_strdup(vm, base);

    if (type_name_token(ty))
        return __builtin_ast_type_name(ty);

    return NULL;
}

Type *__builtin_ast_make_pointer(Type *base) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !base)
        return NULL;
    return pointer_to(vm, base);
}

Type *__builtin_ast_make_array(Type *base, int len) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !base || len < 0)
        return NULL;
    return array_of(vm, base, len);
}

// ============================================================================
// Enum Reflection
// ============================================================================

int __builtin_ast_enum_count(Type *enum_type) {
    VirtualMachine *vm = __builtin_current_vm;
    (void)vm; // Unused but kept for API consistency
    if (!enum_type || enum_type->kind != TY_ENUM)
        return -1;

    int count = 0;
    for (EnumConstant *ec = enum_type->enum_constants; ec; ec = ec->next)
        count++;
    return count;
}

EnumConstant *__builtin_ast_enum_at(Type *enum_type, int index) {
    VirtualMachine *vm = __builtin_current_vm;
    (void)vm;
    if (!enum_type || enum_type->kind != TY_ENUM || index < 0)
        return NULL;

    EnumConstant *ec = enum_type->enum_constants;
    for (int i = 0; i < index && ec; i++)
        ec = ec->next;
    return ec;
}

EnumConstant *__builtin_ast_enum_find(Type *enum_type, const char *name) {
    VirtualMachine *vm = __builtin_current_vm;
    (void)vm;
    if (!enum_type || enum_type->kind != TY_ENUM || !name)
        return NULL;

    for (EnumConstant *ec = enum_type->enum_constants; ec; ec = ec->next)
        if (strlen(ec->name) == strlen(name) &&
            strncmp(ec->name, name, strlen(name)) == 0)
            return ec;
    return NULL;
}

const char *__builtin_ast_enum_constant_name(EnumConstant *ec) {
    return ec ? ec->name : NULL;
}

int64_t __builtin_ast_enum_constant_value(EnumConstant *ec) {
    return ec ? ec->value : 0;
}

const char *__builtin_ast_enum_name(Type *e) {
    return __builtin_ast_type_name(e);
}

int __builtin_ast_enum_value_count(Type *e) {
    int count = __builtin_ast_enum_count(e);
    return count < 0 ? 0 : count;
}

const char *__builtin_ast_enum_value_name(Type *e, int index) {
    EnumConstant *ec = __builtin_ast_enum_at(e, index);
    return __builtin_ast_enum_constant_name(ec);
}

int64_t __builtin_ast_enum_value(Type *e, int index) {
    EnumConstant *ec = __builtin_ast_enum_at(e, index);
    return __builtin_ast_enum_constant_value(ec);
}

// ============================================================================
// Struct/Union Member Introspection
// ============================================================================

int __builtin_ast_struct_member_count(Type *struct_type) {
    VirtualMachine *vm = __builtin_current_vm;
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

Member *__builtin_ast_struct_member_at(Type *struct_type, int index) {
    VirtualMachine *vm = __builtin_current_vm;
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

Member *__builtin_ast_struct_member_find(Type *struct_type, const char *name) {
    VirtualMachine *vm = __builtin_current_vm;
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

const char *__builtin_ast_member_name(Member *m) {
    if (!m || !m->name)
        return NULL;

    // Extract string from token into an arena-allocated buffer. Each call
    // must return a stable, distinct pointer (e.g. for callers building a
    // const char *fields[] array across StructMemberAt iterations) --
    // a shared static buffer would alias across calls.
    VirtualMachine *vm     = __builtin_current_vm;
    int             len    = m->name->len;
    char           *buffer = arena_alloc(&vm->compiler.parser_arena, len + 1);
    memcpy(buffer, m->name->loc, len);
    buffer[len] = '\0';
    return buffer;
}

Type *__builtin_ast_member_type(Member *m) {
    return m ? m->ty : NULL;
}

int __builtin_ast_member_offset(Member *m) {
    return m ? m->offset : 0;
}

bool __builtin_ast_member_is_bitfield(Member *m) {
    return m ? m->is_bitfield : false;
}

int __builtin_ast_member_bitfield_width(Member *m) {
    return (m && m->is_bitfield) ? m->bit_width : 0;
}

// Ticket #235: OffsetofChain(ty, "a", "b", ...) — sum of member offsets
// walking nested struct/union members, e.g. offsetof(ty, a.b). Returns -1
// if any name in the chain cannot be resolved.
int64_t __builtin_ast_offsetof_chain(Type *ty, const char **names, int n) {
    if (!ty || !names || n <= 0)
        return -1;

    int64_t total  = 0;
    Type   *cur_ty = ty;
    for (int i = 0; i < n; i++) {
        Member *m = __builtin_ast_struct_member_find(cur_ty, names[i]);
        if (!m)
            return -1;
        total  += m->offset;
        cur_ty  = m->ty;
    }
    return total;
}

// ============================================================================
// Global Symbol Introspection
// ============================================================================

static Obj *find_global_in_list(VirtualMachine *vm, const char *name,
                                size_t name_len) {
    for (Obj *obj = vm->compiler.globals; obj; obj = obj->next) {
        if (strlen(obj->name) == name_len &&
            strncmp(obj->name, name, name_len) == 0)
            return obj;
    }
    return NULL;
}

Obj *__builtin_ast_find_global(const char *name) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !name)
        return NULL;

    size_t name_len = strlen(name);
    Obj   *obj      = find_global_in_list(vm, name, name_len);
    if (obj)
        return obj;

    // #894: FindGlobal("name") is a public reflection.h entry point (also
    // used internally by __builtin_ast_var_ref) and runs at comptime
    // *execution* time, so retry once by plain name against the
    // demand-driven declaration index, same as __builtin_ast_find_type/
    // __builtin_ast_var_ref above. Not gated on in_macro_mode here --
    // cc_comptime_resolve_value_name only splices a CDK_OBJECT registration
    // while in_macro_mode is true (see comptime_index_splice, src/macros.c);
    // CDK_PROTO/CDK_ENUM_CONST have no such restriction.
    if (cc_comptime_resolve_value_name(vm, name, (int)name_len))
        return find_global_in_list(vm, name, name_len);

    return NULL;
}

int __builtin_ast_global_count(void) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm)
        return 0;

    int count = 0;
    for (Obj *obj = vm->compiler.globals; obj; obj = obj->next)
        count++;
    return count;
}

Obj *__builtin_ast_global_at(int index) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || index < 0)
        return NULL;

    Obj *obj = vm->compiler.globals;
    for (int i = 0; i < index && obj; i++)
        obj = obj->next;
    return obj;
}

const char *__builtin_ast_obj_name(Obj *obj) {
    return obj ? obj->name : NULL;
}

Type *__builtin_ast_obj_type(Obj *obj) {
    return obj ? obj->ty : NULL;
}

bool __builtin_ast_obj_is_function(Obj *obj) {
    return obj ? obj->is_function : false;
}

bool __builtin_ast_obj_is_definition(Obj *obj) {
    return obj ? obj->is_definition : false;
}

bool __builtin_ast_obj_is_static(Obj *obj) {
    return obj ? obj->is_static : false;
}

int __builtin_attr_target_kind(AttrTarget *target) {
    return target ? target->kind : 0;
}

const char *__builtin_attr_target_name(AttrTarget *target) {
    return target ? target->name : NULL;
}

Type *__builtin_attr_target_type(AttrTarget *target) {
    return target ? target->ty : NULL;
}

Obj *__builtin_attr_target_obj(AttrTarget *target) {
    return target ? target->obj : NULL;
}

Token *__builtin_attr_target_token(AttrTarget *target) {
    return target ? target->tok : NULL;
}

// ============================================================================
// AST Node Construction - Helper
// ============================================================================

static Node *alloc_node(VirtualMachine *vm, NodeKind kind) {
    Node *node = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    memset(node, 0, sizeof(Node));
    node->kind = kind;
    node->tok  = vm ? vm->compiler.macro_call_tok : NULL;
    return node;
}

// ============================================================================
// AST Node Construction - Literals
// ============================================================================

Node *__builtin_ast_int_literal(int64_t value) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm)
        return NULL;

    Node *node = alloc_node(vm, ND_NUM);
    node->val  = value;
    node->ty   = ty_long;
    return node;
}

Node *__builtin_ast_float_literal(double value) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm)
        return NULL;

    Node *node = alloc_node(vm, ND_NUM);
    node->fval = value;
    node->ty   = ty_double;
    return node;
}

Node *__builtin_ast_string_literal(const char *str) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !str)
        return NULL;

    int len = strlen(str);

    // Create type for the string
    Type *ty = array_of(vm, ty_char, len + 1);

    // Create an anonymous global variable for the string
    Obj *var = reflect_new_anon_gvar(vm, ty);
    // #925: the only other producer of an is_string_literal anon global is
    // new_string_literal() (parse.c), for a literal written directly in
    // source. This is the reflection API's programmatic equivalent -- same
    // contract (raw string bytes in init_data), so it needs the same flag,
    // or the serializer's `-c=generated` path (which doesn't run the
    // -m/-c=native-only rename_anon_globals pre-pass) treats it as an
    // unrecognized dotted name instead of inlining it as string text.
    var->is_string_literal = true;

    // Allocate space in the data segment and copy the string data
    // This is critical: we must place the data in the segment NOW,
    // not just set init_data (which is only used during initial emit_program)
    long long offset = vm->data_ptr - vm->data_seg;
    offset           = (offset + 7) & ~7; // Align to 8 bytes
    vm->data_ptr     = vm->data_seg + offset;
    if (vm_data_ensure(vm, (long long)(len + 1)) != 0)
        error("codegen: data segment overflow (limit: %d bytes)",
              vm->poolsize_max);

    var->offset    = offset;
    var->init_data = (char *)vm->data_ptr; // Point directly to data segment

    // Copy string to data segment
    memcpy(vm->data_ptr, str, len + 1);
    vm->data_ptr += len + 1;

    // Create a variable reference node
    Node *node = alloc_node(vm, ND_VAR);
    node->var  = var;
    node->ty   = ty;
    return node;
}

// #1050: names reflection.h's Memcpy()/Strlen()/Strcmp() macros (and
// reflect_serialize_members/__builtin_ast_serialize's own direct
// __builtin_ast_var_ref("memcpy") calls, for @serialize/@deserialize) can
// resolve to, and the real host header that declares each. Two distinct,
// independently-discovered ways an Obj for one of these names can end up
// resolved with no #include of its own reaching -c=native output:
//   (1) ensure_libc_fn_decl() (below) synthesizes a fresh Obj with no
//       token/file at all, when nothing else already declares the name.
//   (2) reflection.h itself #includes <string.h> (so these macros "just
//       work" without the TU writing its own #include) -- compile_macro_
//       program()'s unconditional implicit_reflection_tokens() call (not
//       gated on custom-attribute usage the way ensure_reflection_attrs_
//       registered()/__builtin_ensure_string_h_decls() is) parses that
//       #include for real, leaving a genuine Obj in scope whose token
//       names CCCC's own bundled include/string.h -- never a captured
//       user #include, so auto-capture has nothing to replay for it
//       either.
// Both shapes funnel through var_ref_lookup() below (the scope branch for
// (2), the __builtin_ast_find_global() branch for (1)), so registering
// there covers both centrally instead of duplicating the check at every
// call site. See vm->compiler.synth_libc_decls / serialize_synth_libc_
// includes, src/serialize_program.c).
static const struct {
    const char *name;
    const char *header;
} synth_libc_headers[] = {
    {"memcpy", "string.h"}, {"memmove", "string.h"}, {"memcmp", "string.h"},
    {"strlen", "string.h"}, {"strcmp", "string.h"},
};

static void register_synth_libc_call(VirtualMachine *vm, Obj *obj) {
    if (!obj || !obj->is_function)
        return;
    // A program's own file-scope declaration of one of these five names
    // (however unusual -- shadowing a libc name is legal C, more so
    // `static`) is a real, command-line-input Obj with its own token: it
    // needs no synthesized #include at all, and forcing one in would
    // collide with its own, possibly incompatible signature (e.g. a
    // 'static declaration follows non-static declaration' error once
    // <string.h>'s real prototype is also in scope). Only register an Obj
    // that is NOT written in one of the user's own command-line input
    // files -- CCCC's own synthesized (no token) or bundled-header-sourced
    // (reflection.h's internal #include <string.h>) shapes both pass this.
    if (obj->tok && obj->tok->file &&
        cc_file_is_command_line_input(vm, obj->tok->file->name))
        return;
    const char *header = NULL;
    for (size_t i = 0;
         i < sizeof(synth_libc_headers) / sizeof(synth_libc_headers[0]); i++) {
        if (!strcmp(obj->name, synth_libc_headers[i].name)) {
            header = synth_libc_headers[i].header;
            break;
        }
    }
    if (!header)
        return;

    SynthLibcDeclArray *reg = &vm->compiler.synth_libc_decls;
    for (int i = 0; i < reg->len; i++)
        if (reg->data[i].obj == obj)
            return; // already registered

    if (reg->len == reg->capacity) {
        int            new_capacity = reg->capacity ? reg->capacity * 2 : 8;
        SynthLibcDecl *new_data     = arena_alloc(
            &vm->compiler.parser_arena, sizeof(SynthLibcDecl) * new_capacity);
        if (reg->data)
            memcpy(new_data, reg->data, sizeof(SynthLibcDecl) * reg->len);
        reg->data     = new_data;
        reg->capacity = new_capacity;
    }
    reg->data[reg->len++] = (SynthLibcDecl){.obj = obj, .header = header};
}

// Scope-then-globals lookup behind __builtin_ast_var_ref. Factored out so
// it can be retried once after a #894 demand-driven splice attempt.
static Node *var_ref_lookup(VirtualMachine *vm, const char *name,
                            size_t name_len) {
    for (Scope *sc = vm->compiler.scope; sc; sc = sc->next) {
        for (VarScopeNode *node = sc->vars; node; node = node->next) {
            if (node->name_len == (int)name_len &&
                strncmp(node->name, name, name_len) == 0) {
                if (node->var) {
                    node->var->is_used = true;
                    register_synth_libc_call(vm, node->var); // #1050
                    Node *n = alloc_node(vm, ND_VAR);
                    n->var  = node->var;
                    n->ty   = node->var->ty;
                    return n;
                }
            }
        }
    }

    // Also check globals
    Obj *global = __builtin_ast_find_global(name);
    if (global) {
        global->is_used = true;
        register_synth_libc_call(vm, global); // #1050
        Node *n = alloc_node(vm, ND_VAR);
        n->var  = global;
        n->ty   = global->ty;
        return n;
    }

    return NULL;
}

Node *__builtin_ast_var_ref(const char *name) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !name)
        return NULL;

    size_t name_len = strlen(name);
    Node  *n        = var_ref_lookup(vm, name, name_len);
    if (n)
        return n;

    // #894: VarRef("name") (and similar reflection lookups) run at comptime
    // *execution* time -- there is no identifier token to intercept on a
    // miss the way primary()'s hook does during the comptime *parse*, so
    // retry once here by plain name against the same demand-driven
    // declaration index instead. Not gated on in_macro_mode -- see
    // __builtin_ast_find_global above.
    if (cc_comptime_resolve_value_name(vm, name, (int)name_len))
        return var_ref_lookup(vm, name, name_len);

    return NULL;
}

Node *__builtin_ast_param_ref(Obj *fn, const char *name) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !fn || !name)
        return NULL;

    // Find the parameter in the function's params list
    size_t name_len = strlen(name);
    for (Obj *param = fn->params; param; param = param->next) {
        if (strlen(param->name) == name_len &&
            strncmp(param->name, name, name_len) == 0) {
            param->is_used = true;
            Node *n        = alloc_node(vm, ND_VAR);
            n->var         = param;
            n->ty          = param->ty;
            return n;
        }
    }

    return NULL;
}

// ============================================================================
// AST Node Construction - Expressions
// ============================================================================

Node *__builtin_ast_binary(NodeKind op, Node *left, Node *right) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !left || !right)
        return NULL;

    Node *node = alloc_node(vm, op);
    node->lhs  = left;
    node->rhs  = right;
    // Type will be determined by add_type pass
    return node;
}

Node *__builtin_ast_unary(NodeKind op, Node *operand) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !operand)
        return NULL;

    Node *node = alloc_node(vm, op);
    node->lhs  = operand;
    return node;
}

Node *__builtin_ast_cast(Node *expr, Type *target_type) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !expr || !target_type)
        return NULL;

    Node *node = alloc_node(vm, ND_CAST);
    node->lhs  = expr;
    node->ty   = target_type;
    return node;
}

// ============================================================================
// AST Node Construction - Statements
// ============================================================================

Node *__builtin_ast_return(Node *expr) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm)
        return NULL;

    Node *node = alloc_node(vm, ND_RETURN);
    node->lhs  = expr;
    return node;
}

Node *__builtin_ast_block(Node **stmts, int count) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm)
        return NULL;

    Node *node = alloc_node(vm, ND_BLOCK);

    // Link statements together
    Node  head = {};
    Node *cur  = &head;
    for (int i = 0; i < count && stmts[i]; i++) {
        cur = cur->next = stmts[i];
    }
    node->body = head.next;
    return node;
}

Node *__builtin_ast_block_add_stmt(Node *block, Node *stmt) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !block || !stmt || block->kind != ND_BLOCK)
        return NULL;

    stmt->next = NULL;
    if (!block->body) {
        block->body = stmt;
    } else {
        Node *last = block->body;
        while (last->next)
            last = last->next;
        last->next = stmt;
    }
    return block;
}

Node *__builtin_ast_if(Node *cond, Node *then_body, Node *else_body) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !cond)
        return NULL;

    Node *node = alloc_node(vm, ND_IF);
    node->cond = cond;
    node->then = then_body;
    node->els  = else_body;
    return node;
}

// Forward decl: defined further down (Macro Diagnostics section) but needed
// here by __builtin_ast_switch_set_default's duplicate-default check.
void __builtin_macro_error_at(Node *node, const char *fmt, ...);

Node *__builtin_ast_switch(Node *cond) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !cond)
        return NULL;

    Node *node         = alloc_node(vm, ND_SWITCH);
    node->cond         = cond;
    node->case_next    = NULL;
    node->default_case = NULL;
    node->then         = alloc_node(vm, ND_BLOCK);
    return node;
}

void __builtin_ast_switch_add_case(Node *switch_node, Node *value, Node *body) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !switch_node || !value || !body)
        return;

    if (switch_node->kind != ND_SWITCH)
        return;

    // Create a case node. cc_eval constant-folds "value" (it calls add_type
    // itself, so this works even on reflection-built nodes that were never
    // type-checked by the parser) and errors out at the macro call site if
    // it isn't a compile-time constant -- previously this took value->val
    // directly, which silently read 0 for anything but an ND_NUM literal.
    Node   *case_node = alloc_node(vm, ND_CASE);
    int64_t v         = cc_eval(vm, value);
    case_node->begin  = v;
    case_node->end    = v;
    case_node->lhs    = body;

    // #815/#816: reject a case value that collides with one already on the
    // switch, same as the parser does for hand-written switches. Must run
    // before the splice below -- once case_node is linked into case_next it
    // would trivially "collide" with itself.
    check_case_conflict(vm, switch_node->case_next, case_node);

    // Add to switch's case list
    case_node->case_next   = switch_node->case_next;
    switch_node->case_next = case_node;
    __builtin_ast_block_add_stmt(switch_node->then, case_node);
}

void __builtin_ast_switch_set_default(Node *switch_node, Node *body) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !switch_node || !body)
        return;

    if (switch_node->kind != ND_SWITCH)
        return;

    // #815/#816: a second default label silently overwrote the first with no
    // diagnostic; same fix as the parser's registration-time check.
    if (switch_node->default_case)
        __builtin_macro_error_at(switch_node,
                                 "multiple default labels in one switch");

    Node *def                 = alloc_node(vm, ND_CASE);
    def->lhs                  = body;
    switch_node->default_case = def;
    __builtin_ast_block_add_stmt(switch_node->then, def);
}

// ============================================================================
// AST Node Construction - Declarations
// ============================================================================

Node *__builtin_ast_expr_stmt(Node *expr) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm)
        return NULL;

    Node *node = alloc_node(vm, ND_EXPR_STMT);
    node->lhs  = expr;
    return node;
}

// ============================================================================
// Macro Diagnostics (ticket #78)
// ============================================================================

void __builtin_macro_error_at(Node *node, const char *fmt, ...) {
    VirtualMachine *vm = __builtin_current_vm;
    va_list         ap;
    va_start(ap, fmt);
    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (node && node->tok)
        error_tok(vm, node->tok, "%s", buf);
    else
        error("%s", buf); // no source location available
}

void __builtin_macro_warning_at(Node *node, const char *fmt, ...) {
    VirtualMachine *vm = __builtin_current_vm;
    va_list         ap;
    va_start(ap, fmt);
    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (node && node->tok) {
        warn_tok(vm, node->tok, CCCC_WARN_CCCC_MACRO, "%s", buf);
    } else if (vm && (vm->compiler.warnings & CCCC_WARN_CCCC_MACRO)) {
        bool as_error =
            (vm->warnings_as_errors &&
             !(vm->compiler.warning_no_errors & CCCC_WARN_CCCC_MACRO)) ||
            (vm->compiler.warning_errors & CCCC_WARN_CCCC_MACRO);
        if (as_error)
            error("%s", buf);
        else
            fprintf(stderr, "warning: %s [-Wcccc-macro]\n", buf);
    }
}

// ============================================================================
// AST Node Construction - Local Variable Injection (ticket #77)
// ============================================================================

// Allocate a local Obj and prepend it to the current function's locals list.
// Injected variables receive stack offsets later when cc_compile runs.
static Node *make_local_var_node(VirtualMachine *vm, char *name, Type *ty) {
    if (!vm || !ty)
        return NULL;

    Obj *fn = vm->compiler.current_fn;
    if (!fn) {
        // Not inside a function body — cannot inject a local
        return NULL;
    }

    Obj *var      = reflect_new_var(vm, name, strlen(name), ty);
    var->is_local = true;
    // Prepend to vm->compiler.locals, not fn->locals directly.
    // cc_expand_macros flushes vm->compiler.locals back into fn->locals at the
    // end of each function, and push_fn/pop_fn manages this for WithFn blocks.
    var->next           = vm->compiler.locals;
    vm->compiler.locals = var;

    Node *node          = alloc_node(vm, ND_VAR);
    node->var           = var;
    return node;
}

Node *__builtin_ast_local_var(const char *name, Type *ty) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !name || !ty)
        return NULL;

    char *arena_name = arena_format(vm, "%s", name);
    return make_local_var_node(vm, arena_name, ty);
}

Node *__builtin_ast_local_var_unique(Type *ty) {
    VirtualMachine *vm = __builtin_current_vm;
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

Node *__builtin_ast_assign(Node *target, Node *value) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !target || !value)
        return NULL;

    Node *node = alloc_node(vm, ND_ASSIGN);
    node->lhs  = target;
    node->rhs  = value;
    return node;
}

Node *__builtin_ast_member(Node *obj, const char *name) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !obj || !name)
        return NULL;

    // Ensure lhs type is computed (mirrors struct_ref in parse.c:3796)
    add_type(vm, obj);

    Type *ty = obj->ty;
    if (!ty || (ty->kind != TY_STRUCT && ty->kind != TY_UNION))
        return NULL;

    Member *mem = (Member *)__builtin_ast_struct_member_find(ty, name);
    if (!mem)
        return NULL;

    Node *node   = alloc_node(vm, ND_MEMBER);
    node->lhs    = obj;
    node->member = mem;
    return node;
}

Node *__builtin_ast_funcall(Node *callee, Node **args, int n) {
    VirtualMachine *vm = __builtin_current_vm;
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

    Node *node    = alloc_node(vm, ND_FUNCALL);
    node->lhs     = callee;
    node->func_ty = ty;
    node->ty      = ty->return_ty;

    // Chain argument nodes via ->next, resolving each argument's type and
    // casting to the corresponding parameter type (mirrors funcall() in
    // parse.c, including array-to-pointer decay for e.g. memcpy/strlen).
    // CONTRACT: when param_ty is NULL (paramless/variadic callee) or a
    // struct/union param, `arg` is chained WITHOUT a cast, so its ->next is
    // overwritten in place -- callers must pass fresh, non-shared nodes for
    // those argument positions (reusing the same node across multiple
    // funcalls corrupts earlier calls' arg chains).
    Type *param_ty = ty->params;
    Node  head     = {};
    Node *cur      = &head;
    for (int i = 0; i < n && args[i]; i++) {
        Node *arg = args[i];
        add_type(vm, arg);
        if (param_ty) {
            if (param_ty->kind != TY_STRUCT && param_ty->kind != TY_UNION)
                arg = new_cast(vm, arg, param_ty);
            param_ty = param_ty->next;
        } else if (arg->ty && arg->ty->kind == TY_FLOAT) {
            arg = new_cast(vm, arg, ty_double);
        }
        cur = cur->next = arg;
    }
    node->args = head.next;
    return node;
}

// __builtin_ast_while: while(cond) body — represented as ND_FOR with init/inc
// NULL
Node *__builtin_ast_while(Node *cond, Node *body) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !cond)
        return NULL;

    Node *node = alloc_node(vm, ND_FOR);
    node->cond = cond;
    node->then = body;
    // node->init and node->inc left NULL — this is a while loop
    return node;
}

Node *__builtin_ast_for(Node *init, Node *cond, Node *inc, Node *body) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm)
        return NULL;

    Node *node = alloc_node(vm, ND_FOR);
    node->init = init;
    node->cond = cond;
    node->inc  = inc;
    node->then = body;
    return node;
}

Node *__builtin_ast_do_while(Node *body, Node *cond) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !cond)
        return NULL;

    Node *node = alloc_node(vm, ND_DO);
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

// MakeCond(cond, then, else) — ternary ?: expression
Node *__builtin_ast_cond(Node *cond, Node *then_expr, Node *else_expr) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !cond || !then_expr || !else_expr)
        return NULL;
    Node *node = alloc_node(vm, ND_COND);
    node->cond = cond;
    node->then = then_expr;
    node->els  = else_expr;
    // Type is resolved by add_type pass
    return node;
}

// MakeNull() — typed null pointer: (void *)0
Node *__builtin_ast_null(void) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm)
        return NULL;
    Node *zero = __builtin_ast_int_literal(0);
    if (!zero)
        return NULL;
    Node *node = alloc_node(vm, ND_CAST);
    node->lhs  = zero;
    node->ty   = pointer_to(vm, ty_void);
    return node;
}

// MakeSizeofType(ty) — sizeof(ty) as a compile-time integer literal
Node *__builtin_ast_sizeof_type(Type *ty) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !ty)
        return NULL;
    return __builtin_ast_int_literal(ty->size);
}

// MakeAlignofType(ty) — _Alignof(ty) as a compile-time integer literal
Node *__builtin_ast_alignof_type(Type *ty) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !ty)
        return NULL;
    return __builtin_ast_int_literal(ty->align);
}

// MakeSizeofExpr(expr) — sizeof(expr): run add_type then return the size
Node *__builtin_ast_sizeof_expr(Node *expr) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !expr)
        return NULL;
    add_type(vm, expr);
    if (!expr->ty)
        return NULL;
    return __builtin_ast_int_literal(expr->ty->size);
}

// MakeSubscript(arr, idx) — arr[idx], desugared as *(arr + idx * sizeof(*arr))
// Mirrors new_add() in parse.c: the index must be pre-scaled by the element
// size so that codegen emits a plain integer ADD (it does not scale
// internally).
Node *__builtin_ast_subscript(Node *arr, Node *idx) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !arr || !idx)
        return NULL;

    // Resolve types so we can inspect pointer/array base
    add_type(vm, arr);
    add_type(vm, idx);

    // Canonicalize: pointer must be on the left (handle idx + arr too)
    Node *ptr = arr;
    Node *num = idx;
    if (ptr->ty && !ptr->ty->base && num->ty && num->ty->base) {
        ptr = idx;
        num = arr;
    }

    // Scale index by element size for pointer arithmetic
    if (ptr->ty && ptr->ty->base) {
        int elem_size = ptr->ty->base->size;
        // Build: num * elem_size
        Node *scale  = alloc_node(vm, ND_NUM);
        scale->val   = elem_size;
        scale->ty    = ty_long;
        Node *scaled = alloc_node(vm, ND_MUL);
        scaled->lhs  = num;
        scaled->rhs  = scale;
        scaled->ty   = num->ty ? num->ty : ty_long;
        // Build: ptr + scaled
        Node *add = alloc_node(vm, ND_ADD);
        add->lhs  = ptr;
        add->rhs  = scaled;
        add->ty   = ptr->ty; // pointer result type
        // Dereference: *(ptr + scaled)
        Node *deref = alloc_node(vm, ND_DEREF);
        deref->lhs  = add;
        return deref;
    }

    // Fallback: integer subscript (unusual but safe)
    Node *add   = alloc_node(vm, ND_ADD);
    add->lhs    = ptr;
    add->rhs    = num;
    Node *deref = alloc_node(vm, ND_DEREF);
    deref->lhs  = add;
    return deref;
}

// MakeComma(lhs, rhs) — comma expression: evaluate lhs, discard, yield rhs
Node *__builtin_ast_comma(Node *lhs, Node *rhs) {
    return __builtin_ast_binary(ND_COMMA, lhs, rhs);
}

// ============================================================================
// AST Initializer Builders (ticket #296, file-scope path ticket #304)
// ============================================================================

// Write a single constant scalar value to buf[0..ty->size-1].
// Calls cc_eval / cc_eval_double which error() on non-constant expressions.
static void reflect_write_constexpr(VirtualMachine *vm, char *buf, Type *ty,
                                    Node *node) {
    if (is_flonum(ty)) {
        double v = cc_eval_double(vm, node);
        if (ty->kind == TY_FLOAT)
            *(float *)buf = (float)v;
        else
            *(double *)buf = v;
        return;
    }
    int64_t v = cc_eval(vm, node);
    switch (ty->size) {
        case 1:
            *(int8_t *)buf = (int8_t)v;
            break;
        case 2:
            *(int16_t *)buf = (int16_t)v;
            break;
        case 4:
            *(int32_t *)buf = (int32_t)v;
            break;
        case 8:
            *(int64_t *)buf = v;
            break;
        default:
            error_tok(
                vm, node ? node->tok : NULL,
                "unsupported element size %d in file-scope compound literal",
                ty->size);
    }
}

// Create an anonymous static global var for a file-scope compound literal.
// Returns ND_VAR(anon_gvar); gen() will copy init_data into the data segment.
static Node *make_gvar_compound_literal(VirtualMachine *vm, Type *ty,
                                        Node **inits, int n) {
    Obj *var       = reflect_new_anon_gvar(vm, ty);
    var->is_static = true;
    // #928: mark this reachable via the -c=generated emit-event walk
    // (cc_record_emit_object, macros.c) the same way MakeFunction/MakeGlobalVar
    // are -- without this, -c=generated never emits a definition for the anon
    // gvar this node references.
    var->is_macro_generated = true;

    char *buf               = arena_alloc(&vm->compiler.parser_arena, ty->size);
    memset(buf, 0, ty->size);
    var->init_data = buf;

    if (ty->kind == TY_ARRAY) {
        int elem_sz = ty->base->size;
        for (int i = 0; i < n && i < ty->array_len; i++) {
            if (!inits[i])
                continue;
            reflect_write_constexpr(vm, buf + i * elem_sz, ty->base, inits[i]);
        }
    } else if (ty->kind == TY_STRUCT) {
        int i = 0;
        for (Member *m = ty->members; m && i < n; m = m->next) {
            if (!m->name) {
                i++;
                continue;
            }
            if (!inits[i]) {
                i++;
                continue;
            }
            reflect_write_constexpr(vm, buf + m->offset, m->ty, inits[i]);
            i++;
        }
    } else if (n > 0 && inits[0]) {
        reflect_write_constexpr(vm, buf, ty, inits[0]);
    }

    Node *node = alloc_node(vm, ND_VAR);
    node->var  = var;
    node->ty   = ty;
    return node;
}

// CompoundLiteral(ty, ...) — positional compound literal: zero + assign chain
// Mirrors parse.c:4375 compound literal lowering.
// At function scope: emits stack-var + ND_ASSIGN chain (supports non-constant
// values). At file scope: emits static anon gvar with constant init_data
// (ticket #304).
Node *__builtin_ast_compound_literal(Type *ty, Node **inits, int n) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !ty)
        return NULL;
    Token *tok = vm->compiler.macro_call_tok;
    Obj   *fn  = vm->compiler.current_fn;
    if (!fn)
        return make_gvar_compound_literal(vm, ty, inits, n);

    char *name    = reflect_unique_name(vm);
    Obj  *var     = reflect_new_var(vm, name, strlen(name), ty);
    var->is_local = true;
    // Prepend to vm->compiler.locals (not fn->locals) so cc_expand_macros
    // picks up the new var when it flushes: fn->locals = vm->compiler.locals.
    var->next           = vm->compiler.locals;
    vm->compiler.locals = var;

    Node *zero          = alloc_node(vm, ND_MEMZERO);
    zero->var           = var;

    Node *chain         = NULL;
    if (n > 0 && inits) {
        for (int i = 0; i < n - 1; i++)
            if (inits[i])
                inits[i]->next = inits[i + 1];
        if (inits[n - 1])
            inits[n - 1]->next = NULL;
        chain = inits[0];
    }

    Node *splice             = alloc_node(vm, ND_INIT_SPLICE);
    splice->tok              = tok;
    splice->var              = var;
    splice->init_start_index = 0;
    Node *assignments        = node_expand_init_splice(vm, splice, chain);

    Node *init_comma         = alloc_node(vm, ND_COMMA);
    init_comma->lhs          = zero;
    init_comma->rhs          = assignments;

    Node *var_ref            = alloc_node(vm, ND_VAR);
    var_ref->var             = var;
    var_ref->ty              = ty;

    Node *result             = alloc_node(vm, ND_COMMA);
    result->lhs              = init_comma;
    result->rhs              = var_ref;
    add_type(vm, result);
    return result;
}

// InitArray(elem_ty, ...) — array compound literal with explicit element type
Node *__builtin_ast_init_array(Type *elem_ty, Node **elems, int n) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !elem_ty || !elems || n <= 0)
        return NULL;
    Type *arr_ty = array_of(vm, elem_ty, n);
    return __builtin_ast_compound_literal(arr_ty, elems, n);
}

// InitStruct(ty, fields, values, n) — designated struct/union init
// Partial init is fine: unmentioned fields remain zero.
// At file scope: emits static anon gvar with constant init_data (ticket #304).
Node *__builtin_ast_init_struct(Type *ty, const char **fields, Node **values,
                                int n) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !ty || n <= 0)
        return NULL;
    if (ty->kind != TY_STRUCT && ty->kind != TY_UNION)
        return NULL;
    Obj *fn = vm->compiler.current_fn;
    if (!fn) {
        // File scope: static anon gvar with constant init_data
        Obj *var       = reflect_new_anon_gvar(vm, ty);
        var->is_static = true;
        // #928: see make_gvar_compound_literal()'s identical comment --
        // needed so the -c=generated emit-event walk ever emits a definition
        // for this.
        var->is_macro_generated = true;
        char *buf = arena_alloc(&vm->compiler.parser_arena, ty->size);
        memset(buf, 0, ty->size);
        var->init_data = buf;
        for (int i = 0; i < n; i++) {
            if (!fields[i] || !values[i])
                continue;
            Member *mem =
                (Member *)__builtin_ast_struct_member_find(ty, fields[i]);
            if (!mem)
                return NULL;
            reflect_write_constexpr(vm, buf + mem->offset, mem->ty, values[i]);
        }
        Node *node = alloc_node(vm, ND_VAR);
        node->var  = var;
        node->ty   = ty;
        return node;
    }

    char *name          = reflect_unique_name(vm);
    Obj  *var           = reflect_new_var(vm, name, strlen(name), ty);
    var->is_local       = true;
    var->next           = vm->compiler.locals;
    vm->compiler.locals = var;

    Node *zero          = alloc_node(vm, ND_MEMZERO);
    zero->var           = var;

    // Emit assignments only for the specified fields; ND_MEMZERO handles the
    // rest.
    Node *assignments = alloc_node(vm, ND_NULL_EXPR);
    for (int i = 0; i < n; i++) {
        if (!fields[i] || !values[i])
            continue;
        Member *mem = (Member *)__builtin_ast_struct_member_find(ty, fields[i]);
        if (!mem)
            return NULL;

        Node *var_ref    = alloc_node(vm, ND_VAR);
        var_ref->var     = var;
        var_ref->ty      = ty;

        Node *mem_node   = alloc_node(vm, ND_MEMBER);
        mem_node->lhs    = var_ref;
        mem_node->member = mem;
        add_type(vm, mem_node);

        Node *assign = alloc_node(vm, ND_ASSIGN);
        assign->lhs  = mem_node;
        assign->rhs  = values[i];
        add_type(vm, assign);

        Node *comma = alloc_node(vm, ND_COMMA);
        comma->lhs  = assignments;
        comma->rhs  = assign;
        assignments = comma;
    }

    Node *init_comma = alloc_node(vm, ND_COMMA);
    init_comma->lhs  = zero;
    init_comma->rhs  = assignments;

    Node *var_ref    = alloc_node(vm, ND_VAR);
    var_ref->var     = var;
    var_ref->ty      = ty;

    Node *result     = alloc_node(vm, ND_COMMA);
    result->lhs      = init_comma;
    result->rhs      = var_ref;
    add_type(vm, result);
    return result;
}

// ============================================================================
// Serialization (ticket #235)
// ============================================================================

// Recursively emit memcpy() calls copying each scalar/pointer leaf member of
// `ty` (starting from `expr`) into `buf_char[base_offset + member->offset]`.
// Nested struct/union members are recursed into (flat serialization).
// Bitfields are skipped (TODO #<follow-up>: bit-packed serialization).
static void reflect_serialize_members(VirtualMachine *vm, Node *block, Type *ty,
                                      Node *expr, Node *buf_char,
                                      int base_offset) {
    for (Member *m = ty->members; m; m = m->next) {
        if (!m->name || m->is_bitfield)
            continue;

        Node *field = __builtin_ast_member(expr, __builtin_ast_member_name(m));
        int   off   = base_offset + m->offset;

        if (m->ty->kind == TY_STRUCT || m->ty->kind == TY_UNION) {
            reflect_serialize_members(vm, block, m->ty, field, buf_char, off);
            continue;
        }

        Node *dst = __builtin_ast_unary(
            ND_ADDR,
            __builtin_ast_subscript(buf_char, __builtin_ast_int_literal(off)));
        Node *src  = __builtin_ast_unary(ND_ADDR, field);
        Node *call = __builtin_ast_funcall(
            __builtin_ast_var_ref("memcpy"),
            (Node *[]){dst, src, __builtin_ast_int_literal(m->ty->size)}, 3);
        __builtin_ast_block_add_stmt(block, __builtin_ast_expr_stmt(call));
    }
}

// Serialize(ty, expr, buf) — build a block of memcpy() calls copying `expr`
// (of type `ty`) byte-for-byte into `buf` (void*/char*). For struct/union
// types, copies each scalar/pointer leaf member at its natural offset
// (recursing into nested flat structs); for scalar types, copies the whole
// value in one memcpy. V1 placeholder: pointer-typed members are copied as
// raw pointer bytes, not followed (TODO #<follow-up>: deep/pointer-aware
// serialization).
Node *__builtin_ast_serialize(Type *ty, Node *expr, Node *buf) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !ty || !expr || !buf)
        return NULL;

    add_type(vm, expr);
    add_type(vm, buf);

    Node *block    = __builtin_ast_block(NULL, 0);
    Node *buf_char = __builtin_ast_cast(buf, pointer_to(vm, ty_char));

    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        reflect_serialize_members(vm, block, ty, expr, buf_char, 0);
    } else {
        Node *src  = __builtin_ast_unary(ND_ADDR, expr);
        Node *call = __builtin_ast_funcall(
            __builtin_ast_var_ref("memcpy"),
            (Node *[]){buf, src, __builtin_ast_int_literal(ty->size)}, 3);
        __builtin_ast_block_add_stmt(block, __builtin_ast_expr_stmt(call));
    }

    add_type(vm, block);
    return block;
}

// Deserialize(ty, buf) — reinterpret `buf` (void*/char*) as a `ty` value:
// *(ty*)buf. V1 placeholder: this is a cast+deref, so it inherits the host's
// alignment requirements for `ty` (TODO #<follow-up>: field-by-field
// reconstruction if Serialize ever produces a packed/portable layout).
Node *__builtin_ast_deserialize(Type *ty, Node *buf) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !ty || !buf)
        return NULL;

    add_type(vm, buf);
    Node *typed_ptr = __builtin_ast_cast(buf, pointer_to(vm, ty));
    return __builtin_ast_unary(ND_DEREF, typed_ptr);
}

// EnumToString(ty, expr) — builds switch (expr) { case V0: return "Name0";
// ... default: return ""; }. Caller wraps the result in a function returning
// const char*.
Node *__builtin_ast_enum_to_string_switch(Type *ty, Node *expr) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !ty || !expr)
        return NULL;

    add_type(vm, expr);

    Node *sw = __builtin_ast_switch(expr);
    int   n  = __builtin_ast_enum_value_count(ty);
    for (int i = 0; i < n; i++) {
        const char *name       = __builtin_ast_enum_value_name(ty, i);
        int64_t     val        = __builtin_ast_enum_value(ty, i);
        Node       *value_node = __builtin_ast_int_literal(val);
        Node *body = __builtin_ast_return(__builtin_ast_string_literal(name));
        __builtin_ast_switch_add_case(sw, value_node, body);
    }
    __builtin_ast_switch_set_default(
        sw, __builtin_ast_return(__builtin_ast_string_literal("")));

    return sw;
}

// EnumFromString(ty, expr) — builds a block of
// `if (strcmp(expr, "Name0") == 0) return Value0; ... return -1;`. Caller
// wraps the result in a function returning the enum type (or an int).
Node *__builtin_ast_enum_from_string_chain(Type *ty, Node *expr) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !ty || !expr)
        return NULL;

    add_type(vm, expr);

    Node *block = __builtin_ast_block(NULL, 0);
    int   n     = __builtin_ast_enum_value_count(ty);
    for (int i = 0; i < n; i++) {
        const char *name    = __builtin_ast_enum_value_name(ty, i);
        int64_t     val     = __builtin_ast_enum_value(ty, i);

        Node       *str_lit = __builtin_ast_string_literal(name);
        Node       *cmp = __builtin_ast_funcall(__builtin_ast_var_ref("strcmp"),
                                                (Node *[]){expr, str_lit}, 2);
        Node       *cond =
            __builtin_ast_binary(ND_EQ, cmp, __builtin_ast_int_literal(0));
        Node *then    = __builtin_ast_return(__builtin_ast_int_literal(val));
        Node *if_node = __builtin_ast_if(cond, then, NULL);
        __builtin_ast_block_add_stmt(block, if_node);
    }
    __builtin_ast_block_add_stmt(
        block, __builtin_ast_return(__builtin_ast_int_literal(-1)));

    add_type(vm, block);
    return block;
}

// ============================================================================
// AST Type Construction - Qualified Types (ticket #171)
// ============================================================================

// MakeConst(ty) — return a const-qualified copy of ty
Type *__builtin_ast_make_const(Type *ty) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !ty)
        return NULL;
    Type *result     = copy_type(vm, ty);
    result->is_const = true;
    return result;
}

// MakeVolatile(ty) — return a volatile-qualified copy of ty
Type *__builtin_ast_make_volatile(Type *ty) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !ty)
        return NULL;
    Type *result        = copy_type(vm, ty);
    result->is_volatile = true;
    return result;
}

// ============================================================================
// Function Generation
// ============================================================================

// Helper to create a function type, optionally with a parameter list. Each
// entry in `params` must be either a fresh/owned Type (e.g. the result of
// pointer_to() or copy_type()) since they get chained via ->next here --
// never pass a shared singleton (ty_int, ty_long, ...) directly.
static Type *make_func_type_params(VirtualMachine *vm, Type *return_type,
                                   Type **params, int nparams) {
    Type *ty = arena_alloc(&vm->compiler.parser_arena, sizeof(Type));
    memset(ty, 0, sizeof(Type));
    ty->kind      = TY_FUNC;
    ty->return_ty = return_type;
    ty->size      = 8;
    ty->align     = 8;

    Type  head    = {0};
    Type *cur     = &head;
    for (int i = 0; i < nparams; i++) {
        cur->next = params[i];
        cur       = cur->next;
    }
    ty->params = head.next;
    return ty;
}

static Type *make_func_type(VirtualMachine *vm, Type *return_type) {
    return make_func_type_params(vm, return_type, NULL, 0);
}

// __builtin_ast_make_func_ptr_type: builds a pointer-to-function type, e.g.
// `T (*)(T)`, suitable for FunctionAddParam (the callback parameters of
// GenerateMap/reduce/filter). Each entry in param_types is copy_type()'d
// before being chained via ->next, so passing shared singletons (ty_int, a
// struct's own elem_ty, ...) here never mutates them.
Type *__builtin_ast_make_func_ptr_type(Type *return_ty, Type **param_types,
                                       int nparams) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !return_ty)
        return NULL;

    Type *copies[16];
    if (nparams > 16)
        nparams = 16;
    for (int i = 0; i < nparams; i++)
        copies[i] = copy_type(vm, param_types[i]);

    Type *fn_ty = make_func_type_params(vm, return_ty, copies, nparams);
    return pointer_to(vm, fn_ty);
}

// Synthesize a minimal extern declaration for a libc function directly into
// vm->compiler.globals, so __builtin_ast_var_ref/__builtin_ast_funcall can
// build calls to it (e.g. for Serialize's memcpy) even when the target TU
// doesn't #include <string.h>. The FFI registration for these functions is
// unconditional (register_string_functions); only the AST-level Obj is
// missing without the #include.
//
// A real parameter list is required (not just the return type): without it,
// __builtin_ast_funcall() skips arg-casting (no fresh nodes), so a reused arg
// node's ->next gets overwritten by each successive call built from it.
// #1050: no longer records native-header provenance itself -- that's now
// centralized in register_synth_libc_call() (above), reached uniformly via
// var_ref_lookup() regardless of whether a name resolves through this
// synthesis path or through reflection.h's own #include <string.h> (see
// that function's comment for why both shapes exist).
static void ensure_libc_fn_decl(VirtualMachine *vm, const char *name,
                                Type *return_ty, Type **params, int nparams) {
    if (__builtin_ast_find_global(name))
        return;

    Obj *fn = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
    memset(fn, 0, sizeof(Obj));
    fn->name        = arena_strdup(vm, name);
    fn->ty          = make_func_type_params(vm, return_ty, params, nparams);
    fn->align       = 8;
    fn->is_function = true;
    fn->next        = vm->compiler.globals;
    vm->compiler.globals = fn;
}

// Ticket #235: ensure memcpy/memmove/memcmp/strlen/strcmp are resolvable via
// __builtin_ast_var_ref in the runtime TU's globals, for Serialize/Deserialize
// and future EnumToString/EnumFromString-style generators.
void __builtin_ensure_string_h_decls(void) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm)
        return;

    Type *voidp_a = pointer_to(vm, ty_void);
    Type *voidp_b = pointer_to(vm, ty_void);
    Type *size1   = copy_type(vm, ty_ulong);
    ensure_libc_fn_decl(vm, "memcpy", pointer_to(vm, ty_void),
                        (Type *[]){voidp_a, voidp_b, size1}, 3);

    voidp_a = pointer_to(vm, ty_void);
    voidp_b = pointer_to(vm, ty_void);
    size1   = copy_type(vm, ty_ulong);
    ensure_libc_fn_decl(vm, "memmove", pointer_to(vm, ty_void),
                        (Type *[]){voidp_a, voidp_b, size1}, 3);

    voidp_a = pointer_to(vm, ty_void);
    voidp_b = pointer_to(vm, ty_void);
    size1   = copy_type(vm, ty_ulong);
    ensure_libc_fn_decl(vm, "memcmp", ty_int,
                        (Type *[]){voidp_a, voidp_b, size1}, 3);

    Type *charp = pointer_to(vm, ty_char);
    ensure_libc_fn_decl(vm, "strlen", copy_type(vm, ty_ulong),
                        (Type *[]){charp}, 1);

    charp        = pointer_to(vm, ty_char);
    Type *charp2 = pointer_to(vm, ty_char);
    ensure_libc_fn_decl(vm, "strcmp", ty_int, (Type *[]){charp, charp2}, 2);
}

Obj *__builtin_ast_function(const char *name, Type *return_type) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !name || !return_type)
        return NULL;

    // Check if there's already a forward declaration for this function
    size_t name_len = strlen(name);
    Obj   *existing = NULL;

    Obj   *lists[]  = {vm->compiler.globals, vm->compiler.macro_globals};
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
        existing->is_definition      = true;
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
    fn->name               = arena_strdup(vm, name);
    fn->ty                 = func_type;
    fn->align              = 8;
    fn->is_function        = true;
    fn->is_definition      = true;
    fn->is_static          = false;
    fn->is_macro_generated = true;

    // Add to globals list. The function is not made visible to the parser
    // until source declares it, inline macro prototype synthesis declares it,
    // or __builtin_ast_publish() publishes it explicitly.
    fn->next             = vm->compiler.globals;
    vm->compiler.globals = fn;

    return fn;
}

static Node *reflect_noop_node(VirtualMachine *vm) {
    Node *noop = alloc_node(vm, ND_NULL_EXPR);
    noop->ty   = ty_void;
    return noop;
}

Node *__builtin_ast_publish(Obj *obj, Token *tok) {
    VirtualMachine *vm = __builtin_current_vm;
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
                  obj->is_function ? "function" : "global variable", obj->name);
        }
    }

    VarScopeNode *decl =
        arena_alloc(&vm->compiler.parser_arena, sizeof(VarScopeNode));
    memset(decl, 0, sizeof(VarScopeNode));
    decl->name               = obj->name;
    decl->name_len           = name_len;
    decl->var                = obj;
    decl->next               = vm->compiler.scope->vars;
    vm->compiler.scope->vars = decl;
    hashmap_put2_borrowed(&vm->compiler.scope->var_map, decl->name,
                          decl->name_len, decl);
    if (vm->compiler.macro_emit_recording)
        cc_record_emit_object(vm, obj);

    return reflect_noop_node(vm);
}

void __builtin_emit_directive(const char *line) {
    VirtualMachine *vm = __builtin_current_vm;
    cc_record_emit_source(vm, line);
}

Node *__builtin_ast_publish_type(Type *ty, Token *tok) {
    VirtualMachine *vm = __builtin_current_vm;
    (void)ty;
    (void)tok;
    if (!vm)
        return NULL;
    return reflect_noop_node(vm);
}

void __builtin_ast_function_add_param(Obj *fn, const char *name, Type *type) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !fn || !name || !type)
        return;

    // Create parameter local variable
    Obj *param = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
    memset(param, 0, sizeof(Obj));
    param->name     = arena_strdup(vm, name);
    param->ty       = type;
    param->align    = type->align;
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

// #996: a block literal (^{ ... }) lifted while building fn's body outside a
// WithFn(fn) block never learns its enclosing function -- block_literal()
// (src/parse.c) sets block_fn->parent_fn from vm->compiler.current_fn at
// parse time, which is NULL here (see the adoption comment below). Walk the
// finished body for ND_BLOCK_LITERAL nodes and backfill parent_fn/
// nesting_depth on any block_fn that's still unlinked, so it matches exactly
// what WithFn(fn) would have produced -- this is what lets
// belongs_to_outer_function()/calculate_chain_depth() (src/codegen.c) find a
// captured outer variable through the static chain. Mirrors the traversal
// shape of collect_node_types() (src/serialize_type.c).
static void relink_orphan_block_parents(Node *node, Obj *fn) {
    if (!node)
        return;

    if (node->kind == ND_BLOCK_LITERAL && node->block_fn) {
        if (node->block_fn->parent_fn == NULL) {
            node->block_fn->parent_fn     = fn;
            node->block_fn->nesting_depth = fn->nesting_depth + 1;
        }
        // #995: a lifted block function is created via new_gvar() and never
        // gets is_macro_generated set at parse time (block_literal() has no
        // way to know it's being parsed inside a macro-generated body --
        // that only becomes true once __builtin_ast_function_set_body
        // adopts this body onto a generated fn, here). Propagate it so the
        // block's own definition is recorded as an emit event
        // (cc_record_emit_object, macros.c) the same way MakeFunction/
        // PublishNode does -- otherwise -c=generated silently drops the
        // lifted function's body while still emitting calls to it.
        // Recurse into the block's own body (not reached by the generic
        // node->body walk below -- that field holds the *block literal
        // expression's* children, the block function's body lives on
        // block_fn->body) so a block nested inside another generated block
        // is propagated too.
        node->block_fn->is_macro_generated |= fn->is_macro_generated;
        if (fn->is_macro_generated)
            relink_orphan_block_parents(node->block_fn->body, node->block_fn);
    }

    if (node->kind == ND_SWITCH) {
        relink_orphan_block_parents(node->cond, fn);
        for (Node *c = node->case_next; c; c = c->case_next)
            relink_orphan_block_parents(c->lhs, fn);
        if (node->default_case)
            relink_orphan_block_parents(node->default_case->lhs, fn);
        relink_orphan_block_parents(node->next, fn);
        return;
    }

    if (node->kind == ND_CASE) {
        relink_orphan_block_parents(node->lhs, fn);
        relink_orphan_block_parents(node->next, fn);
        return;
    }

    relink_orphan_block_parents(node->lhs, fn);
    relink_orphan_block_parents(node->rhs, fn);
    relink_orphan_block_parents(node->cond, fn);
    relink_orphan_block_parents(node->then, fn);
    relink_orphan_block_parents(node->els, fn);
    relink_orphan_block_parents(node->init, fn);
    relink_orphan_block_parents(node->inc, fn);
    relink_orphan_block_parents(node->body, fn);
    relink_orphan_block_parents(node->args, fn);

    relink_orphan_block_parents(node->next, fn);
}

// #997: returns true if obj is present on the linked list starting at
// candidates, by pointer identity.
static bool obj_in_list(Obj *obj, Obj *candidates) {
    for (Obj *o = candidates; o; o = o->next)
        if (o == obj)
            return true;
    return false;
}

// #997: walk fn's finished body for every local Obj it actually references,
// and return the first one found still sitting on stray_locals (the calling
// function's vm->compiler.locals list, per the caller's comment) rather than
// already attached to fn (fn->locals/fn->params). Mirrors
// relink_orphan_block_parents()'s traversal shape. Recurses into a nested
// block literal's own body via block_fn->body (not reached by the generic
// node->body walk below -- see the comment on relink_orphan_block_parents).
// Detection only, by design: a node kind this walk doesn't know about is
// silently not reported, same as before this fix existed, rather than risk
// a false negative turning into a false positive on unrelated code.
static Obj *collect_stray_body_local(Node *node, Obj *stray_locals, Obj *fn) {
    if (!node)
        return NULL;

    Obj *found = NULL;
    if ((node->kind == ND_VAR || node->kind == ND_VLA_PTR) && node->var &&
        obj_in_list(node->var, stray_locals) &&
        !obj_in_list(node->var, fn->locals) &&
        !obj_in_list(node->var, fn->params))
        found = node->var;

    if (!found && node->kind == ND_BLOCK_LITERAL) {
        if (node->block_desc_var &&
            obj_in_list(node->block_desc_var, stray_locals) &&
            !obj_in_list(node->block_desc_var, fn->locals) &&
            !obj_in_list(node->block_desc_var, fn->params))
            found = node->block_desc_var;
        if (!found && node->block_fn)
            found = collect_stray_body_local(node->block_fn->body, stray_locals,
                                             node->block_fn);
    }
    if (found)
        return found;

    if (node->kind == ND_SWITCH) {
        if ((found = collect_stray_body_local(node->cond, stray_locals, fn)))
            return found;
        for (Node *c = node->case_next; c; c = c->case_next)
            if ((found = collect_stray_body_local(c->lhs, stray_locals, fn)))
                return found;
        if (node->default_case &&
            (found = collect_stray_body_local(node->default_case->lhs,
                                              stray_locals, fn)))
            return found;
        return collect_stray_body_local(node->next, stray_locals, fn);
    }

    if (node->kind == ND_CASE) {
        if ((found = collect_stray_body_local(node->lhs, stray_locals, fn)))
            return found;
        return collect_stray_body_local(node->next, stray_locals, fn);
    }

    if ((found = collect_stray_body_local(node->lhs, stray_locals, fn)) ||
        (found = collect_stray_body_local(node->rhs, stray_locals, fn)) ||
        (found = collect_stray_body_local(node->cond, stray_locals, fn)) ||
        (found = collect_stray_body_local(node->then, stray_locals, fn)) ||
        (found = collect_stray_body_local(node->els, stray_locals, fn)) ||
        (found = collect_stray_body_local(node->init, stray_locals, fn)) ||
        (found = collect_stray_body_local(node->inc, stray_locals, fn)) ||
        (found = collect_stray_body_local(node->body, stray_locals, fn)) ||
        (found = collect_stray_body_local(node->args, stray_locals, fn)))
        return found;

    return collect_stray_body_local(node->next, stray_locals, fn);
}

void __builtin_ast_function_set_body(Obj *fn, Node *body) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !fn || !body)
        return;

    // #1242: a QuoteLazy() fragment passed directly to FunctionSetBody
    // (rather than spliced into an outer Quote()/QuoteLazy() template) is
    // materialised here, in the current parser context -- WithFn(fn)'s
    // context when wrapped, matching the eager-Quote() convention
    // documented in "Quote inside generated function bodies".
    if (body->kind == ND_QUOTE_LAZY) {
        body = cc_quote_expand_lazy(vm, body, /*want_stmt=*/true);
        if (!body)
            return;
    }

    // If body is not already a block, wrap it
    if (body->kind != ND_BLOCK) {
        Node *block = alloc_node(vm, ND_BLOCK);
        block->body = body;
        fn->body    = block;
    } else {
        fn->body = body;
    }

    // CRITICAL: Run add_type on the body to assign types to all nodes
    // This is necessary for code generation to work correctly
    add_type(vm, fn->body);

    // #996: locals declared while building this body (e.g. `int x = 1;`
    // inside a Quote() template, or MakeLocalVar/MakeCompoundLiteral) are
    // always prepended to vm->compiler.locals, never to fn->locals directly
    // (src/parse.c's new_gvar-adjacent new_lvar, and the reflection.c
    // prepend sites feeding __builtin_ast_local_var/compound-literal
    // helpers). Two mechanisms normally move them onto the owning
    // function's own list: cc_expand_macros's per-function loop
    // (src/macros.c) and push_fn/pop_fn behind WithFn(fn) (below in this
    // file). A file-scope comptime macro calling
    // MakeFunction()+FunctionSetBody(fn, Quote(...)) without WithFn hits
    // neither, so fn->locals stayed NULL and every local in the body kept
    // its default offset of 0 -- they all aliased the same frame slot,
    // corrupting anything sharing that slot (a lifted block's own invoke
    // pointer, in the ticket's repro) and, separately, leaving -m/-c=native
    // output referencing undeclared variables.
    //
    // Adopt vm->compiler.locals onto fn->locals here, but ONLY when
    // current_fn == NULL. cc_expand_macros deliberately clears both
    // current_fn and locals after its own per-function loop (see the
    // comment there) specifically so a stray new_lvar in a global-init
    // comptime context can't silently attach to the last function's frame
    // -- so current_fn == NULL is the guarantee that nobody else already
    // owns this list, and adopting it wholesale can't steal a local out
    // from under another function. When current_fn != NULL (a macro
    // invoked from inside an ordinary function that itself builds another
    // function's body) the list genuinely belongs to that calling
    // function; left alone and filed as #997 -- that case needs a
    // different mechanism (an AST walk to tell the two sources of locals
    // apart) rather than being folded in here.
    //
    // Appended (not replaced) so a second FunctionSetBody call on the same
    // fn, or a body assembled across several builder calls, accumulates
    // rather than dropping an earlier batch.
    if (vm->compiler.current_fn == NULL && vm->compiler.locals != NULL) {
        Obj *new_locals     = vm->compiler.locals;
        vm->compiler.locals = NULL;

        if (fn->locals == NULL) {
            fn->locals = new_locals;
        } else {
            Obj *tail = fn->locals;
            while (tail->next)
                tail = tail->next;
            tail->next = new_locals;
        }
    }

    // #995: propagate is_macro_generated onto any block literal lifted
    // while parsing this body (block_literal(), src/parse.c, has no way to
    // know at parse time that its enclosing body will end up attached to a
    // generated function), and backfill parent_fn/nesting_depth -- both
    // needed regardless of which branch above ran (a body assembled under
    // WithFn(fn) needs the propagation too, not just the adopted-locals
    // case), so this call is unconditional rather than nested inside the
    // guard above.
    relink_orphan_block_parents(fn->body, fn);

    // #997: current_fn != NULL means a comptime macro invoked from *inside*
    // an ordinary function is building fn's body without wrapping the call
    // in WithFn(fn) -- vm->compiler.locals genuinely belongs to current_fn
    // here (its own temps), not to fn, so it can't be adopted the way the
    // current_fn == NULL branch above does. But if the body just attached
    // to fn actually declares its own locals, they were still prepended
    // onto current_fn's list (every new_lvar call always prepends there,
    // see the comment above) and now sit stranded, misattached to the
    // wrong function's frame -- the exact #996 offset-aliasing hazard, one
    // frame-layout change away from a SIGBUS. Detect (don't attempt to
    // move: telling apart current_fn's own temps from fn's newly-declared
    // locals on one shared list needs an AST walk of fn's finished body,
    // and a missed node kind there would produce a *partial* move --  one
    // local's offset assigned in fn's frame, another still in current_fn's
    // -- genuine aliasing, worse than today's uniformly-wrong behavior) and
    // hard-error naming the fix instead.
    if (vm->compiler.current_fn != NULL && vm->compiler.current_fn != fn &&
        vm->compiler.locals != NULL) {
        Obj *stray =
            collect_stray_body_local(fn->body, vm->compiler.locals, fn);
        if (stray) {
            // error_tok (used elsewhere in this file, e.g. #969's
            // __builtin_pc_* rejection) needs a real token to report a
            // location; vm->compiler.macro_call_tok is NULL when this
            // builtin was reached other than via a live macro-call frame
            // (should not happen here in practice, but bare error() is the
            // same fallback shape #969 uses).
            if (vm->compiler.macro_call_tok)
                error_tok(vm, vm->compiler.macro_call_tok,
                          "FunctionSetBody: local '%s' was declared for "
                          "function '%s' while building it from inside "
                          "function '%s'; wrap the call in WithFn(%s) { "
                          "FunctionSetBody(...); }",
                          stray->name, fn->name,
                          vm->compiler.current_fn->name
                              ? vm->compiler.current_fn->name
                              : "<anonymous>",
                          fn->name);
            error("FunctionSetBody: local '%s' was declared for function "
                  "'%s' while building it from inside function '%s'; wrap "
                  "the call in WithFn(%s) { FunctionSetBody(...); }",
                  stray->name, fn->name,
                  vm->compiler.current_fn->name ? vm->compiler.current_fn->name
                                                : "<anonymous>",
                  fn->name);
        }
    }

    // Mark as having a definition
    fn->is_definition = true;
}

void __builtin_ast_function_set_static(Obj *fn, bool is_static) {
    if (fn)
        fn->is_static = is_static;
}

void __builtin_ast_function_set_inline(Obj *fn, bool is_inline) {
    if (fn)
        fn->is_inline = is_inline;
}

void __builtin_ast_function_set_variadic(Obj *fn, bool is_variadic) {
    if (fn && fn->ty)
        fn->ty->is_variadic = is_variadic;
}

// FunctionPrototype(name, ret) — create a forward declaration (no body).
// The same params API (FunctionAddParam) applies; use
// PublishNode to make it visible in scope.
Obj *__builtin_ast_function_prototype(const char *name, Type *return_type) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !name || !return_type)
        return NULL;

    size_t name_len = strlen(name);

    // If a forward-declaration or prototype already exists, return it.
    Obj *lists[] = {vm->compiler.globals, vm->compiler.macro_globals};
    for (int i = 0; i < 2; i++) {
        for (Obj *obj = lists[i]; obj; obj = obj->next) {
            if (obj->is_function && strlen(obj->name) == name_len &&
                strncmp(obj->name, name, name_len) == 0) {
                return obj;
            }
        }
    }

    Type *func_type = make_func_type(vm, return_type);

    Obj  *fn        = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
    memset(fn, 0, sizeof(Obj));
    fn->name               = arena_strdup(vm, name);
    fn->ty                 = func_type;
    fn->align              = 8;
    fn->is_function        = true;
    fn->is_definition      = false; // prototype, no body
    fn->is_static          = false;
    fn->is_macro_generated = true;

    fn->next               = vm->compiler.globals;
    vm->compiler.globals   = fn;
    return fn;
}

// ============================================================================
// Programmatic Attribute Application — AddAttribute (ticket #619)
// ============================================================================

// Apply an attribute string to an AST-generated function.
// Delegates to cc_apply_attr_to_fn in parse.c which handles mode attrs,
// standard C23/GNU attrs, and custom @attrs uniformly.
void __builtin_ast_add_attribute(Obj *fn, const char *attr_text) {
    VirtualMachine *vm = __builtin_current_vm;
    cc_apply_attr_to_fn(vm, fn, attr_text, vm->compiler.macro_call_tok);
}

// Thin helper for MarkAsBuildTarget(fn, kind) — composes the kind string at
// runtime so the macro doesn't need string concatenation.
void __builtin_ast_add_build_target_attr(Obj *fn, const char *kind) {
    char buf[64];
    snprintf(buf, sizeof(buf), "cccc::build_target(kind=%s)",
             kind ? kind : "native");
    __builtin_ast_add_attribute(fn, buf);
}

// ============================================================================
// Global Variable Generation (ticket #152)
// ============================================================================

// Create a new named global variable.  The type determines layout; use
// __builtin_ast_make_array(char_ty, len) to get a char[len] type so that
// the codegen init_data copy (codegen.c) copies the right number of bytes.
Obj *__builtin_ast_global_var(const char *name, Type *ty) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !name || !ty)
        return NULL;

    size_t name_len = strlen(name);

    // Reuse an existing forward declaration if present (same logic as
    // __builtin_ast_function).
    for (Obj *obj = vm->compiler.globals; obj; obj = obj->next) {
        if (!obj->is_function && strlen(obj->name) == name_len &&
            strncmp(obj->name, name, name_len) == 0) {
            if (obj->is_definition)
                error("expected unique generated global name, got existing "
                      "definition '%s'",
                      name);
            obj->is_definition      = true;
            obj->is_macro_generated = true;
            obj->ty                 = ty;
            obj->align              = ty->align;
            if (vm->compiler.macro_emit_recording)
                cc_record_emit_object(vm, obj);
            return obj;
        }
    }

    Obj *var = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
    memset(var, 0, sizeof(Obj));
    var->name          = arena_strdup(vm, name);
    var->ty            = ty;
    var->align         = ty->align;
    var->is_function   = false;
    var->is_definition = true;
    var->is_static =
        false; // default: external linkage; call _set_static to change
    var->is_macro_generated = true;

    var->next               = vm->compiler.globals;
    vm->compiler.globals    = var;
    if (vm->compiler.macro_emit_recording)
        cc_record_emit_object(vm, var);
    return var;
}

// Set the initial data for a global variable.  data[0..len-1] is copied into
// the arena.  The variable's type must have ty->size == len; use
// MakeArray(char_ty, len) to ensure the sizes match.
void __builtin_ast_global_var_set_init_data(Obj *var, const char *data,
                                            int len) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !var || !data || len <= 0)
        return;
    char *buf = arena_alloc(&vm->compiler.parser_arena, len);
    memcpy(buf, data, len);
    var->init_data = buf;
}

// Set the static flag on a generated global (true = internal linkage).
void __builtin_ast_global_var_set_static(Obj *var, bool is_static) {
    if (var)
        var->is_static = is_static;
}

// ============================================================================
// Function-building context (ticket #148): WithFn support
// ============================================================================

// Small internal save-stack so macros can push/pop current_fn cleanly.
// Macro execution is single-threaded so a module-level stack is fine.
#define CCCC_FN_CONTEXT_STACK_DEPTH 16
static Obj *_fn_context_stack[CCCC_FN_CONTEXT_STACK_DEPTH];
static Obj
    *_fn_locals_stack[CCCC_FN_CONTEXT_STACK_DEPTH]; // saved vm->compiler.locals
static int _fn_context_depth = 0;

// Push a new function context: saves current_fn and vm->compiler.locals, then
// switches both to fn.  Any vars allocated inside the WithFn block (e.g. from
// CompoundLiteral) go into fn->locals via vm->compiler.locals; they are flushed
// back to fn->locals on pop so assign_stack_offsets sees them correctly.
void __builtin_ast_push_fn(Obj *fn) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm)
        return;
    if (_fn_context_depth >= CCCC_FN_CONTEXT_STACK_DEPTH) {
        error("__builtin_ast_push_fn: function context stack overflow (max %d)",
              CCCC_FN_CONTEXT_STACK_DEPTH);
        return;
    }
    int d                   = _fn_context_depth++;
    _fn_context_stack[d]    = vm->compiler.current_fn;
    _fn_locals_stack[d]     = vm->compiler.locals; // save outer locals pointer
    vm->compiler.current_fn = fn;
    vm->compiler.locals     = fn->locals; // switch to inner fn's locals
}

// Pop the most recently pushed function context.  Flushes any vars that were
// added to vm->compiler.locals during the WithFn block into fn->locals, then
// restores the outer current_fn and locals.
void __builtin_ast_pop_fn(void) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm)
        return;
    if (_fn_context_depth <= 0) {
        // Unmatched pop — reset to NULL rather than crashing.
        vm->compiler.current_fn = NULL;
        return;
    }
    // Flush vars added inside the block back into the inner function.
    if (vm->compiler.current_fn)
        vm->compiler.current_fn->locals = vm->compiler.locals;
    int d                   = --_fn_context_depth;
    vm->compiler.current_fn = _fn_context_stack[d];
    vm->compiler.locals     = _fn_locals_stack[d]; // restore outer locals
}

// ============================================================================
// Ticket #235: FP-style array generators (GenerateSum/map/reduce/filter)
// ============================================================================

// __builtin_generate_sum(elem_ty): publishes
//   T sum_T(T *arr, size_t n) { T total = 0; for (...) total += arr[i]; return
//   total; }
void __builtin_generate_sum(Type *elem_ty) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !elem_ty)
        return;

    char gname[128];
    strcpy(gname, "sum_");
    strcat(gname, __builtin_ast_type_c_name(elem_ty));

    Type *size_ty = __builtin_ast_get_type("size_t");
    Obj  *fn      = __builtin_ast_function(gname, elem_ty);
    __builtin_ast_function_add_param(fn, "arr",
                                     __builtin_ast_make_pointer(elem_ty));
    __builtin_ast_function_add_param(fn, "n", size_ty);

    __builtin_ast_push_fn(fn);

    Node *total = __builtin_ast_local_var("total", elem_ty);
    Node *i     = __builtin_ast_local_var("i", size_ty);

    Node *block = __builtin_ast_block(NULL, 0);
    __builtin_ast_block_add_stmt(
        block, __builtin_ast_expr_stmt(
                   __builtin_ast_assign(total, __builtin_ast_int_literal(0))));

    Node *init = __builtin_ast_expr_stmt(
        __builtin_ast_assign(i, __builtin_ast_int_literal(0)));
    Node *cond =
        __builtin_ast_binary(ND_LT, i, __builtin_ast_param_ref(fn, "n"));
    Node *inc = __builtin_ast_assign(
        i, __builtin_ast_binary(ND_ADD, i, __builtin_ast_int_literal(1)));
    Node *body = __builtin_ast_expr_stmt(__builtin_ast_assign(
        total,
        __builtin_ast_binary(
            ND_ADD, total,
            __builtin_ast_subscript(__builtin_ast_param_ref(fn, "arr"), i))));

    __builtin_ast_block_add_stmt(block,
                                 __builtin_ast_for(init, cond, inc, body));
    __builtin_ast_block_add_stmt(block, __builtin_ast_return(total));

    __builtin_ast_function_set_body(fn, block);
    __builtin_ast_pop_fn();

    __builtin_ast_publish(fn, 0);
}

// __builtin_generate_map(elem_ty): publishes
//   void map_T(T *arr, size_t n, T *out, T (*f)(T)) { for (...) out[i] =
//   f(arr[i]); }
void __builtin_generate_map(Type *elem_ty) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !elem_ty)
        return;

    char gname[128];
    strcpy(gname, "map_");
    strcat(gname, __builtin_ast_type_c_name(elem_ty));

    Type *size_ty = __builtin_ast_get_type("size_t");
    Type *cb_ty =
        __builtin_ast_make_func_ptr_type(elem_ty, (Type *[]){elem_ty}, 1);

    Obj *fn = __builtin_ast_function(gname, __builtin_ast_get_type("void"));
    __builtin_ast_function_add_param(fn, "arr",
                                     __builtin_ast_make_pointer(elem_ty));
    __builtin_ast_function_add_param(fn, "n", size_ty);
    __builtin_ast_function_add_param(fn, "out",
                                     __builtin_ast_make_pointer(elem_ty));
    __builtin_ast_function_add_param(fn, "f", cb_ty);

    __builtin_ast_push_fn(fn);

    Node *i     = __builtin_ast_local_var("i", size_ty);

    Node *block = __builtin_ast_block(NULL, 0);
    Node *init  = __builtin_ast_expr_stmt(
        __builtin_ast_assign(i, __builtin_ast_int_literal(0)));
    Node *cond =
        __builtin_ast_binary(ND_LT, i, __builtin_ast_param_ref(fn, "n"));
    Node *inc = __builtin_ast_assign(
        i, __builtin_ast_binary(ND_ADD, i, __builtin_ast_int_literal(1)));

    Node *call =
        __builtin_ast_funcall(__builtin_ast_param_ref(fn, "f"),
                              (Node *[]){__builtin_ast_subscript(
                                  __builtin_ast_param_ref(fn, "arr"), i)},
                              1);
    Node *body = __builtin_ast_expr_stmt(__builtin_ast_assign(
        __builtin_ast_subscript(__builtin_ast_param_ref(fn, "out"), i), call));

    __builtin_ast_block_add_stmt(block,
                                 __builtin_ast_for(init, cond, inc, body));

    __builtin_ast_function_set_body(fn, block);
    __builtin_ast_pop_fn();

    __builtin_ast_publish(fn, 0);
}

// __builtin_generate_reduce(elem_ty): publishes
//   T reduce_T(T *arr, size_t n, T init, T (*f)(T, T)) {
//       T acc = init; for (...) acc = f(acc, arr[i]); return acc;
//   }
void __builtin_generate_reduce(Type *elem_ty) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !elem_ty)
        return;

    char gname[128];
    strcpy(gname, "reduce_");
    strcat(gname, __builtin_ast_type_c_name(elem_ty));

    Type *size_ty = __builtin_ast_get_type("size_t");
    Type *cb_ty   = __builtin_ast_make_func_ptr_type(
        elem_ty, (Type *[]){elem_ty, elem_ty}, 2);

    Obj *fn = __builtin_ast_function(gname, elem_ty);
    __builtin_ast_function_add_param(fn, "arr",
                                     __builtin_ast_make_pointer(elem_ty));
    __builtin_ast_function_add_param(fn, "n", size_ty);
    __builtin_ast_function_add_param(fn, "init", elem_ty);
    __builtin_ast_function_add_param(fn, "f", cb_ty);

    __builtin_ast_push_fn(fn);

    Node *acc   = __builtin_ast_local_var("acc", elem_ty);
    Node *i     = __builtin_ast_local_var("i", size_ty);

    Node *block = __builtin_ast_block(NULL, 0);
    __builtin_ast_block_add_stmt(
        block, __builtin_ast_expr_stmt(__builtin_ast_assign(
                   acc, __builtin_ast_param_ref(fn, "init"))));

    Node *init_stmt = __builtin_ast_expr_stmt(
        __builtin_ast_assign(i, __builtin_ast_int_literal(0)));
    Node *cond =
        __builtin_ast_binary(ND_LT, i, __builtin_ast_param_ref(fn, "n"));
    Node *inc = __builtin_ast_assign(
        i, __builtin_ast_binary(ND_ADD, i, __builtin_ast_int_literal(1)));

    Node *call = __builtin_ast_funcall(
        __builtin_ast_param_ref(fn, "f"),
        (Node *[]){acc, __builtin_ast_subscript(
                            __builtin_ast_param_ref(fn, "arr"), i)},
        2);
    Node *body = __builtin_ast_expr_stmt(__builtin_ast_assign(acc, call));

    __builtin_ast_block_add_stmt(block,
                                 __builtin_ast_for(init_stmt, cond, inc, body));
    __builtin_ast_block_add_stmt(block, __builtin_ast_return(acc));

    __builtin_ast_function_set_body(fn, block);
    __builtin_ast_pop_fn();

    __builtin_ast_publish(fn, 0);
}

// __builtin_generate_filter(elem_ty): publishes
//   void filter_T(T *arr, size_t n, T *out, size_t *out_n, bool (*pred)(T)) {
//       size_t count = 0;
//       for (...) if (pred(arr[i])) out[count] = arr[i], count += 1;
//       *out_n = count;
//   }
void __builtin_generate_filter(Type *elem_ty) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !elem_ty)
        return;

    char gname[128];
    strcpy(gname, "filter_");
    strcat(gname, __builtin_ast_type_c_name(elem_ty));

    Type *size_ty = __builtin_ast_get_type("size_t");
    Type *cb_ty   = __builtin_ast_make_func_ptr_type(
        __builtin_ast_get_type("_Bool"), (Type *[]){elem_ty}, 1);

    Obj *fn = __builtin_ast_function(gname, __builtin_ast_get_type("void"));
    __builtin_ast_function_add_param(fn, "arr",
                                     __builtin_ast_make_pointer(elem_ty));
    __builtin_ast_function_add_param(fn, "n", size_ty);
    __builtin_ast_function_add_param(fn, "out",
                                     __builtin_ast_make_pointer(elem_ty));
    __builtin_ast_function_add_param(fn, "out_n",
                                     __builtin_ast_make_pointer(size_ty));
    __builtin_ast_function_add_param(fn, "pred", cb_ty);

    __builtin_ast_push_fn(fn);

    Node *i     = __builtin_ast_local_var("i", size_ty);
    Node *count = __builtin_ast_local_var("count", size_ty);

    Node *block = __builtin_ast_block(NULL, 0);
    __builtin_ast_block_add_stmt(
        block, __builtin_ast_expr_stmt(
                   __builtin_ast_assign(count, __builtin_ast_int_literal(0))));

    Node *init = __builtin_ast_expr_stmt(
        __builtin_ast_assign(i, __builtin_ast_int_literal(0)));
    Node *cond =
        __builtin_ast_binary(ND_LT, i, __builtin_ast_param_ref(fn, "n"));
    Node *inc = __builtin_ast_assign(
        i, __builtin_ast_binary(ND_ADD, i, __builtin_ast_int_literal(1)));

    Node *elem = __builtin_ast_subscript(__builtin_ast_param_ref(fn, "arr"), i);
    Node *pred_call = __builtin_ast_funcall(__builtin_ast_param_ref(fn, "pred"),
                                            (Node *[]){elem}, 1);

    Node *then_block = __builtin_ast_block(NULL, 0);
    __builtin_ast_block_add_stmt(
        then_block,
        __builtin_ast_expr_stmt(__builtin_ast_assign(
            __builtin_ast_subscript(__builtin_ast_param_ref(fn, "out"), count),
            elem)));
    __builtin_ast_block_add_stmt(
        then_block,
        __builtin_ast_expr_stmt(__builtin_ast_assign(
            count, __builtin_ast_binary(ND_ADD, count,
                                        __builtin_ast_int_literal(1)))));

    Node *body = __builtin_ast_if(pred_call, then_block, NULL);

    __builtin_ast_block_add_stmt(block,
                                 __builtin_ast_for(init, cond, inc, body));
    __builtin_ast_block_add_stmt(
        block,
        __builtin_ast_expr_stmt(__builtin_ast_assign(
            __builtin_ast_unary(ND_DEREF, __builtin_ast_param_ref(fn, "out_n")),
            count)));

    __builtin_ast_function_set_body(fn, block);
    __builtin_ast_pop_fn();

    __builtin_ast_publish(fn, 0);
}

// ============================================================================
// Scoped AST builder contexts (ticket #232)
// ============================================================================

#define CCCC_AST_CONTEXT_STACK_DEPTH 16

static Node *_block_context_stack[CCCC_AST_CONTEXT_STACK_DEPTH];
static int   _block_context_depth = 0;
static Type *_struct_context_stack[CCCC_AST_CONTEXT_STACK_DEPTH];
static int   _struct_context_depth = 0;
static Node *_switch_context_stack[CCCC_AST_CONTEXT_STACK_DEPTH];
static int   _switch_context_depth = 0;
static Type *_enum_context_stack[CCCC_AST_CONTEXT_STACK_DEPTH];
static int   _enum_context_depth = 0;

Type *__builtin_ast_struct_add_field(Type *ty, const char *name,
                                     Type *field_type);
void __builtin_ast_enum_add_constant(Type *ty, const char *name, int64_t value);

void __builtin_ast_push_block(Node *block) {
    VirtualMachine *vm = __builtin_current_vm;
    (void)vm;
    if (_block_context_depth >= CCCC_AST_CONTEXT_STACK_DEPTH) {
        error("__builtin_ast_push_block: block context stack overflow (max %d)",
              CCCC_AST_CONTEXT_STACK_DEPTH);
        return;
    }
    _block_context_stack[_block_context_depth++] = block;
}

void __builtin_ast_pop_block(void) {
    VirtualMachine *vm = __builtin_current_vm;
    (void)vm;
    if (_block_context_depth > 0)
        _block_context_depth--;
}

void __builtin_ast_push_struct(Type *ty) {
    VirtualMachine *vm = __builtin_current_vm;
    (void)vm;
    if (_struct_context_depth >= CCCC_AST_CONTEXT_STACK_DEPTH) {
        error(
            "__builtin_ast_push_struct: struct context stack overflow (max %d)",
            CCCC_AST_CONTEXT_STACK_DEPTH);
        return;
    }
    _struct_context_stack[_struct_context_depth++] = ty;
}

void __builtin_ast_pop_struct(void) {
    VirtualMachine *vm = __builtin_current_vm;
    (void)vm;
    if (_struct_context_depth > 0)
        _struct_context_depth--;
}

void __builtin_ast_push_switch(Node *switch_node) {
    VirtualMachine *vm = __builtin_current_vm;
    (void)vm;
    if (_switch_context_depth >= CCCC_AST_CONTEXT_STACK_DEPTH) {
        error(
            "__builtin_ast_push_switch: switch context stack overflow (max %d)",
            CCCC_AST_CONTEXT_STACK_DEPTH);
        return;
    }
    _switch_context_stack[_switch_context_depth++] = switch_node;
}

void __builtin_ast_pop_switch(void) {
    VirtualMachine *vm = __builtin_current_vm;
    (void)vm;
    if (_switch_context_depth > 0)
        _switch_context_depth--;
}

void __builtin_ast_push_enum(Type *ty) {
    VirtualMachine *vm = __builtin_current_vm;
    (void)vm;
    if (_enum_context_depth >= CCCC_AST_CONTEXT_STACK_DEPTH) {
        error("__builtin_ast_push_enum: enum context stack overflow (max %d)",
              CCCC_AST_CONTEXT_STACK_DEPTH);
        return;
    }
    _enum_context_stack[_enum_context_depth++] = ty;
}

void __builtin_ast_pop_enum(void) {
    VirtualMachine *vm = __builtin_current_vm;
    (void)vm;
    if (_enum_context_depth > 0)
        _enum_context_depth--;
}

Node *__builtin_ast_block_add_current_stmt(Node *stmt) {
    if (_block_context_depth <= 0)
        return NULL;
    return __builtin_ast_block_add_stmt(
        _block_context_stack[_block_context_depth - 1], stmt);
}

Type *__builtin_ast_struct_add_current_field(const char *name,
                                             Type       *field_type) {
    if (_struct_context_depth <= 0)
        return NULL;
    return __builtin_ast_struct_add_field(
        _struct_context_stack[_struct_context_depth - 1], name, field_type);
}

void __builtin_ast_switch_add_current_case(Node *value, Node *body) {
    if (_switch_context_depth <= 0)
        return;
    __builtin_ast_switch_add_case(
        _switch_context_stack[_switch_context_depth - 1], value, body);
}

void __builtin_ast_switch_set_current_default(Node *body) {
    if (_switch_context_depth <= 0)
        return;
    __builtin_ast_switch_set_default(
        _switch_context_stack[_switch_context_depth - 1], body);
}

void __builtin_ast_enum_add_current_constant(const char *name, int value) {
    if (_enum_context_depth <= 0)
        return;
    __builtin_ast_enum_add_constant(
        _enum_context_stack[_enum_context_depth - 1], name, value);
}

// ============================================================================
// AST Dump Functions (ticket #58)
// ============================================================================

// ---------------------------------------------------------------------------
// dumpTree: reuse the existing cc_dump_node text renderer
// ---------------------------------------------------------------------------

void __builtin_dump_tree(Node *node) {
    VirtualMachine *vm = __builtin_current_vm;
    (void)vm;
    if (!node)
        return;
    cc_dump_node(stdout, node, /*verbose=*/0);
    fflush(stdout);
}

const char *__builtin_dump_tree_to_string(Node *node) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !node)
        return NULL;

    char  *buf  = NULL;
    size_t size = 0;
    FILE  *f    = open_memstream(&buf, &size);
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
// dumpAstGen: emit __builtin_ast_*() builder calls that reconstruct the node
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
    for (Node *p = node; p; p = p->next)
        n++;
    fprintf(f, "(Node*[]){");
    for (Node *p = node; p; p = p->next) {
        emit_ast_gen(f, p);
        if (p->next)
            fprintf(f, ", ");
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
            if (node->ty &&
                (node->ty->kind == TY_FLOAT || node->ty->kind == TY_DOUBLE ||
                 node->ty->kind == TY_LDOUBLE))
                fprintf(f, "__builtin_ast_float_literal(VM, %Lg)", node->fval);
            else
                fprintf(f, "__builtin_ast_int_literal(VM, %lld)",
                        (long long)node->val);
            break;
        case ND_VAR:
            if (node->var && node->var->name)
                fprintf(f, "__builtin_ast_var_ref(VM, \"%s\")",
                        node->var->name);
            else
                fprintf(f, "/* VAR(?) */");
            break;
        case ND_ASSIGN:
            fprintf(f, "__builtin_ast_assign(VM, ");
            emit_ast_gen(f, node->lhs);
            fprintf(f, ", ");
            emit_ast_gen(f, node->rhs);
            fprintf(f, ")");
            break;
        case ND_MEMBER:
            fprintf(f, "__builtin_ast_member(VM, ");
            emit_ast_gen(f, node->lhs);
            // Extract member name from the Token stored in member->name
            if (node->member && node->member->name)
                fprintf(f, ", \"%.*s\")", node->member->name->len,
                        node->member->name->loc);
            else
                fprintf(f, ", \"?\")");
            break;
        case ND_FUNCALL:
            fprintf(f, "__builtin_ast_funcall(VM, ");
            emit_ast_gen(f, node->lhs);
            fprintf(f, ", ");
            emit_ast_gen_list(f, node->args);
            fprintf(f, ")");
            break;
        case ND_FOR:
            if (!node->init && !node->inc) {
                // Looks like a while loop
                fprintf(f, "__builtin_ast_while(VM, ");
                emit_ast_gen(f, node->cond);
                fprintf(f, ", ");
                emit_ast_gen(f, node->then);
                fprintf(f, ")");
            } else {
                fprintf(f, "__builtin_ast_for(VM, ");
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
            fprintf(f, "__builtin_ast_do_while(VM, ");
            emit_ast_gen(f, node->then);
            fprintf(f, ", ");
            emit_ast_gen(f, node->cond);
            fprintf(f, ")");
            break;
        case ND_RETURN:
            fprintf(f, "__builtin_ast_return(VM, ");
            emit_ast_gen(f, node->lhs);
            fprintf(f, ")");
            break;
        case ND_IF:
            fprintf(f, "__builtin_ast_if(VM, ");
            emit_ast_gen(f, node->cond);
            fprintf(f, ", ");
            emit_ast_gen(f, node->then);
            fprintf(f, ", ");
            emit_ast_gen(f, node->els);
            fprintf(f, ")");
            break;
        case ND_BLOCK:
            fprintf(f, "__builtin_ast_block(VM, (Node*[]){");
            {
                int i = 0;
                for (Node *s = node->body; s; s = s->next) {
                    if (i++)
                        fprintf(f, ", ");
                    emit_ast_gen(f, s);
                }
            }
            fprintf(f, "}, %d)", ({
                        int n = 0;
                        for (Node *s = node->body; s; s = s->next)
                            n++;
                        n;
                    }));
            break;
        case ND_EXPR_STMT:
            fprintf(f, "__builtin_ast_expr_stmt(VM, ");
            emit_ast_gen(f, node->lhs);
            fprintf(f, ")");
            break;
        case ND_CAST:
            // The target type cannot be fully reconstructed from the AST alone
            // — emit a placeholder comment for the type argument.
            fprintf(f, "__builtin_ast_cast(VM, ");
            emit_ast_gen(f, node->lhs);
            fprintf(f, ", /* type */ NULL)");
            break;
        case ND_QUOTE_LAZY:
            // #1242: should always have been materialised by
            // cc_quote_expand_lazy() before reaching here; explicit case so
            // it doesn't fall into the binary/unary default below and emit
            // nonsense from lhs/rhs (which this kind doesn't use).
            fprintf(f, "/* QUOTE_LAZY(?) -- unspliced QuoteLazy fragment */");
            break;
        default:
            // Binary / unary operators: emit via __builtin_ast_binary /
            // __builtin_ast_unary
            if (node->lhs && node->rhs) {
                fprintf(f, "__builtin_ast_binary(VM, _%s, ",
                        cc_node_kind_name(node->kind));
                emit_ast_gen(f, node->lhs);
                fprintf(f, ", ");
                emit_ast_gen(f, node->rhs);
                fprintf(f, ")");
            } else if (node->lhs) {
                fprintf(f, "__builtin_ast_unary(VM, _%s, ",
                        cc_node_kind_name(node->kind));
                emit_ast_gen(f, node->lhs);
                fprintf(f, ")");
            } else {
                fprintf(f, "/* %s */", cc_node_kind_name(node->kind));
            }
            break;
    }
}

void __builtin_dump_ast_gen(Node *node) {
    VirtualMachine *vm = __builtin_current_vm;
    (void)vm;
    if (!node)
        return;
    emit_ast_gen(stdout, node);
    fprintf(stdout, "\n");
    fflush(stdout);
}

const char *__builtin_dump_ast_gen_to_string(Node *node) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !node)
        return NULL;

    char  *buf  = NULL;
    size_t size = 0;
    FILE  *f    = open_memstream(&buf, &size);
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
// Quasi-quoting: __builtin_quote / __builtin_quote_n (ticket #1)
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
    int  numlen = tok->len - 1;
    if (numlen > 20)
        return 0; // absurdly large index: not a splice point
    memcpy(buf, tok->loc + 1, numlen);
    buf[numlen] = '\0';
    int n       = atoi(buf);
    return n > 0 ? n : 0;
}

// Scan the token stream:
//   - detects mixing of $N/$@N positional and $$/$@ incremental (error on mix)
//   - rewrites the k-th $$ or $@ to a canonical $k or $@k token
//     (collapsing the multi-token $@N / $@ sequences in-place)
//   - sets *splice_mask (bit k-1) for each $@k splice index seen
//   - returns the maximum index referenced (0 if no splice points)
//   - returns -1 on mixing error
static int quote_scan_and_rewrite(VirtualMachine *vm, Token *toks,
                                  uint64_t *splice_mask) {
    bool has_positional  = false;
    bool has_incremental = false;
    int  max_index       = 0;
    int  incr_counter    = 0;
    if (splice_mask)
        *splice_mask = 0;

    for (Token *t = toks; t && t->kind != TK_EOF; t = t->next) {
        // Check for $@ splice syntax: a lone '$' ident followed by '@' punct.
        // $@N  → positional splice (index N)
        // $@   → incremental splice (next sequential index)
        if (t->kind == TK_IDENT && t->len == 1 && t->loc[0] == '$' && t->next &&
            t->next->kind == TK_PUNCT && t->next->len == 1 &&
            t->next->loc[0] == '@') {
            Token *at_tok  = t->next;
            Token *num_tok = at_tok->next;
            int    k;

            if (num_tok && num_tok->kind == TK_NUM) {
                // $@N positional splice
                has_positional = true;
                if (has_incremental) {
                    error_tok(vm, t,
                              "__builtin_quote: cannot mix positional ($@N) "
                              "and incremental "
                              "($@ / $$) splice syntax in one template");
                    return -1;
                }
                // Parse the index from the number token's text
                int  numlen = (num_tok->len < 20) ? num_tok->len : 20;
                char buf[32];
                memcpy(buf, num_tok->loc, numlen);
                buf[numlen] = '\0';
                k           = atoi(buf);
                if (k <= 0) {
                    error_tok(
                        vm, t,
                        "__builtin_quote: $@0 is not a valid splice index "
                        "(splice indices start at 1)");
                    return -1;
                }
                // Collapse three tokens ($ @ N) into one token.
                // Keep t->loc pointing into the template file buffer so that
                // error diagnostics can compute the correct column offset.
                // Since "$@N" is literally in the template text, spanning
                // t->loc through the end of num_tok covers the exact text.
                t->len  = (int)(num_tok->loc + num_tok->len - t->loc);
                t->next = num_tok->next; // skip @ and N tokens
            } else {
                // $@ incremental splice
                has_incremental = true;
                if (has_positional) {
                    error_tok(vm, t,
                              "__builtin_quote: cannot mix positional ($@N) "
                              "and incremental "
                              "($@ / $$) splice syntax in one template");
                    return -1;
                }
                incr_counter++;
                k = incr_counter;
                // Collapse two tokens ($ @) into one named $@k
                char *newname = arena_format(vm, "$@%d", k);
                t->loc        = newname;
                t->len        = (int)strlen(newname);
                t->next       = at_tok->next; // skip @ token
            }

            if (k > max_index)
                max_index = k;
            if (splice_mask && k >= 1 && k <= 64)
                *splice_mask |= (uint64_t)1 << (k - 1);
            continue;
        }

        // Regular $N / $$ scalar splice points
        int k = quote_splice_kind(t);
        if (k == 0)
            continue;

        if (k == -1) {
            // $$ incremental
            has_incremental = true;
            if (has_positional) {
                error_tok(vm, t,
                          "__builtin_quote: cannot mix $N positional and $$ "
                          "incremental "
                          "splice syntax in one template");
                return -1;
            }
            incr_counter++;
            // Rewrite this token's loc/len to "$<incr_counter>"
            char *newname = arena_format(vm, "$%d", incr_counter);
            t->loc        = newname;
            t->len        = (int)strlen(newname);
            if (incr_counter > max_index)
                max_index = incr_counter;
        } else {
            // $N positional
            has_positional = true;
            if (has_incremental) {
                error_tok(vm, t,
                          "__builtin_quote: cannot mix $N positional and $$ "
                          "incremental "
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
static Obj *quote_push_placeholder(VirtualMachine *vm, Scope *sc, char *name,
                                   Node *arg_node) {
    int name_len = (int)strlen(name);

    // Derive type from the argument node if available. #1242: a QuoteLazy()
    // fragment is not yet parsed, so it has no meaningful type and must not
    // be run through add_type -- add_type hard-errors on ND_QUOTE_LAZY
    // (see src/type.c), since the only legitimate way that kind is ever
    // consumed is via cc_quote_expand_lazy(), never through ordinary type
    // inference. The placeholder Obj stays ty_long (never substituted as a
    // value -- it is replaced wholesale by the materialised fragment before
    // add_type ever sees the surrounding tree).
    Type *ty      = ty_long; // safe fallback
    bool  is_lazy = arg_node && arg_node->kind == ND_QUOTE_LAZY;
    if (arg_node && !is_lazy) {
        add_type(vm, arg_node);
        if (arg_node->ty)
            ty = arg_node->ty;
    }

    Obj *var = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
    memset(var, 0, sizeof(Obj));
    var->name  = name;
    var->ty    = ty;
    var->align = ty->align;
    if (is_lazy)
        var->lazy_quote = arg_node;
    // #894: a $k/$@k quote placeholder behaves like a local pseudo-variable
    // (scoped to this one quote_core call, never a real global with
    // data-segment storage) -- mark it is_local so the #887 guard in
    // primary() (src/parse.c), which now also applies while Quote() forces
    // in_macro_mode true for its own reentrant parse, doesn't mistake it
    // for a non-local global that needs to be part of vm->compiler.globals.
    var->is_local = true;

    VarScopeNode *snode =
        arena_alloc(&vm->compiler.parser_arena, sizeof(VarScopeNode));
    memset(snode, 0, sizeof(VarScopeNode));
    snode->var      = var;
    snode->name     = name;
    snode->name_len = name_len;
    snode->next     = sc->vars;
    sc->vars        = snode;

    // Also register in the hashmap so find_var can locate the placeholder
    // even after push_scope initializes sc->var_map (which happens when the
    // compound literal creates its anonymous variable via new_var).
    hashmap_put2_borrowed(&sc->var_map, name, name_len, snode);

    return var;
}

// Substitution walk state
typedef struct {
    VirtualMachine
          *vm; // compiler context (needed for ND_INIT_SPLICE expansion)
    Obj   *placeholder_vars[64]; // placeholder_vars[i] = Obj for $(i+1)
    Obj   *splice_vars[64];      // splice_vars[i]      = Obj for $@(i+1)
    Node **arg_nodes;
    int    n_args;
} QuoteSubstState;

// If stmt is an ND_EXPR_STMT whose sole expression is a reference to a splice
// placeholder $@k, return the caller's node chain for index k.  Otherwise NULL.
static Node *splice_chain_for(QuoteSubstState *s, Node *stmt);

// Like splice_chain_for but for a bare expression-position arg (ND_VAR
// directly, not wrapped in ND_EXPR_STMT).  Returns true and sets *out_chain if
// arg is a
// $@k placeholder; false otherwise.  *out_chain may be NULL for an empty
// splice.
static bool splice_chain_for_arg(QuoteSubstState *s, Node *arg,
                                 Node **out_chain) {
    if (!arg || arg->kind != ND_VAR || !arg->var)
        return false;
    for (int i = 0; i < s->n_args && i < 64; i++) {
        if (s->splice_vars[i] && arg->var == s->splice_vars[i]) {
            *out_chain = s->arg_nodes[i];
            return true;
        }
    }
    return false;
}

static Node *splice_chain_for(QuoteSubstState *s, Node *stmt) {
    if (!stmt || stmt->kind != ND_EXPR_STMT)
        return NULL;
    Node *inner = stmt->lhs;
    if (!inner || inner->kind != ND_VAR || !inner->var)
        return NULL;
    for (int i = 0; i < s->n_args && i < 64; i++) {
        if (s->splice_vars[i] && inner->var == s->splice_vars[i])
            return s->arg_nodes[i]; // chain head (may be NULL = empty splice)
    }
    return NULL;                    // not a splice placeholder
}

// Walk the parsed tree and replace ND_VAR placeholder nodes with arg nodes.
// Mirrors the transform_node() field traversal in pragma.c.
// Splice placeholders ($@k) are expanded in statement-list positions (body).
// Using $@k outside a statement-list position is a compile-time error.
static Node *quote_substitute(QuoteSubstState *s, Node *node) {
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
        // (Direct arg-list and initializer positions are handled elsewhere.)
        for (int i = 0; i < s->n_args && i < 64; i++) {
            if (s->splice_vars[i] && node->var == s->splice_vars[i]) {
                error_tok(
                    s->vm, node->tok,
                    "__builtin_quote: $@%d is only valid in statement-list "
                    "position (inside a block { }), as a direct call "
                    "argument, or as the sole element of a compound-literal "
                    "initializer; cannot be used as a sub-expression",
                    i + 1);
                return node; // return unchanged to avoid a NULL crash
            }
        }
    }

    // Initializer-list splice: expand deferred compound-literal $@k splice
    if (node->kind == ND_INIT_SPLICE) {
        if (!node->lhs || node->lhs->kind != ND_VAR || !node->lhs->var)
            error("ND_INIT_SPLICE: missing splice var (internal error)");
        for (int i = 0; i < s->n_args && i < 64; i++) {
            if (s->splice_vars[i] && node->lhs->var == s->splice_vars[i]) {
                Node *chain = s->arg_nodes[i];
                return node_expand_init_splice(s->vm, node, chain);
            }
        }
        error("ND_INIT_SPLICE: unresolved splice var (internal error)");
        return node;
    }

    // Recurse into all child fields (same set as transform_node in pragma.c)
    node->lhs  = quote_substitute(s, node->lhs);
    node->rhs  = quote_substitute(s, node->rhs);
    node->cond = quote_substitute(s, node->cond);
    node->then = quote_substitute(s, node->then);
    node->els  = quote_substitute(s, node->els);
    node->init = quote_substitute(s, node->init);
    node->inc  = quote_substitute(s, node->inc);
    // #1018: va_ap/va_last/va_src are the parsed __builtin_va_*() annotation
    // trees (Node.va_form, src/cccc.h) -- independently-parsed subtrees, not
    // aliases into node's own lhs/rhs/etc, so they need the same
    // placeholder substitution as any other child field or a quoted
    // variadic function body would keep a stale $k placeholder in its
    // serializer annotation after substitution.
    node->va_ap   = quote_substitute(s, node->va_ap);
    node->va_last = quote_substitute(s, node->va_last);
    node->va_src  = quote_substitute(s, node->va_src);

    // body is a statement chain linked via ->next.
    // Splice placeholders in body position expand to N statements.
    if (node->body) {
        Node  head_val = {};
        Node *cur      = &head_val;
        for (Node *st = node->body; st;) {
            Node *next_st = st->next;
            st->next      = NULL; // isolate before recursing

            Node *chain   = splice_chain_for(s, st);
            if (chain) {
                // Append the entire caller-provided chain
                Node *tail = chain;
                while (tail->next)
                    tail = tail->next;
                cur->next = chain;
                cur       = tail;
            } else {
                Node *sub = quote_substitute(s, st);
                if (sub) {
                    cur->next = sub;
                    cur       = sub;
                }
            }
            st = next_st;
        }
        cur->next  = NULL;
        node->body = head_val.next;
    }

    // args is an argument chain linked via ->next.
    // Splice placeholders ($@k) in direct arg position expand to N expressions.
    if (node->args) {
        bool has_splice = false;
        for (Node *a = node->args; a; a = a->next) {
            Node *dummy;
            if (splice_chain_for_arg(s, a, &dummy)) {
                has_splice = true;
                break;
            }
        }

        if (has_splice) {
            Node  head_val = {};
            Node *cur      = &head_val;
            for (Node *a = node->args; a;) {
                Node *next_a = a->next;
                a->next      = NULL;

                Node *chain;
                if (splice_chain_for_arg(s, a, &chain)) {
                    if (chain) {
                        Node *tail = chain;
                        while (tail->next)
                            tail = tail->next;
                        cur->next = chain;
                        cur       = tail;
                    }
                    // Empty splice: arg disappears (chain == NULL → no-op)
                } else {
                    Node *sub = quote_substitute(s, a);
                    if (sub) {
                        cur->next = sub;
                        cur       = sub;
                    }
                }
                cur->next = NULL;
                a         = next_a;
            }
            node->args = head_val.next;
        } else {
            // No splice: existing scalar substitution (unchanged)
            node->args = quote_substitute(s, node->args);
            for (Node *a = node->args; a && a->next; a = a->next)
                a->next = quote_substitute(s, a->next);
        }
    }

    // switch case chains
    for (Node *c = node->case_next; c; c = c->case_next)
        c->lhs = quote_substitute(s, c->lhs);
    if (node->default_case)
        node->default_case->lhs = quote_substitute(s, node->default_case->lhs);

    return node;
}

static void quote_rebind_macro_scope(Node *node, Scope *old_scope,
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

    for (Node *st = node->body; st; st = st->next)
        quote_rebind_macro_scope(st, old_scope, new_scope);

    for (Node *a = node->args; a; a = a->next)
        quote_rebind_macro_scope(a, old_scope, new_scope);

    for (Node *c = node->case_next; c; c = c->case_next)
        quote_rebind_macro_scope(c->lhs, old_scope, new_scope);
    if (node->default_case)
        quote_rebind_macro_scope(node->default_case->lhs, old_scope, new_scope);
}

// Determine lexically whether an unbraced template holds more than one
// top-level statement: a ';' at bracket/paren/brace depth 0 that is not the
// last non-EOF token. Depth tracking keeps a `for (a; b; c)` header (and an
// already-braced template, which never sees depth return to 0 mid-stream)
// from being misdetected as multi-statement.
static bool quote_is_multi_stmt(Token *tok) {
    int depth = 0;
    for (Token *t = tok; t && t->kind != TK_EOF; t = t->next) {
        if (t->kind != TK_PUNCT)
            continue;
        if (equal(t, "(") || equal(t, "[") || equal(t, "{"))
            depth++;
        else if (equal(t, ")") || equal(t, "]") || equal(t, "}"))
            depth--;
        else if (depth == 0 && t->len == 1 && t->loc[0] == ';') {
            if (t->next && t->next->kind != TK_EOF)
                return true;
        }
    }
    return false;
}

// Determine lexically whether the token stream should be parsed as a statement.
// Uses the first token kind/text and, as a fallback, whether the last
// non-EOF token is ';' (expression-statement form).
static bool quote_is_stmt(Token *tok) {
    if (!tok || tok->kind == TK_EOF)
        return false;

    // Compound statement starting with '{'
    if (tok->kind == TK_PUNCT && equal(tok, "{"))
        return true;

    // Statement-initiating keywords
    if (tok->kind == TK_KEYWORD) {
        static const char *stmt_kws[] = {
            "return", "if",       "while", "for",  "do",      "switch",
            "break",  "continue", "goto",  "case", "default", NULL};
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

// After quote_substitute, walk the AST and re-apply parameter casts + arity
// validation for any ND_FUNCALL nodes that had $@k splice placeholders (which
// bypassed parse-time checking in funcall()).
static void recheck_spliced_funcalls(VirtualMachine *vm, Node *node) {
    if (!node)
        return;
    recheck_spliced_funcalls(vm, node->lhs);
    recheck_spliced_funcalls(vm, node->rhs);
    recheck_spliced_funcalls(vm, node->cond);
    recheck_spliced_funcalls(vm, node->then);
    recheck_spliced_funcalls(vm, node->els);
    recheck_spliced_funcalls(vm, node->init);
    recheck_spliced_funcalls(vm, node->inc);
    for (Node *n = node->body; n; n = n->next)
        recheck_spliced_funcalls(vm, n);
    for (Node *a = node->args; a; a = a->next)
        recheck_spliced_funcalls(vm, a);

    if (node->kind != ND_FUNCALL || !node->has_splice_arg)
        return;
    node->has_splice_arg = false;

    Type  *param_ty      = node->func_ty->params;
    Node **ap            = &node->args;
    while (*ap) {
        if (!param_ty) {
            if (!node->func_ty->is_variadic)
                error_tok(vm, node->tok,
                          "too many arguments (after splice expansion)");
            break;
        }
        Node *a = *ap;
        if (param_ty->kind != TY_STRUCT && param_ty->kind != TY_UNION) {
            warn_implicit_conversion(vm, a, param_ty, node->tok);
            Node *cast_node = new_cast(vm, a, param_ty);
            cast_node->next = a->next;
            *ap             = cast_node;
        }
        param_ty = param_ty->next;
        ap       = &(*ap)->next;
    }
    if (param_ty)
        error_tok(vm, node->tok, "too few arguments (after splice expansion)");
}

// Shared implementation for both public entry points.
static Node *quote_core(VirtualMachine *vm, const char *tmpl, Node **nodes,
                        int n) {
    if (!vm || !tmpl)
        return NULL;

    // 1. Tokenize the template string
    Token *toks = tokenize_string(vm, (char *)"<quote>", (char *)tmpl);
    if (!toks)
        return NULL;
    convert_pp_tokens(vm, toks);

    // 1b. #955: a template with more than one top-level statement used to
    // silently drop everything after the first one unless the caller wrapped
    // it in braces (Quote() parsed exactly one statement, via cc_parse_stmt,
    // and never inspected the leftover tokens). Detect that shape and
    // re-tokenize the brace-wrapped source so Quote("a; b;") means exactly
    // what Quote("{ a; b; }") already means -- one ND_BLOCK holding every
    // statement. Re-tokenizing (rather than splicing synthetic '{'/'}'
    // tokens onto the existing chain) keeps $N rewriting and the parse below
    // each running exactly once, over the final chain only.
    if (quote_is_multi_stmt(toks)) {
        char *braced = arena_format(vm, "{ %s }", tmpl);
        toks         = tokenize_string(vm, (char *)"<quote>", braced);
        if (!toks)
            return NULL;
        convert_pp_tokens(vm, toks);
    }

    // 2. Scan, validate mixing, rewrite $$ / $@ / $@N
    uint64_t splice_mask = 0;
    int      max_index   = quote_scan_and_rewrite(vm, toks, &splice_mask);
    if (max_index < 0)
        return NULL; // mixing error already reported

    // 3. Validate count (the array form enforces this; variadic derives n)
    if (max_index > n) {
        error_tok(vm, toks,
                  "__builtin_quote: template references $%d but only %d "
                  "argument%s supplied",
                  max_index, n, n == 1 ? "" : "s");
        return NULL;
    }

    // 4. Build placeholder scope on the stack
    Scope quote_scope;
    memset(&quote_scope, 0, sizeof(Scope));
    quote_scope.next   = vm->compiler.scope;
    vm->compiler.scope = &quote_scope;

    QuoteSubstState subst;
    memset(&subst, 0, sizeof(subst));
    subst.vm        = vm;
    subst.arg_nodes = nodes;
    subst.n_args    = (n < 64) ? n : 64;

    // Register scalar placeholders $k for all referenced indices
    for (int k = 1; k <= max_index; k++) {
        Node *arg  = (k - 1 < n) ? nodes[k - 1] : NULL;
        char *name = arena_format(vm, "$%d", k);
        Obj  *var  = quote_push_placeholder(vm, &quote_scope, name, arg);
        subst.placeholder_vars[k - 1] = var;
    }

    // Register splice placeholders $@k for each $@k / $@ index seen
    for (int k = 1; k <= max_index && k <= 64; k++) {
        if (!(splice_mask & ((uint64_t)1 << (k - 1))))
            continue;
        // #1242: a $@k list splice expects an already-parsed ->next-linked
        // node chain (NodeList()/splice_chain_for); a QuoteLazy() fragment
        // is neither parsed nor a chain, so reject it here, before any
        // parsing happens, rather than failing confusingly deep inside
        // quote_substitute's body-splice walk.
        Node *splice_arg = (k - 1 < n) ? nodes[k - 1] : NULL;
        if (splice_arg && splice_arg->kind == ND_QUOTE_LAZY)
            error_tok(vm, toks,
                      "__builtin_quote: $@%d cannot bind a QuoteLazy "
                      "fragment; list splices require already-parsed "
                      "nodes (build the chain with Quote()/NodeList())",
                      k);
        char *name = arena_format(vm, "$@%d", k);
        // Type doesn't matter for splice placeholders — the whole
        // ND_EXPR_STMT wrapper is discarded during substitution.
        Obj *var = quote_push_placeholder(vm, &quote_scope, name, NULL);
        var->is_splice_placeholder = true;
        subst.splice_vars[k - 1]   = var;
    }

    // 5. Parse (auto-detect expr vs stmt)
    //
    // #894: Quote()/QuoteN() are called from comptime *execution* (a
    // comptime function's body calling Quote(...) as it runs), which for a
    // file-scope-called macro like this one happens after
    // compile_macro_program already reset in_macro_mode to false -- same
    // situation as GetType()/VarRef()/FindGlobal() in this file. Set
    // comptime_splice_active for the duration so a name referenced only
    // inside the quoted template (e.g. an FFI builtin declared but never
    // otherwise mentioned in the comptime program's own source) can still
    // trigger is_typename()/find_tag()/primary()'s demand-driven splice
    // hooks in src/parse.c. Deliberately NOT in_macro_mode itself: that
    // flag also gates primary()'s macro-vs-ordinary-call dispatch, and a
    // Quote()d call to a *sibling comptime function* (e.g.
    // Quote("other_comptime_fn()")) needs that dispatch to keep behaving
    // as if in_macro_mode were still false here, exactly as it already did
    // before this call -- forcing in_macro_mode itself was tried first and
    // broke exactly that case (a comptime-to-comptime call written inside a
    // quoted template).
    bool saved_splice_active            = vm->compiler.comptime_splice_active;
    vm->compiler.comptime_splice_active = true;
    Token *rest                         = NULL;
    Node  *result                       = NULL;
    bool   parsed_as_stmt               = quote_is_stmt(toks);
    if (parsed_as_stmt) {
        result = cc_parse_stmt(vm, &rest, toks);
    } else {
        result = cc_parse_expr(vm, &rest, toks);
    }
    vm->compiler.comptime_splice_active = saved_splice_active;

    // #955: previously any tokens left over after the single parsed
    // statement/expression were silently discarded -- e.g. an unbraced
    // expression-form template like "a = 1; b = 2" (no trailing ';', so
    // quote_is_stmt() took the expression branch) dropped "b = 2" with no
    // diagnostic. The multi-statement case above is now handled by wrapping
    // in braces before parsing; anything still leaving tokens behind here is
    // a genuine malformed template and should be a compile error, not a
    // silent truncation.
    if (rest && rest->kind != TK_EOF)
        error_tok(vm, rest,
                  "__builtin_quote: unparsed tokens remain after the "
                  "template's %s; wrap a multi-statement template in braces",
                  parsed_as_stmt ? "first statement" : "expression");

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

    // 7b. Re-apply parameter casts and validate arity for any ND_FUNCALL nodes
    //     that deferred their checks because a $@k splice placeholder was
    //     present.
    recheck_spliced_funcalls(vm, result);

    // 8. Re-run add_type so spliced-in types propagate correctly
    add_type(vm, result);

    return result;
}

Node *__builtin_quote_n(const char *tmpl, Node **nodes, int count) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !tmpl || (!nodes && count > 0))
        return NULL;
    return quote_core(vm, tmpl, nodes, count);
}

// Scan the raw template string to derive the max splice index referenced,
// without tokenising (avoids double arena allocation in quote_core and lets
// the variadic builtins size their va_arg collection before parsing).
// Handles $N, $$, $@N, and $@ forms. Shared by __builtin_quote and the
// #1242 QuoteLazy builtins.
static int quote_scan_max_index_raw(const char *tmpl) {
    int max_index  = 0;
    int incr_count = 0;

    for (const char *p = tmpl; *p; p++) {
        if (*p != '$')
            continue;
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
    return max_index;
}

Node *__builtin_quote(const char *tmpl, ...) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !tmpl)
        return NULL;

    int max_index = quote_scan_max_index_raw(tmpl);

    // Collect exactly max_index nodes from va_args
    int   n = (max_index < 64) ? max_index : 64;
    Node *arg_buf[64];
    memset(arg_buf, 0, sizeof(arg_buf));

    va_list ap;
    va_start(ap, tmpl);
    for (int i = 0; i < n; i++)
        arg_buf[i] = va_arg(ap, Node *);
    va_end(ap);

    return quote_core(vm, tmpl, arg_buf, n);
}

// ----------------------------------------------------------------------
// #1242: QuoteLazy / QuoteLazyN -- deferred (unparsed) quasi-quotes.
//
// quote_core (above) parses its template eagerly, at the call site, so a
// fragment built by its own Quote() call is validated (break/continue,
// variable scope) against whatever parser context happens to be live right
// then -- not the context it will actually land in once spliced into a
// separately-built outer template. __builtin_quote_lazy/_lazy_n instead
// capture the template text and splice-argument nodes verbatim, without
// tokenising or parsing, and return an ND_QUOTE_LAZY placeholder node. The
// template is only ever tokenized+parsed by cc_quote_expand_lazy() (below),
// which re-enters quote_core at the fragment's actual splice site -- inside
// the enclosing template's own scope chain and (if any) brk_label/
// cont_label -- so `break`/`continue`/free variable references resolve
// exactly as they would if the whole thing had been written as one
// template.
// ----------------------------------------------------------------------

// Shared capture path for __builtin_quote_lazy/__builtin_quote_lazy_n:
// arena-copy the template text and the (already max_index-validated)
// argument array into an ND_QUOTE_LAZY node.
static Node *quote_lazy_capture(VirtualMachine *vm, const char *tmpl,
                                Node **nodes, int n) {
    Node *node = alloc_node(vm, ND_QUOTE_LAZY);
    // Copy now: `tmpl` is a guest string literal owned by the comptime
    // program's own memory, which may be torn down before this fragment is
    // ever spliced (unlike quote_core's eager path, which consumes `tmpl`
    // immediately and never needs to keep it alive past this call).
    node->quote_tmpl     = arena_strdup(vm, (char *)tmpl);
    node->lazy_arg_count = n;
    if (n > 0) {
        node->lazy_args =
            arena_alloc(&vm->compiler.parser_arena, (size_t)n * sizeof(Node *));
        // Deliberately NOT ->next-linked: a $@N argument is itself a
        // ->next-linked chain (NodeList()/splice_chain_for), and any slot
        // may legitimately be NULL (quote_core tolerates nodes[k-1] ==
        // NULL for an index never actually passed). Positional identity is
        // what the eventual quote_core re-parse needs, so this must stay a
        // plain indexed array.
        memcpy(node->lazy_args, nodes, (size_t)n * sizeof(Node *));
    }
    return node;
}

Node *__builtin_quote_lazy_n(const char *tmpl, Node **nodes, int count) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !tmpl || (!nodes && count > 0))
        return NULL;

    // quote_core defers this check until the fragment is actually parsed;
    // QuoteLazyN validates eagerly at the capture site instead, so a wrong
    // count is reported where the caller wrote it rather than deep inside
    // some later, unrelated splice.
    int max_index = quote_scan_max_index_raw(tmpl);
    if (max_index > count) {
        if (vm->compiler.macro_call_tok)
            error_tok(vm, vm->compiler.macro_call_tok,
                      "__builtin_quote_lazy_n: template references $%d but "
                      "only %d argument%s supplied",
                      max_index, count, count == 1 ? "" : "s");
        else
            error("__builtin_quote_lazy_n: template references $%d but "
                  "only %d argument%s supplied",
                  max_index, count, count == 1 ? "" : "s");
        return NULL;
    }
    return quote_lazy_capture(vm, tmpl, nodes, count);
}

Node *__builtin_quote_lazy(const char *tmpl, ...) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !tmpl)
        return NULL;

    int   max_index = quote_scan_max_index_raw(tmpl);
    int   n         = (max_index < 64) ? max_index : 64;
    Node *arg_buf[64];
    memset(arg_buf, 0, sizeof(arg_buf));

    va_list ap;
    va_start(ap, tmpl);
    for (int i = 0; i < n; i++)
        arg_buf[i] = va_arg(ap, Node *);
    va_end(ap);

    return quote_lazy_capture(vm, tmpl, arg_buf, n);
}

// Materialise a QuoteLazy fragment at its splice site: tokenize and parse
// `lazy->quote_tmpl` for the first time, right now, inside the caller's real
// parser context (whatever vm->compiler.scope/brk_label/cont_label/
// current_fn currently are). This is the whole mechanism behind #1242 --
// quote_core itself is already safely reentrant (its placeholder Scope is a
// stack local properly chained/unchained, and comptime_splice_active is
// saved/restored around the inner parse), so no additional state save/
// restore is needed here.
//
// want_stmt selects statement- vs expression-position splicing:
//   - want_stmt && result isn't already a statement kind: wrap in
//     ND_EXPR_STMT (mirrors expr_stmt(), parse_stmt.c) so it can sit
//     directly in a block's ->body chain.
//   - !want_stmt && result is a statement kind: that fragment cannot be
//     used as an expression operand -- error rather than producing a
//     malformed tree.
Node *cc_quote_expand_lazy(VirtualMachine *vm, Node *lazy, bool want_stmt) {
    if (!lazy || lazy->kind != ND_QUOTE_LAZY)
        return lazy;

    Node *result =
        quote_core(vm, lazy->quote_tmpl, lazy->lazy_args, lazy->lazy_arg_count);
    if (!result)
        return result;

    bool is_stmt_kind = result->kind == ND_BLOCK || result->kind == ND_IF ||
                        result->kind == ND_FOR || result->kind == ND_DO ||
                        result->kind == ND_SWITCH ||
                        result->kind == ND_RETURN || result->kind == ND_GOTO ||
                        result->kind == ND_LABEL ||
                        result->kind == ND_EXPR_STMT;

    if (want_stmt) {
        if (is_stmt_kind)
            return result;
        Node *wrap = alloc_node(vm, ND_EXPR_STMT);
        wrap->tok  = lazy->tok;
        wrap->lhs  = result;
        add_type(vm, wrap->lhs);
        return wrap;
    }

    if (is_stmt_kind) {
        if (lazy->tok)
            error_tok(vm, lazy->tok,
                      "QuoteLazy: fragment '%s' is a statement and cannot "
                      "be spliced in expression position",
                      lazy->quote_tmpl);
        else
            error("QuoteLazy: fragment '%s' is a statement and cannot be "
                  "spliced in expression position",
                  lazy->quote_tmpl);
        return result;
    }
    return result;
}

// Build a ->next-linked chain from an array of nodes and return the head.
// Useful for constructing the list argument to a $@k splice.
// A single node is a chain of length 1; passing count==0 returns NULL.
Node *__builtin_node_list(Node **nodes, int count) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !nodes || count <= 0)
        return NULL;

    Node *head = nodes[0];
    Node *cur  = head;
    for (int i = 1; i < count; i++) {
        if (!nodes[i])
            break;
        cur->next = nodes[i];
        cur       = cur->next;
    }
    if (cur)
        cur->next = NULL;
    return head;
}

// ============================================================================
// Type / Declaration Builders (ticket #171)
// ============================================================================

// Helper: synthesize a Token for use as a name field in Type/Member.
// The token's loc is an arena-allocated copy of name so it outlives the call.
static Token *reflect_make_name_token(VirtualMachine *vm, const char *name,
                                      int name_len) {
    Token *tok = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    memset(tok, 0, sizeof(Token));
    tok->kind = TK_IDENT;
    tok->loc  = arena_strdup(vm, name);
    tok->len  = name_len;
    return tok;
}

// Helper: expose a struct/union/enum type by tag name so FindType(name)
// resolves it. Mirrors push_tag_scope + record_type_name in parse.c.
static void reflect_push_tag_scope(VirtualMachine *vm, const char *name,
                                   int name_len, Type *ty) {
    if (!vm || !vm->compiler.scope)
        return;

    // Insert into tag scope linked list + hashmap
    TagScopeNode *node =
        arena_alloc(&vm->compiler.parser_arena, sizeof(TagScopeNode));
    memset(node, 0, sizeof(TagScopeNode));
    node->name               = arena_strdup(vm, name);
    node->name_len           = name_len;
    node->ty                 = ty;
    node->next               = vm->compiler.scope->tags;
    vm->compiler.scope->tags = node;
    hashmap_put2_borrowed(&vm->compiler.scope->tag_map, node->name,
                          node->name_len, node);

    // Record for type_names list (-M support)
    TypeNameRecord *rec =
        arena_alloc(&vm->compiler.parser_arena, sizeof(TypeNameRecord));
    memset(rec, 0, sizeof(TypeNameRecord));
    rec->ty       = ty;
    rec->name     = node->name;
    rec->name_len = name_len;
    rec->owner_fn = vm->compiler.current_fn;
    rec->is_tag   = true;
    // #891: comptime/reflection-synthesized -- no primary-file token to
    // check provenance against, and it must never be silently dropped from
    // -c=native / -M output the way a header-sourced type now is.
    rec->always_emit        = true;
    rec->next               = vm->compiler.type_names;
    vm->compiler.type_names = rec;
}

// Helper: expose a typedef by name so FindType(name) resolves it.
// Mirrors push_scope(...)->type_def = ty + record_type_name in parse.c.
static void reflect_push_typedef_scope(VirtualMachine *vm, const char *name,
                                       int name_len, Type *ty) {
    if (!vm || !vm->compiler.scope)
        return;

    VarScopeNode *node =
        arena_alloc(&vm->compiler.parser_arena, sizeof(VarScopeNode));
    memset(node, 0, sizeof(VarScopeNode));
    node->name               = arena_strdup(vm, name);
    node->name_len           = name_len;
    node->type_def           = ty;
    node->next               = vm->compiler.scope->vars;
    vm->compiler.scope->vars = node;
    hashmap_put2_borrowed(&vm->compiler.scope->var_map, node->name,
                          node->name_len, node);

    // Record for type_names list
    TypeNameRecord *rec =
        arena_alloc(&vm->compiler.parser_arena, sizeof(TypeNameRecord));
    memset(rec, 0, sizeof(TypeNameRecord));
    rec->ty       = ty;
    rec->name     = node->name;
    rec->name_len = name_len;
    rec->owner_fn = vm->compiler.current_fn;
    rec->is_tag   = false;
    // #891: see the matching comment in reflect_push_tag_scope above.
    rec->always_emit        = true;
    rec->next               = vm->compiler.type_names;
    vm->compiler.type_names = rec;
}

// MakeStruct(name) — create and expose a new struct type.
// Fields are added with StructAddField.
Type *__builtin_ast_make_struct(const char *name) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !name)
        return NULL;
    Type *ty       = struct_type(vm);
    int   name_len = (int)strlen(name);
    ty->name       = reflect_make_name_token(vm, name, name_len);
    ty->struct_tag =
        ty->name; // #900: survives any later declarator name-overwrite
    ty->size  = 0;
    ty->align = 1;
    reflect_push_tag_scope(vm, name, name_len, ty);
    return ty;
}

// MakeUnion(name) — create and expose a new union type.
Type *__builtin_ast_make_union(const char *name) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !name)
        return NULL;
    Type *ty       = union_type(vm);
    int   name_len = (int)strlen(name);
    ty->name       = reflect_make_name_token(vm, name, name_len);
    ty->struct_tag =
        ty->name; // #900: survives any later declarator name-overwrite
    ty->size  = 0;
    ty->align = 1;
    reflect_push_tag_scope(vm, name, name_len, ty);
    return ty;
}

// StructAddField(ty, name, field_type) — append a field to a struct or
// union type and recompute the aggregate size/alignment.
Type *__builtin_ast_struct_add_field(Type *ty, const char *name,
                                     Type *field_type) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !ty || !name || !field_type)
        return NULL;
    if (ty->kind != TY_STRUCT && ty->kind != TY_UNION)
        return NULL;

    int     name_len = (int)strlen(name);

    Member *mem      = arena_alloc(&vm->compiler.parser_arena, sizeof(Member));
    memset(mem, 0, sizeof(Member));
    mem->ty    = field_type;
    mem->name  = reflect_make_name_token(vm, name, name_len);
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
        int bits      = 0;
        int new_align = 1;
        for (Member *m = ty->members; m; m = m->next) {
            bits       = reflect_align_to(bits, m->align * 8);
            m->offset  = bits / 8;
            bits      += m->ty->size * 8;
            if (new_align < m->align)
                new_align = m->align;
        }
        ty->align = new_align;
        ty->size  = reflect_align_to(bits, ty->align * 8) / 8;
        if (ty->size == 0)
            ty->size = 1; // empty struct -> 1 byte
    } else {
        // Union: all members at offset 0, size = max member size
        int new_size  = 0;
        int new_align = 1;
        for (Member *m = ty->members; m; m = m->next) {
            m->offset = 0;
            if (new_align < m->align)
                new_align = m->align;
            if (new_size < m->ty->size)
                new_size = m->ty->size;
        }
        ty->align = new_align;
        ty->size  = reflect_align_to(new_size, new_align);
        if (ty->size == 0)
            ty->size = 1;
    }

    return ty;
}

// MakeEnum(name) — create and expose a new enum type.
// Constants are added with EnumAddConstant.
Type *__builtin_ast_make_enum(const char *name) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !name)
        return NULL;
    Type *ty       = enum_type(vm);
    int   name_len = (int)strlen(name);
    ty->name       = reflect_make_name_token(vm, name, name_len);
    ty->enum_tag =
        ty->name; // #900: survives any later declarator name-overwrite
    reflect_push_tag_scope(vm, name, name_len, ty);
    return ty;
}

// EnumAddConstant(ty, name, value) — add a named constant to an enum
// type and expose it as an integer constant in current scope.
void __builtin_ast_enum_add_constant(Type *ty, const char *name,
                                     int64_t value) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !ty || !name || ty->kind != TY_ENUM)
        return;

    int name_len = (int)strlen(name);

    // Append constant to type's enum_constants list
    EnumConstant *ec =
        arena_alloc(&vm->compiler.parser_arena, sizeof(EnumConstant));
    memset(ec, 0, sizeof(EnumConstant));
    ec->name  = arena_strdup(vm, name);
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
    sc->name                 = arena_strdup(vm, name);
    sc->name_len             = name_len;
    sc->enum_ty              = ty;
    sc->enum_val             = value;
    sc->next                 = vm->compiler.scope->vars;
    vm->compiler.scope->vars = sc;
    hashmap_put2_borrowed(&vm->compiler.scope->var_map, sc->name, sc->name_len,
                          sc);
}

// MakeTypedef(name, underlying) — register name as a typedef alias for
// underlying so that FindType(name) and C code can use it.
Type *__builtin_ast_make_typedef(const char *name, Type *underlying) {
    VirtualMachine *vm = __builtin_current_vm;
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

static ComptimeVar *find_comptime_var(VirtualMachine *vm, const char *name) {
    size_t len = strlen(name);
    for (ComptimeVar *cv = vm->compiler.comptime_vars; cv; cv = cv->next) {
        if (strlen(cv->name) == len && strncmp(cv->name, name, len) == 0)
            return cv;
    }
    return NULL;
}

int64_t __builtin_get_comptime_int(const char *name) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !name)
        return 0;
    ComptimeVar *cv = find_comptime_var(vm, name);
    if (!cv || !cv->is_evaluated || cv->is_struct)
        return 0;
    return cv->is_float ? (int64_t)cv->float_val : cv->int_val;
}

double __builtin_get_comptime_float(const char *name) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !name)
        return 0.0;
    ComptimeVar *cv = find_comptime_var(vm, name);
    if (!cv || !cv->is_evaluated || cv->is_struct)
        return 0.0;
    return cv->is_float ? cv->float_val : (double)cv->int_val;
}

Node *__builtin_get_comptime_var(const char *name) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !name)
        return NULL;
    ComptimeVar *cv = find_comptime_var(vm, name);
    if (!cv || !cv->is_evaluated || cv->is_struct)
        return NULL;
    if (cv->is_float)
        return __builtin_ast_float_literal(cv->float_val);
    return __builtin_ast_int_literal(cv->int_val);
}

Node *__builtin_get_comptime_ptr(const char *name) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !name)
        return NULL;
    ComptimeVar *cv = find_comptime_var(vm, name);
    if (!cv || !cv->is_evaluated || !cv->ptr_obj)
        return NULL;

    Node *var  = alloc_node(vm, ND_VAR);
    var->var   = cv->ptr_obj;
    var->ty    = cv->ptr_obj->ty;

    Node *addr = alloc_node(vm, ND_ADDR);
    addr->lhs  = var;
    add_type(vm, addr);
    return addr;
}

Node *__builtin_get_comptime_member(const char *var_name, const char *field) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !var_name || !field)
        return NULL;
    ComptimeVar *cv = find_comptime_var(vm, var_name);
    if (!cv || !cv->is_evaluated || !cv->is_struct)
        return NULL;
    size_t flen = strlen(field);
    for (ComptimeVarMember *m = cv->members; m; m = m->next) {
        if (m->name && strlen(m->name) == flen &&
            strncmp(m->name, field, flen) == 0) {
            if (m->is_float)
                return __builtin_ast_float_literal(m->float_val);
            return __builtin_ast_int_literal(m->int_val);
        }
    }
    return NULL;
}

Node *__builtin_get_constexpr_value(const char *name) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !name)
        return NULL;
    Obj *obj = (Obj *)__builtin_ast_find_global(name);
    if (!obj || !obj->is_constexpr || !obj->init_expr)
        return NULL;
    if (obj->ty->kind >= TY_FLOAT)
        return __builtin_ast_float_literal(cc_eval_double(vm, obj->init_expr));
    return __builtin_ast_int_literal(cc_eval(vm, obj->init_expr));
}
