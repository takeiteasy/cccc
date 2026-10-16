/*
 CCCC: Comprehensiev C Compensation Compiler - Macro Standard Library Attributes

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

// Handlers for the built-in comptime attributes (@serialize, @enum_to_string,
// @generate_getters, ...). A program that uses them includes this header; the
// handlers run at compile time and add nothing to the program itself.

#ifndef CCCC_DERIVE_H
#define CCCC_DERIVE_H
// ============================================================================
// Macro Standard Library Attributes
// ============================================================================

// The attributes below apply to `struct X {...};` and `typedef ... X;` alike.

// The Type an attribute target names, or a diagnostic for `attr` when the
// target is not a struct/union (want_enum == false) or enum (true) type.
@comptime Type *__builtin_attr_type_target(AttrTarget *target, const char *attr,
                                           bool want_enum) {
    int kind = GetAttrTargetKind(target);
    if (kind != ATTR_TARGET_TYPE && kind != ATTR_TARGET_TYPEDEF)
        MacroErrorAt(MakeIntLiteral(0), "%s expects a type or typedef target",
                     attr);

    Type *ty = AttrTargetType(target);
    if (!ty)
        MacroErrorAt(MakeIntLiteral(0), "%s target has no usable type", attr);

    int tk = GetTypeKind(ty);
    if (want_enum ? tk != TK_ENUM : (tk != TK_STRUCT && tk != TK_UNION))
        MacroErrorAt(MakeIntLiteral(0), "%s expects %s target", attr,
                     want_enum ? "an enum" : "a struct/union");
    return ty;
}

// The name an attribute target is declared with (tag or typedef name).
@comptime const char *__builtin_attr_type_name(AttrTarget *target,
                                               const char *attr) {
    const char *tname = AttrTargetName(target);
    if (!tname)
        MacroErrorAt(MakeIntLiteral(0), "%s target has no usable name", attr);
    return tname;
}

// @serialize struct Foo {...}; publishes
//   int Foo_serialize(struct Foo *self, void *buf);
// which copies *self into buf via Serialize and returns sizeof(struct Foo).
@comptime(attribute("serialize")) void __builtin_attr_serialize(
    AttrTarget *target) {
    Type       *ty    = __builtin_attr_type_target(target, "@serialize", false);
    const char *tname = __builtin_attr_type_name(target, "@serialize");

    char fname[128];
    strcpy(fname, tname);
    strcat(fname, "_serialize");

    Obj *fn = MakeFunction(fname, GetType("int"));
    FunctionAddParam(fn, "self", MakePointer(ty));
    FunctionAddParam(fn, "buf", MakePointer(GetType("void")));
    WithFn(fn) {
        Node *self  = MakeUnary(NK_DEREF, MakeParamRef(fn, "self"));
        Node *buf   = MakeParamRef(fn, "buf");
        Node *block = Serialize(ty, self, buf);
        BlockAddStmt(block, MakeReturn(MakeIntLiteral(TypeSize(ty))));
        FunctionSetBody(fn, block);
    }
    PublishNode(fn);
}

// @deserialize struct Foo {...}; publishes
//   struct Foo Foo_deserialize(void *buf);
// which reconstructs a struct Foo from buf via Deserialize.
@comptime(attribute("deserialize")) void __builtin_attr_deserialize(
    AttrTarget *target) {
    Type       *ty = __builtin_attr_type_target(target, "@deserialize", false);
    const char *tname = __builtin_attr_type_name(target, "@deserialize");

    char fname[128];
    strcpy(fname, tname);
    strcat(fname, "_deserialize");

    Obj *fn = MakeFunction(fname, ty);
    FunctionAddParam(fn, "buf", MakePointer(GetType("void")));
    WithFn(fn) {
        Node *buf = MakeParamRef(fn, "buf");
        FunctionSetBody(fn, MakeReturn(Deserialize(ty, buf)));
    }
    PublishNode(fn);
}

// @enum_to_string enum Color {...}; publishes
//   const char *Color_to_string(enum Color v);
// which switches over v and returns the matching constant's name, or "" if
// no constant matches. Constants sharing a value return the first name.
@comptime(attribute("enum_to_string")) void __builtin_attr_enum_to_string(
    AttrTarget *target) {
    Type *ty = __builtin_attr_type_target(target, "@enum_to_string", true);
    const char *tname = __builtin_attr_type_name(target, "@enum_to_string");

    char fname[128];
    strcpy(fname, tname);
    strcat(fname, "_to_string");

    Type *cstr_ty = MakePointer(MakeConst(GetType("char")));
    Obj  *fn      = MakeFunction(fname, cstr_ty);
    FunctionAddParam(fn, "v", ty);
    WithFn(fn) {
        Node *v = MakeParamRef(fn, "v");
        FunctionSetBody(fn, EnumToString(ty, v));
    }
    PublishNode(fn);
}

// @enum_from_string enum Color {...}; publishes
//   enum Color Color_from_string(const char *s);
// which compares s against each constant's name and returns the matching
// value, or -1 if no constant matches.
@comptime(attribute("enum_from_string")) void __builtin_attr_enum_from_string(
    AttrTarget *target) {
    Type *ty = __builtin_attr_type_target(target, "@enum_from_string", true);
    const char *tname = __builtin_attr_type_name(target, "@enum_from_string");

    char fname[128];
    strcpy(fname, tname);
    strcat(fname, "_from_string");

    Type *cstr_ty = MakePointer(MakeConst(GetType("char")));
    Obj  *fn      = MakeFunction(fname, ty);
    FunctionAddParam(fn, "s", cstr_ty);
    WithFn(fn) {
        Node *s = MakeParamRef(fn, "s");
        FunctionSetBody(fn, EnumFromString(ty, s));
    }
    PublishNode(fn);
}

// The optional name prefix passed as `@attr("Prefix")`: "" when absent.
@comptime const char *__builtin_attr_prefix_arg(const char *attr) {
    if (VarargCount() == 0)
        return "";
    if (VarargCount() > 1)
        MacroErrorAt(MakeIntLiteral(0),
                     "%s takes at most one argument (a name prefix)", attr);
    const char *prefix = StringLiteralValue(VarargAt(0));
    if (!prefix)
        MacroErrorAt(MakeIntLiteral(0), "%s expects a string literal prefix",
                     attr);
    return prefix;
}

// <prefix>_<verb>_<field>, or <verb>_<field> when prefix is empty.
@comptime void __builtin_accessor_name(char *out, const char *prefix,
                                       const char *verb, const char *field) {
    out[0] = 0;
    if (prefix[0]) {
        strcpy(out, prefix);
        strcat(out, "_");
    }
    strcat(out, verb);
    strcat(out, "_");
    strcat(out, field);
}

// Publishes the getter and/or setter of every member of struct/union `ty`
// (fields of anonymous struct/union members included) taking `self_ty *`.
// An array member's getter returns a pointer to its first element and its
// setter copies the whole array from a pointer.
@comptime void __builtin_generate_accessors(Type *self_ty, Type *ty,
                                            const char *prefix, bool getters,
                                            bool setters) {
    int count = StructMemberCount(ty);
    for (int i = 0; i < count; i++) {
        Member     *m     = StructMemberAt(ty, i);
        const char *field = MemberName(m);
        Type       *fty   = MemberType(m);
        if (!field) {
            int tk = GetTypeKind(fty);
            if (tk == TK_STRUCT || tk == TK_UNION)
                __builtin_generate_accessors(self_ty, fty, prefix, getters,
                                             setters);
            continue;
        }
        if (MemberHasAttr(m, "accessor_skip"))
            continue;

        bool is_array = GetTypeKind(fty) == TK_ARRAY;
        char name[256];

        if (getters) {
            __builtin_accessor_name(name, prefix, "get", field);
            Obj *fn =
                MakeFunction(name, is_array ? MakePointer(TypeBase(fty)) : fty);
            FunctionAddParam(fn, "self", MakePointer(self_ty));
            WithFn(fn) {
                Node *self = MakeUnary(NK_DEREF, MakeParamRef(fn, "self"));
                FunctionSetBody(fn, MakeReturn(MakeMember(self, field)));
            }
            PublishNode(fn);
        }

        if (setters) {
            __builtin_accessor_name(name, prefix, "set", field);
            Obj *fn = MakeFunction(name, GetType("void"));
            FunctionAddParam(fn, "self", MakePointer(self_ty));
            FunctionAddParam(fn, "value",
                             is_array ? MakePointer(MakeConst(TypeBase(fty)))
                                      : fty);
            WithFn(fn) {
                Node *self   = MakeUnary(NK_DEREF, MakeParamRef(fn, "self"));
                Node *target = MakeMember(self, field);
                Node *value  = MakeParamRef(fn, "value");
                Node *store  = is_array ? Memcpy(target, value,
                                                 MakeIntLiteral(TypeSize(fty)))
                                        : MakeAssign(target, value);
                FunctionSetBody(fn, MakeExprStmt(store));
            }
            PublishNode(fn);
        }
    }
}

// Constructor parameters mirror the members of `ty`, anonymous struct/union
// members flattened; `r` accumulates them in the body.
@comptime void __builtin_constructor_params(Obj *fn, Type *ty) {
    int count = StructMemberCount(ty);
    for (int i = 0; i < count; i++) {
        Member     *m     = StructMemberAt(ty, i);
        const char *field = MemberName(m);
        Type       *fty   = MemberType(m);
        if (!field) {
            int tk = GetTypeKind(fty);
            if (tk == TK_STRUCT || tk == TK_UNION)
                __builtin_constructor_params(fn, fty);
            continue;
        }
        if (MemberHasAttr(m, "constructor_skip"))
            continue;
        FunctionAddParam(fn, field,
                         GetTypeKind(fty) == TK_ARRAY
                             ? MakePointer(MakeConst(TypeBase(fty)))
                             : fty);
    }
}

@comptime void __builtin_constructor_stores(Obj *fn, Node *block, Node *r,
                                            Type *ty) {
    int count = StructMemberCount(ty);
    for (int i = 0; i < count; i++) {
        Member     *m     = StructMemberAt(ty, i);
        const char *field = MemberName(m);
        Type       *fty   = MemberType(m);
        if (!field) {
            int tk = GetTypeKind(fty);
            if (tk == TK_STRUCT || tk == TK_UNION)
                __builtin_constructor_stores(fn, block, r, fty);
            continue;
        }
        if (MemberHasAttr(m, "constructor_skip"))
            continue;
        Node *target = MakeMember(r, field);
        Node *value  = MakeParamRef(fn, field);
        Node *store = GetTypeKind(fty) == TK_ARRAY
                          ? Memcpy(target, value, MakeIntLiteral(TypeSize(fty)))
                          : MakeAssign(target, value);
        BlockAddStmt(block, MakeExprStmt(store));
    }
}

// __builtin_generate_constructor(ty, tname): publishes
// `<ty> <tname>_create(<member1>, <member2>, ...)` returning a zero-initialised
// `ty` with each member set from its parameter.
@comptime void __builtin_generate_constructor(Type *ty, const char *tname) {
    char gname[128];
    strcpy(gname, tname);
    strcat(gname, "_create");

    Obj *fn = MakeFunction(gname, ty);
    __builtin_constructor_params(fn, ty);

    WithFn(fn) {
        Node *r     = MakeLocalVar("r", ty);
        Node *block = MakeBlock(NULL, 0);
        BlockAddStmt(
            block, MakeExprStmt(MakeAssign(r, InitStruct(ty, NULL, NULL, 0))));
        __builtin_constructor_stores(fn, block, r, ty);
        BlockAddStmt(block, MakeReturn(r));
        FunctionSetBody(fn, block);
    }
    PublishNode(fn);
}

// @generate_getters struct Foo {...}; publishes get_<field>(struct Foo *self)
// for each member, returning self->field. @generate_getters("Foo") names them
// Foo_get_<field>.
@comptime(attribute("generate_getters")) void __builtin_attr_generate_getters(
    AttrTarget *target, ...) {
    Type *ty = __builtin_attr_type_target(target, "@generate_getters", false);
    const char *prefix = __builtin_attr_prefix_arg("@generate_getters");
    __builtin_generate_accessors(ty, ty, prefix, true, false);
}

// @generate_setters struct Foo {...}; publishes set_<field>(struct Foo *self,
// <FieldType> value) for each member, assigning self->field = value.
// @generate_setters("Foo") names them Foo_set_<field>.
@comptime(attribute("generate_setters")) void __builtin_attr_generate_setters(
    AttrTarget *target, ...) {
    Type *ty = __builtin_attr_type_target(target, "@generate_setters", false);
    const char *prefix = __builtin_attr_prefix_arg("@generate_setters");
    __builtin_generate_accessors(ty, ty, prefix, false, true);
}

// @generate_accessors struct Foo {...}; publishes both the getter and the
// setter of every member. @generate_accessors("Foo") prefixes both names.
@comptime(attribute(
    "generate_accessors")) void __builtin_attr_generate_accessors(AttrTarget
                                                                      *target,
                                                                  ...) {
    Type *ty = __builtin_attr_type_target(target, "@generate_accessors", false);
    const char *prefix = __builtin_attr_prefix_arg("@generate_accessors");
    __builtin_generate_accessors(ty, ty, prefix, true, true);
}

// @generate_constructor struct Foo {...}; publishes
//   struct Foo Foo_create(<member1>, <member2>, ...);
// returning a struct Foo initialized from the given member values.
@comptime(attribute("generate_constructor")) void
__builtin_attr_generate_constructor(AttrTarget *target) {
    Type *ty =
        __builtin_attr_type_target(target, "@generate_constructor", false);
    const char *tname =
        __builtin_attr_type_name(target, "@generate_constructor");
    __builtin_generate_constructor(ty, tname);
}

// ---- @to_json / @from_json ---------------------------------------------------
// Both generate a function over a struct whose members are integers, floats,
// bool, enums, char arrays (JSON strings), arrays of those, and nested structs
// (expanded in place), and need #include <cccc/json.h>. The member kinds:
//   1 signed integer / enum   2 unsigned integer   3 float   4 bool
//   5 char[N] string          6 other array        7 struct   0 unsupported

@comptime void __builtin_json_require(const char *attr) {
    if (!FindGlobal("__cccc_json_w_init"))
        MacroErrorAt(MakeIntLiteral(0), "%s needs #include <cccc/json.h>",
                     attr);
}

@comptime int __builtin_json_kind(Type *ty) {
    int tk = GetTypeKind(ty);
    if (tk == TK_BOOL)
        return 4;
    if (tk == TK_ENUM)
        return 1;
    if (tk == TK_CHAR || tk == TK_SHORT || tk == TK_INT || tk == TK_LONG)
        return TypeIsUnsigned(ty) ? 2 : 1;
    if (tk == TK_FLOAT || tk == TK_DOUBLE || tk == TK_LDOUBLE)
        return 3;
    if (tk == TK_ARRAY)
        return GetTypeKind(TypeBase(ty)) == TK_CHAR ? 5 : 6;
    if (tk == TK_STRUCT)
        return 7;
    return 0;
}

@comptime Node *__builtin_json_call1(const char *fn, Node *a) {
    Node *args[1] = {a};
    return MakeFuncCall(MakeVarRef(fn), args, 1);
}

@comptime Node *__builtin_json_call2(const char *fn, Node *a, Node *b) {
    Node *args[2] = {a, b};
    return MakeFuncCall(MakeVarRef(fn), args, 2);
}

@comptime Node *__builtin_json_call3(const char *fn, Node *a, Node *b,
                                     Node *c) {
    Node *args[3] = {a, b, c};
    return MakeFuncCall(MakeVarRef(fn), args, 3);
}

@comptime Node *__builtin_json_call4(const char *fn, Node *a, Node *b, Node *c,
                                     Node *d) {
    Node *args[4] = {a, b, c, d};
    return MakeFuncCall(MakeVarRef(fn), args, 4);
}

@comptime Node *__builtin_json_addr(Node *x) {
    return MakeUnary(NK_ADDR, x);
}

@comptime Node *__builtin_json_charp(Node *array) {
    return MakeCast(MakeUnary(NK_ADDR, array),
                    MakePointer(MakeConst(GetType("char"))));
}

@comptime void __builtin_json_put_ch(Node *block, Node *w, int c) {
    BlockAddStmt(block,
                 MakeExprStmt(__builtin_json_call2("__cccc_json_w_ch",
                                                   __builtin_json_addr(w),
                                                   MakeIntLiteral(c))));
}

// Return 1 from the generated function when `call` (an int-returning reader)
// reports an error.
@comptime void __builtin_json_fail_if(Node *block, Node *call) {
    BlockAddStmt(block, MakeIf(call, MakeReturn(MakeIntLiteral(1)), NULL));
}

// Flattens the members of `ty` (anonymous struct members included) into
// names/keys/types/exprs, where exprs[i] reads member i of `e`, names[i] is the
// C member name and keys[i] its JSON key (@json_name). @json_skip members are
// left out.
@comptime void __builtin_json_collect(Type *ty, Node *e, const char *attr,
                                      const char **names, const char **keys,
                                      Type **types, Node **exprs, int *n) {
    int count = StructMemberCount(ty);
    for (int i = 0; i < count; i++) {
        Member     *m     = StructMemberAt(ty, i);
        const char *field = MemberName(m);
        Type       *fty   = MemberType(m);
        if (!field) {
            int tk = GetTypeKind(fty);
            if (tk == TK_UNION)
                MacroErrorAt(MakeIntLiteral(0),
                             "%s does not support an anonymous union member",
                             attr);
            if (tk == TK_STRUCT)
                __builtin_json_collect(fty, e, attr, names, keys, types, exprs,
                                       n);
            continue;
        }
        if (MemberHasAttr(m, "json_skip"))
            continue;
        if (*n >= 128)
            MacroErrorAt(MakeIntLiteral(0), "%s supports at most 128 members",
                         attr);
        const char *key = field;
        if (MemberHasAttr(m, "json_name"))
            key = StringLiteralValue(MemberAttrArg(m, "json_name", 0));
        for (int j = 0; j < *n; j++)
            if (!strcmp(keys[j], key))
                MacroErrorAt(MakeIntLiteral(0), "%s: duplicate JSON key '%s'",
                             attr, key);
        names[*n] = field;
        keys[*n]  = key;
        types[*n] = fty;
        exprs[*n] = MakeMember(e, field);
        (*n)++;
    }
}

@comptime void __builtin_json_write_value(Node *block, Node *w, Type *ty,
                                          Node *e, const char *attr,
                                          const char *path) {
    int k = __builtin_json_kind(ty);
    if (k == 0)
        MacroErrorAt(MakeIntLiteral(0),
                     "%s: member '%s' has a type JSON cannot represent", attr,
                     path);
    Node *wp = __builtin_json_addr(w);
    if (k == 1) {
        BlockAddStmt(block, MakeExprStmt(__builtin_json_call2(
                                "__cccc_json_w_i64", wp,
                                MakeCast(e, GetType("long long")))));
    } else if (k == 2) {
        BlockAddStmt(block, MakeExprStmt(__builtin_json_call2(
                                "__cccc_json_w_u64", wp,
                                MakeCast(e, GetType("unsigned long long")))));
    } else if (k == 3) {
        BlockAddStmt(block, MakeExprStmt(__builtin_json_call2(
                                "__cccc_json_w_f64", wp,
                                MakeCast(e, GetType("double")))));
    } else if (k == 4) {
        BlockAddStmt(block,
                     MakeExprStmt(__builtin_json_call2(
                         "__cccc_json_w_bool", wp, MakeCast(e, GetType("int")))));
    } else if (k == 5) {
        BlockAddStmt(block,
                     MakeExprStmt(__builtin_json_call3(
                         "__cccc_json_w_str", wp, __builtin_json_charp(e),
                         MakeIntLiteral(TypeArrayLen(ty)))));
    } else if (k == 6) {
        char elem_path[256];
        strcpy(elem_path, path);
        strcat(elem_path, "[]");
        Node *i     = MakeLocalVarUnique(GetType("int"));
        Node *body  = MakeBlock(NULL, 0);
        Node *comma = MakeBlock(NULL, 0);
        __builtin_json_put_ch(comma, w, ',');
        BlockAddStmt(body, MakeIf(i, comma, NULL));
        __builtin_json_write_value(body, w, TypeBase(ty), MakeSubscript(e, i),
                                   attr, elem_path);
        __builtin_json_put_ch(block, w, '[');
        BlockAddStmt(
            block,
            MakeFor(MakeExprStmt(MakeAssign(i, MakeIntLiteral(0))),
                    MakeBinary(NK_LT, i, MakeIntLiteral(TypeArrayLen(ty))),
                    MakeAssign(i, MakeBinary(NK_ADD, i, MakeIntLiteral(1))),
                    body));
        __builtin_json_put_ch(block, w, ']');
    } else {
        const char *names[128];
        const char *keys[128];
        Type       *types[128];
        Node       *exprs[128];
        int         n = 0;
        __builtin_json_collect(ty, e, attr, names, keys, types, exprs, &n);
        __builtin_json_put_ch(block, w, '{');
        for (int j = 0; j < n; j++) {
            char sub[256];
            strcpy(sub, path);
            strcat(sub, ".");
            strcat(sub, names[j]);
            if (j > 0)
                __builtin_json_put_ch(block, w, ',');
            BlockAddStmt(block, MakeExprStmt(__builtin_json_call2(
                                    "__cccc_json_w_key", __builtin_json_addr(w),
                                    MakeStringLiteral(keys[j]))));
            __builtin_json_write_value(block, w, types[j], exprs[j], attr, sub);
        }
        __builtin_json_put_ch(block, w, '}');
    }
}

@comptime void __builtin_json_read_value(Node *block, Node *r, Type *ty,
                                         Node *e, const char *attr,
                                         const char *path) {
    int k = __builtin_json_kind(ty);
    if (k == 0)
        MacroErrorAt(MakeIntLiteral(0),
                     "%s: member '%s' has a type JSON cannot represent", attr,
                     path);
    Node *rp = __builtin_json_addr(r);
    if (k >= 1 && k <= 4) {
        const char *scalar = k == 1   ? "long long"
                             : k == 2 ? "unsigned long long"
                             : k == 3 ? "double"
                                      : "int";
        const char *reader = k == 1   ? "__cccc_json_r_int"
                             : k == 2 ? "__cccc_json_r_uint"
                             : k == 3 ? "__cccc_json_r_f64"
                                      : "__cccc_json_r_bool";
        Node *t = MakeLocalVarUnique(GetType(scalar));
        Node *call = (k == 1 || k == 2)
                         ? __builtin_json_call3(reader, rp,
                                                __builtin_json_addr(t),
                                                MakeIntLiteral(TypeSize(ty)))
                         : __builtin_json_call2(reader, rp,
                                                __builtin_json_addr(t));
        __builtin_json_fail_if(block, call);
        BlockAddStmt(block, MakeExprStmt(MakeAssign(e, MakeCast(t, ty))));
    } else if (k == 5) {
        __builtin_json_fail_if(
            block, __builtin_json_call3("__cccc_json_r_str", rp,
                                        MakeCast(__builtin_json_addr(e),
                                                 MakePointer(GetType("char"))),
                                        MakeIntLiteral(TypeArrayLen(ty))));
    } else if (k == 6) {
        char elem_path[256];
        strcpy(elem_path, path);
        strcat(elem_path, "[]");
        Node *i     = MakeLocalVarUnique(GetType("int"));
        Node *body  = MakeBlock(NULL, 0);
        Node *comma = MakeBlock(NULL, 0);
        __builtin_json_fail_if(
            comma, __builtin_json_call2("__cccc_json_r_open",
                                        __builtin_json_addr(r),
                                        MakeIntLiteral(',')));
        BlockAddStmt(body, MakeIf(i, comma, NULL));
        __builtin_json_read_value(body, r, TypeBase(ty), MakeSubscript(e, i),
                                  attr, elem_path);
        __builtin_json_fail_if(
            block, __builtin_json_call2("__cccc_json_r_open",
                                        __builtin_json_addr(r),
                                        MakeIntLiteral('[')));
        BlockAddStmt(
            block,
            MakeFor(MakeExprStmt(MakeAssign(i, MakeIntLiteral(0))),
                    MakeBinary(NK_LT, i, MakeIntLiteral(TypeArrayLen(ty))),
                    MakeAssign(i, MakeBinary(NK_ADD, i, MakeIntLiteral(1))),
                    body));
        __builtin_json_fail_if(
            block, __builtin_json_call2("__cccc_json_r_open",
                                        __builtin_json_addr(r),
                                        MakeIntLiteral(']')));
    } else {
        const char *names[128];
        const char *keys[128];
        Type       *types[128];
        Node       *exprs[128];
        int         n = 0;
        __builtin_json_collect(ty, e, attr, names, keys, types, exprs, &n);

        Node *seen =
            MakeLocalVarUnique(MakeArray(GetType("unsigned char"), n ? n : 1));
        Node *key   = MakeLocalVarUnique(MakeArray(GetType("char"), 128));
        Node *first = MakeLocalVarUnique(GetType("int"));
        Node *next  = MakeLocalVarUnique(GetType("int"));
        Node *uchar_p = MakePointer(GetType("unsigned char"));

        __builtin_json_fail_if(
            block, __builtin_json_call2("__cccc_json_r_open",
                                        __builtin_json_addr(r),
                                        MakeIntLiteral('{')));
        BlockAddStmt(block,
                     MakeExprStmt(__builtin_json_call2(
                         "__cccc_json_zero",
                         MakeCast(__builtin_json_addr(seen), uchar_p),
                         MakeIntLiteral(n ? n : 1))));
        BlockAddStmt(block, MakeExprStmt(MakeAssign(first, MakeIntLiteral(1))));

        // if (key_is(key, "a")) {...} else if ... else if (r_skip(&r)) return 1;
        Node *chain = MakeIf(__builtin_json_call1("__cccc_json_r_skip",
                                                  __builtin_json_addr(r)),
                             MakeReturn(MakeIntLiteral(1)), NULL);
        for (int j = n - 1; j >= 0; j--) {
            char sub[256];
            strcpy(sub, path);
            strcat(sub, ".");
            strcat(sub, names[j]);
            Node *member = MakeBlock(NULL, 0);
            __builtin_json_read_value(member, r, types[j], exprs[j], attr, sub);
            BlockAddStmt(member,
                         MakeExprStmt(MakeAssign(
                             MakeSubscript(seen, MakeIntLiteral(j)),
                             MakeIntLiteral(1))));
            Node *is_key = __builtin_json_call2("__cccc_json_key_is",
                                                __builtin_json_charp(key),
                                                MakeStringLiteral(keys[j]));
            chain = MakeIf(is_key, member, chain);
        }

        Node *loop_cond = MakeBinary(
            NK_LT, MakeIntLiteral(0),
            MakeAssign(next, __builtin_json_call4(
                                 "__cccc_json_r_key", __builtin_json_addr(r),
                                 MakeCast(__builtin_json_addr(key),
                                          MakePointer(GetType("char"))),
                                 MakeIntLiteral(128),
                                 __builtin_json_addr(first))));
        BlockAddStmt(block, MakeWhile(loop_cond, chain));
        BlockAddStmt(block, MakeIf(MakeBinary(NK_LT, next, MakeIntLiteral(0)),
                                   MakeReturn(MakeIntLiteral(1)), NULL));
        BlockAddStmt(
            block,
            MakeIf(MakeUnary(NK_NOT,
                             __builtin_json_call2(
                                 "__cccc_json_all_set",
                                 MakeCast(__builtin_json_addr(seen), uchar_p),
                                 MakeIntLiteral(n ? n : 1))),
                   MakeReturn(MakeIntLiteral(1)), NULL));
    }
}

// ---- @json_skip / @json_name -------------------------------------------
// Member attributes read by @to_json and @from_json: `int x @json_skip;` leaves
// the member out of the JSON, `int x @json_name("id");` uses another key.

@comptime void __builtin_json_member_target(AttrTarget *target,
                                            const char *attr) {
    if (GetAttrTargetKind(target) != ATTR_TARGET_MEMBER)
        MacroErrorAt(MakeIntLiteral(0), "%s applies to a struct member", attr);
}

@comptime(attribute("json_skip")) void __builtin_attr_json_skip(
    AttrTarget *target, ...) {
    __builtin_json_member_target(target, "@json_skip");
    if (VarargCount() != 0)
        MacroErrorAt(MakeIntLiteral(0), "@json_skip takes no arguments");
    if (MemberHasAttr(AttrTargetMember(target), "json_name"))
        MacroErrorAt(MakeIntLiteral(0),
                     "@json_skip and @json_name cannot share a member");
}

@comptime(attribute("json_name")) void __builtin_attr_json_name(
    AttrTarget *target, ...) {
    __builtin_json_member_target(target, "@json_name");
    if (VarargCount() != 1)
        MacroErrorAt(MakeIntLiteral(0),
                     "@json_name takes one string literal key");
    const char *key = StringLiteralValue(VarargAt(0));
    if (!key || !key[0])
        MacroErrorAt(MakeIntLiteral(0),
                     "@json_name expects a non-empty string literal key");
    if (strlen(key) >= 128)
        MacroErrorAt(MakeIntLiteral(0),
                     "@json_name key is longer than 127 characters");
}

// <prefix>_<Type>_<suffix>, or <Type>_<suffix> when the prefix is empty.
@comptime void __builtin_json_fn_name(char *out, const char *prefix,
                                      const char *tname, const char *suffix) {
    out[0] = 0;
    if (prefix[0]) {
        strcpy(out, prefix);
        strcat(out, "_");
    }
    strcat(out, tname);
    strcat(out, suffix);
}

// @to_json struct Foo {...}; publishes
//   size_t Foo_to_json(const struct Foo *self, char *buf, size_t cap);
// which writes the struct as a JSON object and returns the length it needs
// (snprintf style: buf is always NUL-terminated when cap > 0).
// @to_json("P") publishes P_Foo_to_json.
@comptime(attribute("to_json")) void __builtin_attr_to_json(AttrTarget *target,
                                                            ...) {
    Type *ty = __builtin_attr_type_target(target, "@to_json", false);
    if (GetTypeKind(ty) == TK_UNION)
        MacroErrorAt(MakeIntLiteral(0), "@to_json does not support unions");
    const char *tname  = __builtin_attr_type_name(target, "@to_json");
    const char *prefix = __builtin_attr_prefix_arg("@to_json");
    __builtin_json_require("@to_json");

    char fname[256];
    __builtin_json_fn_name(fname, prefix, tname, "_to_json");

    Obj *fn = MakeFunction(fname, GetType("size_t"));
    FunctionAddParam(fn, "self", MakePointer(MakeConst(ty)));
    FunctionAddParam(fn, "buf", MakePointer(GetType("char")));
    FunctionAddParam(fn, "cap", GetType("size_t"));
    WithFn(fn) {
        Node *block = MakeBlock(NULL, 0);
        Node *w     = MakeLocalVarUnique(GetType("__cccc_json_w"));
        BlockAddStmt(block, MakeExprStmt(__builtin_json_call3(
                                "__cccc_json_w_init", __builtin_json_addr(w),
                                MakeParamRef(fn, "buf"),
                                MakeParamRef(fn, "cap"))));
        Node *self = MakeUnary(NK_DEREF, MakeParamRef(fn, "self"));
        __builtin_json_write_value(block, w, ty, self, "@to_json", tname);
        BlockAddStmt(block, MakeReturn(__builtin_json_call1(
                                "__cccc_json_w_end", __builtin_json_addr(w))));
        FunctionSetBody(fn, block);
    }
    PublishNode(fn);
}

// @from_json struct Foo {...}; publishes
//   int Foo_from_json(struct Foo *out, const char *json);
// which reads a JSON object into *out and returns 0, or nonzero on malformed
// input, a missing or mistyped member, or a value that does not fit.
// Members the struct lacks are skipped. @from_json("P") publishes
// P_Foo_from_json.
@comptime(attribute("from_json")) void __builtin_attr_from_json(
    AttrTarget *target, ...) {
    Type *ty = __builtin_attr_type_target(target, "@from_json", false);
    if (GetTypeKind(ty) == TK_UNION)
        MacroErrorAt(MakeIntLiteral(0), "@from_json does not support unions");
    const char *tname  = __builtin_attr_type_name(target, "@from_json");
    const char *prefix = __builtin_attr_prefix_arg("@from_json");
    __builtin_json_require("@from_json");

    char fname[256];
    __builtin_json_fn_name(fname, prefix, tname, "_from_json");

    Obj *fn = MakeFunction(fname, GetType("int"));
    FunctionAddParam(fn, "out", MakePointer(ty));
    FunctionAddParam(fn, "json", MakePointer(MakeConst(GetType("char"))));
    WithFn(fn) {
        Node *block = MakeBlock(NULL, 0);
        Node *r     = MakeLocalVarUnique(GetType("__cccc_json_r"));
        BlockAddStmt(block, MakeExprStmt(__builtin_json_call2(
                                "__cccc_json_r_init", __builtin_json_addr(r),
                                MakeParamRef(fn, "json"))));
        Node *out = MakeUnary(NK_DEREF, MakeParamRef(fn, "out"));
        __builtin_json_read_value(block, r, ty, out, "@from_json", tname);
        BlockAddStmt(
            block,
            MakeIf(MakeUnary(NK_NOT,
                             __builtin_json_call1("__cccc_json_r_at_end",
                                                  __builtin_json_addr(r))),
                   MakeReturn(MakeIntLiteral(1)), NULL));
        BlockAddStmt(block, MakeReturn(MakeIntLiteral(0)));
        FunctionSetBody(fn, block);
    }
    PublishNode(fn);
}

// ---- @equals / @hash / @dump
// ------------------------------------------------- Each generates one function
// that walks the members of a struct (nested and anonymous structs expanded in
// place, a union is rejected). Member kinds, as in the JSON section plus 8
// pointer.

@comptime int __builtin_derive_kind(Type *ty) {
    if (GetTypeKind(ty) == TK_PTR)
        return 8;
    return __builtin_json_kind(ty);
}

@comptime Type *__builtin_derive_struct_target(AttrTarget *target,
                                               const char *attr) {
    Type *ty = __builtin_attr_type_target(target, attr, false);
    if (GetTypeKind(ty) == TK_UNION)
        MacroErrorAt(MakeIntLiteral(0), "%s does not support unions", attr);
    return ty;
}

// Flattens the members of `ty` (anonymous structs included) that do not carry
// the member attribute `skip`; exprs[i] reads member i of `e`.
@comptime void __builtin_derive_collect(Type *ty, Node *e, const char *attr,
                                        const char *skip, const char **names,
                                        Type **types, Node **exprs, int *n) {
    int count = StructMemberCount(ty);
    for (int i = 0; i < count; i++) {
        Member     *m     = StructMemberAt(ty, i);
        const char *field = MemberName(m);
        Type       *fty   = MemberType(m);
        if (!field) {
            int tk = GetTypeKind(fty);
            if (tk == TK_UNION)
                MacroErrorAt(MakeIntLiteral(0),
                             "%s does not support an anonymous union member",
                             attr);
            if (tk == TK_STRUCT)
                __builtin_derive_collect(fty, e, attr, skip, names, types,
                                         exprs, n);
            continue;
        }
        if (MemberHasAttr(m, skip))
            continue;
        if (*n >= 128)
            MacroErrorAt(MakeIntLiteral(0), "%s supports at most 128 members",
                         attr);
        names[*n] = field;
        types[*n] = fty;
        exprs[*n] = MakeMember(e, field);
        (*n)++;
    }
}

@comptime void __builtin_derive_bad_member(const char *attr, const char *path) {
    MacroErrorAt(MakeIntLiteral(0), "%s: member '%s' has an unsupported type",
                 attr, path);
}

@comptime void __builtin_derive_path(char *out, const char *path,
                                     const char *suffix) {
    strcpy(out, path);
    strcat(out, suffix);
}

@comptime void __builtin_derive_skip_target(AttrTarget *target,
                                            const char *attr) {
    if (GetAttrTargetKind(target) != ATTR_TARGET_MEMBER)
        MacroErrorAt(MakeIntLiteral(0), "%s applies to a struct member", attr);
    if (VarargCount() != 0)
        MacroErrorAt(MakeIntLiteral(0), "%s takes no arguments", attr);
}

// @equals_skip leaves a member out of @equals and @hash, @dump_skip out of
// @dump: `int cache @equals_skip;`.
@comptime(attribute("equals_skip")) void __builtin_attr_equals_skip(
    AttrTarget *target, ...) {
    __builtin_derive_skip_target(target, "@equals_skip");
}

@comptime(attribute("dump_skip")) void __builtin_attr_dump_skip(
    AttrTarget *target, ...) {
    __builtin_derive_skip_target(target, "@dump_skip");
}

// @serialize_skip leaves a member out of @serialize, @deserialize and the
// Serialize/Deserialize builders, @accessor_skip out of the @generate_getters,
// setters and accessors functions, @constructor_skip out of
// @generate_constructor.
@comptime(attribute("serialize_skip")) void __builtin_attr_serialize_skip(
    AttrTarget *target, ...) {
    __builtin_derive_skip_target(target, "@serialize_skip");
    if (MemberIsBitfield(AttrTargetMember(target)))
        MacroErrorAt(MakeIntLiteral(0),
                     "@serialize_skip cannot apply to a bit-field");
}

@comptime(attribute("accessor_skip")) void __builtin_attr_accessor_skip(
    AttrTarget *target, ...) {
    __builtin_derive_skip_target(target, "@accessor_skip");
}

@comptime(attribute("constructor_skip")) void __builtin_attr_constructor_skip(
    AttrTarget *target, ...) {
    __builtin_derive_skip_target(target, "@constructor_skip");
}

// if (a != b) return 0; for every leaf. char[N] compares as a C string.
@comptime void __builtin_equals_value(Node *block, Type *ty, Node *a, Node *b,
                                      const char *path) {
    int k = __builtin_derive_kind(ty);
    if (k == 0)
        __builtin_derive_bad_member("@equals", path);
    if (k == 5) {
        Node *i    = MakeLocalVarUnique(GetType("int"));
        Node *done = MakeLocalVarUnique(GetType("int"));
        Node *step = MakeBlock(NULL, 0);
        BlockAddStmt(step, MakeIf(MakeBinary(NK_NE, MakeSubscript(a, i),
                                             MakeSubscript(b, i)),
                                  MakeReturn(MakeIntLiteral(0)), NULL));
        BlockAddStmt(step,
                     MakeIf(MakeUnary(NK_NOT, MakeSubscript(a, i)),
                            MakeExprStmt(MakeAssign(done, MakeIntLiteral(1))),
                            NULL));
        Node *body = MakeBlock(NULL, 0);
        BlockAddStmt(body, MakeIf(MakeUnary(NK_NOT, done), step, NULL));
        BlockAddStmt(block, MakeExprStmt(MakeAssign(done, MakeIntLiteral(0))));
        BlockAddStmt(
            block,
            MakeFor(MakeExprStmt(MakeAssign(i, MakeIntLiteral(0))),
                    MakeBinary(NK_LT, i, MakeIntLiteral(TypeArrayLen(ty))),
                    MakeAssign(i, MakeBinary(NK_ADD, i, MakeIntLiteral(1))),
                    body));
    } else if (k == 6) {
        char elem[256];
        __builtin_derive_path(elem, path, "[]");
        Node *i    = MakeLocalVarUnique(GetType("int"));
        Node *body = MakeBlock(NULL, 0);
        __builtin_equals_value(body, TypeBase(ty), MakeSubscript(a, i),
                               MakeSubscript(b, i), elem);
        BlockAddStmt(
            block,
            MakeFor(MakeExprStmt(MakeAssign(i, MakeIntLiteral(0))),
                    MakeBinary(NK_LT, i, MakeIntLiteral(TypeArrayLen(ty))),
                    MakeAssign(i, MakeBinary(NK_ADD, i, MakeIntLiteral(1))),
                    body));
    } else if (k == 7) {
        const char *names[128];
        Type       *types[128];
        Node       *ea[128];
        Node       *eb[128];
        int         n = 0;
        int         m = 0;
        __builtin_derive_collect(ty, a, "@equals", "equals_skip", names, types,
                                 ea, &n);
        __builtin_derive_collect(ty, b, "@equals", "equals_skip", names, types,
                                 eb, &m);
        for (int j = 0; j < n; j++) {
            char sub[256];
            __builtin_derive_path(sub, path, ".");
            strcat(sub, names[j]);
            __builtin_equals_value(block, types[j], ea[j], eb[j], sub);
        }
    } else {
        BlockAddStmt(block, MakeIf(MakeBinary(NK_NE, a, b),
                                   MakeReturn(MakeIntLiteral(0)), NULL));
    }
}

// h = (h ^ byte) * prime for the 8 bytes of `value` (an unsigned long long
// expression): FNV-1a.
@comptime void __builtin_hash_mix(Node *block, Node *h, Node *value) {
    Type *u64 = GetType("unsigned long long");
    Node *t   = MakeLocalVarUnique(u64);
    Node *k   = MakeLocalVarUnique(GetType("int"));
    BlockAddStmt(block, MakeExprStmt(MakeAssign(t, value)));
    Node *byte = MakeBinary(
        NK_BITAND,
        MakeBinary(NK_SHR, t, MakeBinary(NK_MUL, k, MakeIntLiteral(8))),
        MakeIntLiteral(255));
    Node *step =
        MakeAssign(h, MakeBinary(NK_MUL, MakeBinary(NK_BITXOR, h, byte),
                                 MakeCast(MakeIntLiteral(1099511628211), u64)));
    BlockAddStmt(
        block, MakeFor(MakeExprStmt(MakeAssign(k, MakeIntLiteral(0))),
                       MakeBinary(NK_LT, k, MakeIntLiteral(8)),
                       MakeAssign(k, MakeBinary(NK_ADD, k, MakeIntLiteral(1))),
                       MakeExprStmt(step)));
}

@comptime void __builtin_hash_value(Node *block, Node *h, Type *ty, Node *e,
                                    const char *path) {
    Type *u64 = GetType("unsigned long long");
    int   k   = __builtin_derive_kind(ty);
    if (k == 0)
        __builtin_derive_bad_member("@hash", path);
    if (k == 1 || k == 2 || k == 4 || k == 8) {
        __builtin_hash_mix(block, h, MakeCast(e, u64));
    } else if (k == 3) {
        // x + 0.0 turns -0.0 into 0.0, which compares equal to it.
        Node *d = MakeLocalVarUnique(GetType("double"));
        Node *t = MakeLocalVarUnique(u64);
        BlockAddStmt(block,
                     MakeExprStmt(MakeAssign(
                         d, MakeBinary(NK_ADD, MakeCast(e, GetType("double")),
                                       MakeFloatLiteral(0.0)))));
        BlockAddStmt(block, MakeExprStmt(Memcpy(MakeUnary(NK_ADDR, t),
                                                MakeUnary(NK_ADDR, d),
                                                MakeIntLiteral(8))));
        __builtin_hash_mix(block, h, t);
    } else if (k == 5) {
        Node *i    = MakeLocalVarUnique(GetType("int"));
        Node *done = MakeLocalVarUnique(GetType("int"));
        Node *mix  = MakeBlock(NULL, 0);
        __builtin_hash_mix(
            mix, h,
            MakeCast(MakeCast(MakeSubscript(e, i), GetType("unsigned char")),
                     u64));
        Node *step = MakeBlock(NULL, 0);
        BlockAddStmt(step,
                     MakeIf(MakeUnary(NK_NOT, MakeSubscript(e, i)),
                            MakeExprStmt(MakeAssign(done, MakeIntLiteral(1))),
                            mix));
        Node *body = MakeBlock(NULL, 0);
        BlockAddStmt(body, MakeIf(MakeUnary(NK_NOT, done), step, NULL));
        BlockAddStmt(block, MakeExprStmt(MakeAssign(done, MakeIntLiteral(0))));
        BlockAddStmt(
            block,
            MakeFor(MakeExprStmt(MakeAssign(i, MakeIntLiteral(0))),
                    MakeBinary(NK_LT, i, MakeIntLiteral(TypeArrayLen(ty))),
                    MakeAssign(i, MakeBinary(NK_ADD, i, MakeIntLiteral(1))),
                    body));
    } else if (k == 6) {
        char elem[256];
        __builtin_derive_path(elem, path, "[]");
        Node *i    = MakeLocalVarUnique(GetType("int"));
        Node *body = MakeBlock(NULL, 0);
        __builtin_hash_value(body, h, TypeBase(ty), MakeSubscript(e, i), elem);
        BlockAddStmt(
            block,
            MakeFor(MakeExprStmt(MakeAssign(i, MakeIntLiteral(0))),
                    MakeBinary(NK_LT, i, MakeIntLiteral(TypeArrayLen(ty))),
                    MakeAssign(i, MakeBinary(NK_ADD, i, MakeIntLiteral(1))),
                    body));
    } else {
        const char *names[128];
        Type       *types[128];
        Node       *exprs[128];
        int         n = 0;
        __builtin_derive_collect(ty, e, "@hash", "equals_skip", names, types,
                                 exprs, &n);
        for (int j = 0; j < n; j++) {
            char sub[256];
            __builtin_derive_path(sub, path, ".");
            strcat(sub, names[j]);
            __builtin_hash_value(block, h, types[j], exprs[j], sub);
        }
    }
}

// @equals struct Foo {...}; publishes
//   _Bool Foo_equals(const struct Foo *a, const struct Foo *b);
// which compares the members one by one (padding is ignored): scalars and
// pointers with ==, char[N] as a C string, other arrays element-wise, nested
// structs member-wise. @equals("P") publishes P_Foo_equals.
@comptime(attribute("equals")) void __builtin_attr_equals(AttrTarget *target,
                                                          ...) {
    Type       *ty     = __builtin_derive_struct_target(target, "@equals");
    const char *tname  = __builtin_attr_type_name(target, "@equals");
    const char *prefix = __builtin_attr_prefix_arg("@equals");

    char        fname[256];
    __builtin_json_fn_name(fname, prefix, tname, "_equals");

    Obj *fn = MakeFunction(fname, GetType("_Bool"));
    FunctionAddParam(fn, "a", MakePointer(MakeConst(ty)));
    FunctionAddParam(fn, "b", MakePointer(MakeConst(ty)));
    WithFn(fn) {
        Node *block = MakeBlock(NULL, 0);
        __builtin_equals_value(
            block, ty, MakeUnary(NK_DEREF, MakeParamRef(fn, "a")),
            MakeUnary(NK_DEREF, MakeParamRef(fn, "b")), tname);
        BlockAddStmt(block, MakeReturn(MakeIntLiteral(1)));
        FunctionSetBody(fn, block);
    }
    PublishNode(fn);
}

// @hash struct Foo {...}; publishes
//   size_t Foo_hash(const struct Foo *self);
// an FNV-1a hash of the members @equals compares, so Foo_equals(a, b) implies
// Foo_hash(a) == Foo_hash(b). @hash("P") publishes P_Foo_hash.
@comptime(attribute("hash")) void __builtin_attr_hash(AttrTarget *target, ...) {
    Type       *ty     = __builtin_derive_struct_target(target, "@hash");
    const char *tname  = __builtin_attr_type_name(target, "@hash");
    const char *prefix = __builtin_attr_prefix_arg("@hash");

    char        fname[256];
    __builtin_json_fn_name(fname, prefix, tname, "_hash");

    // size_t is only declared once the program includes a header for it.
    Type *size_ty =
        FindType("size_t") ? GetType("size_t") : GetType("unsigned long");
    Obj *fn = MakeFunction(fname, size_ty);
    FunctionAddParam(fn, "self", MakePointer(MakeConst(ty)));
    WithFn(fn) {
        Type *u64   = GetType("unsigned long long");
        Node *block = MakeBlock(NULL, 0);
        Node *h     = MakeLocalVarUnique(u64);
        BlockAddStmt(
            block,
            MakeExprStmt(MakeAssign(
                h, MakeCast(MakeIntLiteral(-3750763034362895579LL), u64))));
        __builtin_hash_value(
            block, h, ty, MakeUnary(NK_DEREF, MakeParamRef(fn, "self")), tname);
        BlockAddStmt(block, MakeReturn(MakeCast(h, size_ty)));
        FunctionSetBody(fn, block);
    }
    PublishNode(fn);
}

@comptime void __builtin_dump_text(Node *block, Node *out, const char *text) {
    BlockAddStmt(block, MakeExprStmt(__builtin_json_call2(
                            "fputs", MakeStringLiteral(text), out)));
}

@comptime void __builtin_dump_printf1(Node *block, Node *out, const char *fmt,
                                      Node *a) {
    BlockAddStmt(block, MakeExprStmt(__builtin_json_call3(
                            "fprintf", out, MakeStringLiteral(fmt), a)));
}

// "%.<n>s": at most n bytes, stopping at a NUL. Written by hand so the handler
// needs no stdio declarations.
@comptime void __builtin_dump_string_format(char *out, int n) {
    char digits[16];
    int  len = 0;
    do {
        digits[len++]  = (char)('0' + n % 10);
        n             /= 10;
    } while (n > 0);
    int pos    = 0;
    out[pos++] = '%';
    out[pos++] = '.';
    while (len > 0)
        out[pos++] = digits[--len];
    out[pos++] = 's';
    out[pos]   = 0;
}

@comptime void __builtin_dump_value(Node *block, Node *out, Type *ty, Node *e,
                                    const char *tname, const char *path) {
    int k = __builtin_derive_kind(ty);
    if (k == 0)
        __builtin_derive_bad_member("@dump", path);
    if (k == 1 && GetTypeKind(ty) == TK_ENUM) {
        // if (e == A) fputs("A"); else if ... else fprintf("%lld", e);
        // `e` is read by every comparison, so evaluate it once.
        Node *v = MakeLocalVarUnique(GetType("long long"));
        BlockAddStmt(block, MakeExprStmt(MakeAssign(v, e)));
        Node *chain = MakeBlock(NULL, 0);
        __builtin_dump_printf1(chain, out, "%lld", v);
        Node *tail = chain;
        for (int i = EnumValueCount(ty) - 1; i >= 0; i--) {
            Node *hit = MakeBlock(NULL, 0);
            __builtin_dump_text(hit, out, EnumValueName(ty, i));
            tail =
                MakeIf(MakeBinary(NK_EQ, v, MakeIntLiteral(EnumValue(ty, i))),
                       hit, tail);
        }
        BlockAddStmt(block, tail);
    } else if (k == 1) {
        __builtin_dump_printf1(block, out, "%lld",
                               MakeCast(e, GetType("long long")));
    } else if (k == 2) {
        __builtin_dump_printf1(block, out, "%llu",
                               MakeCast(e, GetType("unsigned long long")));
    } else if (k == 3) {
        if (GetTypeKind(ty) == TK_LDOUBLE)
            __builtin_dump_printf1(block, out, "%Lg", e);
        else
            __builtin_dump_printf1(block, out, "%.17g",
                                   MakeCast(e, GetType("double")));
    } else if (k == 4) {
        BlockAddStmt(block, MakeExprStmt(__builtin_json_call2(
                                "fputs",
                                MakeCond(e, MakeStringLiteral("true"),
                                         MakeStringLiteral("false")),
                                out)));
    } else if (k == 8) {
        __builtin_dump_printf1(block, out, "%p",
                               MakeCast(e, MakePointer(GetType("void"))));
    } else if (k == 5) {
        __builtin_dump_text(block, out, "\"");
        char fmt[32];
        __builtin_dump_string_format(fmt, TypeArrayLen(ty));
        __builtin_dump_printf1(block, out, fmt, __builtin_json_charp(e));
        __builtin_dump_text(block, out, "\"");
    } else if (k == 6) {
        char elem[256];
        __builtin_derive_path(elem, path, "[]");
        Node *i     = MakeLocalVarUnique(GetType("int"));
        Node *body  = MakeBlock(NULL, 0);
        Node *comma = MakeBlock(NULL, 0);
        __builtin_dump_text(comma, out, ", ");
        BlockAddStmt(body, MakeIf(i, comma, NULL));
        __builtin_dump_value(body, out, TypeBase(ty), MakeSubscript(e, i), NULL,
                             elem);
        __builtin_dump_text(block, out, "{ ");
        BlockAddStmt(
            block,
            MakeFor(MakeExprStmt(MakeAssign(i, MakeIntLiteral(0))),
                    MakeBinary(NK_LT, i, MakeIntLiteral(TypeArrayLen(ty))),
                    MakeAssign(i, MakeBinary(NK_ADD, i, MakeIntLiteral(1))),
                    body));
        __builtin_dump_text(block, out, " }");
    } else {
        const char *names[128];
        Type       *types[128];
        Node       *exprs[128];
        int         n = 0;
        __builtin_derive_collect(ty, e, "@dump", "dump_skip", names, types,
                                 exprs, &n);
        const char *label = tname ? tname : TypeName(ty);
        if (label && label[0]) {
            __builtin_dump_text(block, out, label);
            __builtin_dump_text(block, out, n ? " { " : " {");
        } else {
            __builtin_dump_text(block, out, n ? "{ " : "{");
        }
        for (int j = 0; j < n; j++) {
            char sub[256];
            __builtin_derive_path(sub, path, ".");
            strcat(sub, names[j]);
            char head[256];
            strcpy(head, j ? ", " : "");
            strcat(head, names[j]);
            strcat(head, " = ");
            __builtin_dump_text(block, out, head);
            __builtin_dump_value(block, out, types[j], exprs[j], NULL, sub);
        }
        __builtin_dump_text(block, out, n ? " }" : "}");
    }
}

// @dump struct Foo {...}; publishes
//   void Foo_dump(const struct Foo *self, FILE *out);
// which prints `Foo { a = 1, name = "x", in = Inner { b = 2 }, v = { 1, 2 } }`
// to out. Enums print by name (the number when no constant matches), pointers
// with %p. Needs #include <stdio.h>. @dump("P") publishes P_Foo_dump.
@comptime(attribute("dump")) void __builtin_attr_dump(AttrTarget *target, ...) {
    Type       *ty     = __builtin_derive_struct_target(target, "@dump");
    const char *tname  = __builtin_attr_type_name(target, "@dump");
    const char *prefix = __builtin_attr_prefix_arg("@dump");
    if (!FindType("FILE") || !FindGlobal("fprintf"))
        MacroErrorAt(MakeIntLiteral(0), "@dump needs #include <stdio.h>");

    char fname[256];
    __builtin_json_fn_name(fname, prefix, tname, "_dump");

    Obj *fn = MakeFunction(fname, GetType("void"));
    FunctionAddParam(fn, "self", MakePointer(MakeConst(ty)));
    FunctionAddParam(fn, "out", MakePointer(GetType("FILE")));
    WithFn(fn) {
        Node *block = MakeBlock(NULL, 0);
        __builtin_dump_value(block, MakeParamRef(fn, "out"), ty,
                             MakeUnary(NK_DEREF, MakeParamRef(fn, "self")),
                             tname, tname);
        FunctionSetBody(fn, block);
    }
    PublishNode(fn);
}

// ---- @owned / @clone / @destroy ---------------------------------------------
// `char *s @owned` is a heap C string, `T *p @owned` a heap T and
// `T *a @owned("n")` a heap array whose length is the integer member n of the
// same struct. @clone deep-copies those members and @destroy frees them;
// members without @owned are copied as they are. A struct pointee that itself
// owns memory is cloned and destroyed through its own T_clone / T_destroy.

@comptime(attribute("owned")) void __builtin_attr_owned(AttrTarget *target,
                                                        ...) {
    if (GetAttrTargetKind(target) != ATTR_TARGET_MEMBER)
        MacroErrorAt(MakeIntLiteral(0), "@owned applies to a struct member");
    if (GetTypeKind(MemberType(AttrTargetMember(target))) != TK_PTR)
        MacroErrorAt(MakeIntLiteral(0), "@owned applies to a pointer member");
    if (VarargCount() > 1)
        MacroErrorAt(MakeIntLiteral(0),
                     "@owned takes at most one argument (the length member)");
    if (VarargCount() == 1) {
        const char *count = StringLiteralValue(VarargAt(0));
        if (!count || !count[0])
            MacroErrorAt(MakeIntLiteral(0),
                         "@owned expects the length member's name as a string "
                         "literal");
    }
}

// Whether `ty` (a struct, or an array of them) has an @owned member.
@comptime int __builtin_owned_has(Type *ty) {
    int tk = GetTypeKind(ty);
    if (tk == TK_ARRAY)
        return __builtin_owned_has(TypeBase(ty));
    if (tk != TK_STRUCT)
        return 0;
    int count = StructMemberCount(ty);
    for (int i = 0; i < count; i++) {
        Member *m = StructMemberAt(ty, i);
        if (MemberHasAttr(m, "owned") || __builtin_owned_has(MemberType(m)))
            return 1;
    }
    return 0;
}

// 1 when the pointee `pt` of an owned member is cloned/destroyed through its
// own function (`out` receives its name), 0 when it is copied as plain bytes.
@comptime int __builtin_owned_pointee_fn(char *out, Type *pt, const char *prefix,
                                         const char *self_name,
                                         const char *suffix, const char *attr,
                                         const char *path) {
    int tk = GetTypeKind(pt);
    if (tk == TK_VOID)
        MacroErrorAt(MakeIntLiteral(0),
                     "%s: owned member '%s' points to void; use a typed "
                     "pointer",
                     attr, path);
    if (tk != TK_STRUCT)
        return 0;
    const char *pname = TypeName(pt);
    bool        self  = pname && !strcmp(pname, self_name);
    if (!self && !__builtin_owned_has(pt))
        return 0;
    if (!pname)
        MacroErrorAt(MakeIntLiteral(0),
                     "%s: owned member '%s' points to an unnamed struct", attr,
                     path);
    __builtin_json_fn_name(out, prefix, pname, suffix);
    if (!self && !FindGlobal(out))
        MacroErrorAt(MakeIntLiteral(0),
                     "%s: owned member '%s' needs %s; put @clone and @destroy "
                     "on '%s', declared before '%s'",
                     attr, path, out, pname, self_name);
    return 1;
}

// The length member `cname` of the struct `ty` that holds an @owned array.
@comptime void __builtin_owned_count_check(Type *ty, const char *cname,
                                           const char *attr, const char *path) {
    int count = StructMemberCount(ty);
    for (int i = 0; i < count; i++) {
        Member     *m     = StructMemberAt(ty, i);
        const char *field = MemberName(m);
        if (!field || strcmp(field, cname))
            continue;
        int tk = GetTypeKind(MemberType(m));
        if (tk != TK_CHAR && tk != TK_SHORT && tk != TK_INT && tk != TK_LONG)
            MacroErrorAt(MakeIntLiteral(0),
                         "%s: length member '%s' of '%s' is not an integer",
                         attr, cname, path);
        return;
    }
    MacroErrorAt(MakeIntLiteral(0), "%s: '%s' has no length member '%s'", attr,
                 path, cname);
}

@comptime void __builtin_owned_require(const char *attr) {
    if (!FindGlobal("malloc"))
        MacroErrorAt(MakeIntLiteral(0), "%s needs #include <stdlib.h>", attr);
}

@comptime Node *__builtin_owned_loop(Node *i, int len, Node *body) {
    return MakeFor(MakeExprStmt(MakeAssign(i, MakeIntLiteral(0))),
                   MakeBinary(NK_LT, i, MakeIntLiteral(len)),
                   MakeAssign(i, MakeBinary(NK_ADD, i, MakeIntLiteral(1))),
                   body);
}

// Frees the @owned members reachable from the struct lvalue `e` and sets them
// to NULL, or only sets them to NULL when `null_only`.
@comptime void __builtin_owned_release(Node *block, Type *ty, Node *e,
                                       bool null_only, const char *prefix,
                                       const char *self_name, const char *attr,
                                       const char *path) {
    int count = StructMemberCount(ty);
    for (int i = 0; i < count; i++) {
        Member     *m     = StructMemberAt(ty, i);
        const char *field = MemberName(m);
        Type       *fty   = MemberType(m);
        if (!field) {
            if (GetTypeKind(fty) == TK_STRUCT)
                __builtin_owned_release(block, fty, e, null_only, prefix,
                                        self_name, attr, path);
            continue;
        }
        char sub[256];
        __builtin_derive_path(sub, path, ".");
        strcat(sub, field);
        if (!MemberHasAttr(m, "owned")) {
            if (!__builtin_owned_has(fty))
                continue;
            Node *member = MakeMember(e, field);
            if (GetTypeKind(fty) == TK_ARRAY) {
                Node *idx  = MakeLocalVarUnique(GetType("int"));
                Node *body = MakeBlock(NULL, 0);
                __builtin_owned_release(body, TypeBase(fty),
                                        MakeSubscript(member, idx), null_only,
                                        prefix, self_name, attr, sub);
                BlockAddStmt(block, __builtin_owned_loop(
                                        idx, TypeArrayLen(fty), body));
            } else {
                __builtin_owned_release(block, fty, member, null_only, prefix,
                                        self_name, attr, sub);
            }
            continue;
        }

        Node *p = MakeMember(e, field);
        if (null_only) {
            BlockAddStmt(block, Quote("$1 = 0;", p));
            continue;
        }
        char fn[256];
        int  deep =
            __builtin_owned_pointee_fn(fn, TypeBase(MemberType(m)), prefix,
                                       self_name, "_destroy", attr, sub);
        char tmpl[512];
        if (MemberAttrArgCount(m, "owned") == 1) {
            const char *cname = StringLiteralValue(MemberAttrArg(m, "owned", 0));
            __builtin_owned_count_check(ty, cname, attr, sub);
            Node *n = MakeMember(e, cname);
            if (deep) {
                strcpy(tmpl, "if ($1) { for (long i_ = 0; i_ < (long)$2; i_++) ");
                strcat(tmpl, fn);
                strcat(tmpl, "(&$1[i_]); free((void *)$1); $1 = 0; }");
                BlockAddStmt(block, Quote(tmpl, p, n));
                continue;
            }
        } else if (deep) {
            strcpy(tmpl, "if ($1) { ");
            strcat(tmpl, fn);
            strcat(tmpl, "($1); free((void *)$1); $1 = 0; }");
            BlockAddStmt(block, Quote(tmpl, p));
            continue;
        }
        BlockAddStmt(block, Quote("free((void *)$1); $1 = 0;", p));
    }
}

// Deep-copies the @owned members of the struct lvalue `src` into `dst`, which
// already holds a shallow copy with those pointers set to NULL. A failed
// allocation sets `fail` and the remaining members are skipped.
@comptime void __builtin_owned_copy(Node *block, Type *ty, Node *dst, Node *src,
                                    Node *fail, const char *prefix,
                                    const char *self_name, const char *attr,
                                    const char *path) {
    int count = StructMemberCount(ty);
    for (int i = 0; i < count; i++) {
        Member     *m     = StructMemberAt(ty, i);
        const char *field = MemberName(m);
        Type       *fty   = MemberType(m);
        if (!field) {
            if (GetTypeKind(fty) == TK_STRUCT)
                __builtin_owned_copy(block, fty, dst, src, fail, prefix,
                                     self_name, attr, path);
            continue;
        }
        char sub[256];
        __builtin_derive_path(sub, path, ".");
        strcat(sub, field);
        if (!MemberHasAttr(m, "owned")) {
            if (!__builtin_owned_has(fty))
                continue;
            Node *d = MakeMember(dst, field);
            Node *s = MakeMember(src, field);
            if (GetTypeKind(fty) == TK_ARRAY) {
                Node *idx  = MakeLocalVarUnique(GetType("int"));
                Node *body = MakeBlock(NULL, 0);
                __builtin_owned_copy(body, TypeBase(fty), MakeSubscript(d, idx),
                                     MakeSubscript(s, idx), fail, prefix,
                                     self_name, attr, sub);
                BlockAddStmt(block, __builtin_owned_loop(
                                        idx, TypeArrayLen(fty), body));
            } else {
                __builtin_owned_copy(block, fty, d, s, fail, prefix, self_name,
                                     attr, sub);
            }
            continue;
        }

        Node *d = MakeMember(dst, field);
        Node *s = MakeMember(src, field);
        char  fn[256];
        char  dfn[256];
        int   deep = __builtin_owned_pointee_fn(fn, TypeBase(fty), prefix,
                                                self_name, "_clone", attr, sub);
        if (deep)
            __builtin_owned_pointee_fn(dfn, TypeBase(fty), prefix, self_name,
                                       "_destroy", attr, sub);
        char tmpl[768];
        if (MemberAttrArgCount(m, "owned") == 1) {
            const char *cname = StringLiteralValue(MemberAttrArg(m, "owned", 0));
            __builtin_owned_count_check(ty, cname, attr, sub);
            Node *n = MakeMember(src, cname);
            strcpy(tmpl,
                   "if (!$3 && $2) { unsigned long n_ = $4 > 0 ? "
                   "(unsigned long)$4 : 0; ");
            if (deep) {
                strcat(tmpl, "$1 = calloc(n_ ? n_ : 1, sizeof(*$2)); "
                             "if (!$1) $3 = 1; else for (unsigned long i_ = 0; "
                             "i_ < n_ && !$3; i_++) if (");
                strcat(tmpl, fn);
                strcat(tmpl, "(&$1[i_], &$2[i_])) $3 = 1; }");
            } else {
                strcat(tmpl, "$1 = malloc((n_ ? n_ : 1) * sizeof(*$2)); "
                             "if (!$1) $3 = 1; else for (unsigned long i_ = 0; "
                             "i_ < n_; i_++) $1[i_] = $2[i_]; }");
            }
            BlockAddStmt(block, Quote(tmpl, d, s, fail, n));
        } else if (deep) {
            strcpy(tmpl, "if (!$3 && $2) { $1 = calloc(1, sizeof(*$2)); "
                         "if (!$1 || ");
            strcat(tmpl, fn);
            strcat(tmpl, "($1, $2)) $3 = 1; }");
            BlockAddStmt(block, Quote(tmpl, d, s, fail));
        } else if (GetTypeKind(TypeBase(fty)) == TK_CHAR) {
            BlockAddStmt(
                block,
                Quote("if (!$3 && $2) { unsigned long n_ = 0; while ($2[n_]) "
                      "n_++; $1 = malloc(n_ + 1); if (!$1) $3 = 1; else for "
                      "(unsigned long i_ = 0; i_ <= n_; i_++) ((char "
                      "*)$1)[i_] = $2[i_]; }",
                      d, s, fail));
        } else {
            BlockAddStmt(block,
                         Quote("if (!$3 && $2) { $1 = malloc(sizeof(*$2)); if "
                               "(!$1) $3 = 1; else *$1 = *$2; }",
                               d, s, fail));
        }
    }
}

// Publishes a prototype for fname unless the program already has one, so the
// generated bodies can call it; MakeFunction then defines it with the
// prototype's parameters.
@comptime void __builtin_owned_proto(const char *fname, Type *ret, Type *first,
                                     const char *first_name, Type *second,
                                     const char *second_name) {
    if (FindGlobal(fname))
        return;
    Obj *proto = FunctionPrototype(fname, ret);
    FunctionAddParam(proto, first_name, first);
    if (second)
        FunctionAddParam(proto, second_name, second);
    PublishNode(proto);
}

// @destroy struct Foo {...}; publishes
//   void Foo_destroy(struct Foo *self);
// which frees the @owned members and sets them to NULL (self itself is not
// freed), so a second call is harmless. @destroy("P") publishes P_Foo_destroy.
@comptime(attribute("destroy")) void __builtin_attr_destroy(AttrTarget *target,
                                                            ...) {
    Type       *ty     = __builtin_derive_struct_target(target, "@destroy");
    const char *tname  = __builtin_attr_type_name(target, "@destroy");
    const char *prefix = __builtin_attr_prefix_arg("@destroy");
    __builtin_owned_require("@destroy");

    char fname[256];
    __builtin_json_fn_name(fname, prefix, tname, "_destroy");
    __builtin_owned_proto(fname, GetType("void"), MakePointer(ty), "self", NULL,
                          NULL);

    Obj *fn = MakeFunction(fname, GetType("void"));
    WithFn(fn) {
        Node *block = MakeBlock(NULL, 0);
        __builtin_owned_release(block, ty,
                                MakeUnary(NK_DEREF, MakeParamRef(fn, "self")),
                                false, prefix, tname, "@destroy", tname);
        FunctionSetBody(fn, block);
    }
    PublishNode(fn);
}

// @clone struct Foo {...}; publishes
//   int Foo_clone(struct Foo *out, const struct Foo *self);
// which copies *self into *out, giving every @owned member its own copy of
// the memory, and returns 0. If an allocation fails it frees what it already
// made, returns 1 and leaves *out untouched. Needs @destroy on the same type.
// @clone("P") publishes P_Foo_clone.
@comptime(attribute("clone")) void __builtin_attr_clone(AttrTarget *target,
                                                        ...) {
    Type       *ty     = __builtin_derive_struct_target(target, "@clone");
    const char *tname  = __builtin_attr_type_name(target, "@clone");
    const char *prefix = __builtin_attr_prefix_arg("@clone");
    __builtin_owned_require("@clone");

    char fname[256];
    char dname[256];
    __builtin_json_fn_name(fname, prefix, tname, "_clone");
    __builtin_json_fn_name(dname, prefix, tname, "_destroy");
    __builtin_owned_proto(dname, GetType("void"), MakePointer(ty), "self", NULL,
                          NULL);
    __builtin_owned_proto(fname, GetType("int"), MakePointer(ty), "out",
                          MakePointer(MakeConst(ty)), "self");

    Obj *fn = MakeFunction(fname, GetType("int"));
    WithFn(fn) {
        Node *block = MakeBlock(NULL, 0);
        Node *r     = MakeLocalVarUnique(ty);
        Node *fail  = MakeLocalVarUnique(GetType("int"));
        Node *self  = MakeUnary(NK_DEREF, MakeParamRef(fn, "self"));
        BlockAddStmt(block, MakeExprStmt(MakeAssign(r, self)));
        BlockAddStmt(block, MakeExprStmt(MakeAssign(fail, MakeIntLiteral(0))));
        __builtin_owned_release(block, ty, r, true, prefix, tname, "@clone",
                                tname);
        __builtin_owned_copy(block, ty, r, self, fail, prefix, tname, "@clone",
                             tname);
        Node *undo = MakeBlock(NULL, 0);
        __builtin_owned_release(undo, ty, r, false, prefix, tname, "@clone",
                                tname);
        BlockAddStmt(undo, MakeReturn(MakeIntLiteral(1)));
        BlockAddStmt(block, MakeIf(fail, undo, NULL));
        BlockAddStmt(block,
                     MakeExprStmt(MakeAssign(
                         MakeUnary(NK_DEREF, MakeParamRef(fn, "out")), r)));
        BlockAddStmt(block, MakeReturn(MakeIntLiteral(0)));
        FunctionSetBody(fn, block);
    }
    PublishNode(fn);
}

// ---- @builder ----------------------------------------------------------------
// @builder struct Foo {...}; publishes a `Foo_builder` type and
//   Foo_builder  Foo_builder_new(void);
//   Foo_builder *Foo_builder_<field>(Foo_builder *b, <FieldType> value);
//   int          Foo_build(const Foo_builder *b, struct Foo *out);
// Foo_builder_new zero-initialises and applies the @builder_default members,
// each setter returns its builder for chaining, and Foo_build returns nonzero
// (leaving *out alone) while a @builder_required member was never set.
// @builder("P") publishes P_Foo_builder, P_Foo_builder_new, ...

@comptime(attribute("builder_default")) void __builtin_attr_builder_default(
    AttrTarget *target, ...) {
    if (GetAttrTargetKind(target) != ATTR_TARGET_MEMBER)
        MacroErrorAt(MakeIntLiteral(0),
                     "@builder_default applies to a struct member");
    if (VarargCount() != 1)
        MacroErrorAt(MakeIntLiteral(0),
                     "@builder_default takes one default value expression");
    Member *m = AttrTargetMember(target);
    if (MemberHasAttr(m, "builder_required"))
        MacroErrorAt(MakeIntLiteral(0),
                     "@builder_default and @builder_required cannot share a "
                     "member");
    if (GetTypeKind(MemberType(m)) == TK_ARRAY)
        MacroErrorAt(MakeIntLiteral(0),
                     "@builder_default does not apply to an array member");
}

@comptime(attribute("builder_required")) void __builtin_attr_builder_required(
    AttrTarget *target, ...) {
    if (GetAttrTargetKind(target) != ATTR_TARGET_MEMBER)
        MacroErrorAt(MakeIntLiteral(0),
                     "@builder_required applies to a struct member");
    if (VarargCount() != 0)
        MacroErrorAt(MakeIntLiteral(0), "@builder_required takes no arguments");
    if (MemberHasAttr(AttrTargetMember(target), "builder_default"))
        MacroErrorAt(MakeIntLiteral(0),
                     "@builder_default and @builder_required cannot share a "
                     "member");
}

// Number of @builder_required members of `ty` (anonymous structs included).
@comptime int __builtin_builder_required_count(Type *ty) {
    int n     = 0;
    int count = StructMemberCount(ty);
    for (int i = 0; i < count; i++) {
        Member *m     = StructMemberAt(ty, i);
        Type   *fty   = MemberType(m);
        if (!MemberName(m)) {
            int tk = GetTypeKind(fty);
            if (tk == TK_STRUCT || tk == TK_UNION)
                n += __builtin_builder_required_count(fty);
        } else if (MemberHasAttr(m, "builder_required")) {
            n++;
        }
    }
    return n;
}

// Publishes the setter of every member of `ty`; `*req` numbers the required
// members.
@comptime void __builtin_builder_setters(Type *ty, Type *bty, const char *bname,
                                         int *req) {
    int count = StructMemberCount(ty);
    for (int i = 0; i < count; i++) {
        Member     *m     = StructMemberAt(ty, i);
        const char *field = MemberName(m);
        Type       *fty   = MemberType(m);
        if (!field) {
            int tk = GetTypeKind(fty);
            if (tk == TK_STRUCT || tk == TK_UNION)
                __builtin_builder_setters(fty, bty, bname, req);
            continue;
        }
        if (!strcmp(field, "new"))
            MacroErrorAt(MakeIntLiteral(0),
                         "@builder: member 'new' clashes with %s_new", bname);
        bool is_array = GetTypeKind(fty) == TK_ARRAY;
        char name[256];
        strcpy(name, bname);
        strcat(name, "_");
        strcat(name, field);
        Obj *fn = MakeFunction(name, MakePointer(bty));
        FunctionAddParam(fn, "b", MakePointer(bty));
        FunctionAddParam(fn, "value",
                         is_array ? MakePointer(MakeConst(TypeBase(fty))) : fty);
        WithFn(fn) {
            Node *b      = MakeUnary(NK_DEREF, MakeParamRef(fn, "b"));
            Node *target = MakeMember(MakeMember(b, "value"), field);
            Node *value  = MakeParamRef(fn, "value");
            Node *block  = MakeBlock(NULL, 0);
            Node *store  = is_array ? Memcpy(target, value,
                                             MakeIntLiteral(TypeSize(fty)))
                                    : MakeAssign(target, value);
            BlockAddStmt(block, MakeExprStmt(store));
            if (MemberHasAttr(m, "builder_required")) {
                BlockAddStmt(
                    block,
                    MakeExprStmt(MakeAssign(
                        MakeSubscript(MakeMember(b, "set"),
                                      MakeIntLiteral((*req)++)),
                        MakeIntLiteral(1))));
            }
            BlockAddStmt(block, MakeReturn(MakeParamRef(fn, "b")));
            FunctionSetBody(fn, block);
        }
        PublishNode(fn);
    }
}

// Assigns the @builder_default members of `ty` in `Foo_builder_new`.
@comptime void __builtin_builder_defaults(Node *block, Type *ty, Node *value) {
    int count = StructMemberCount(ty);
    for (int i = 0; i < count; i++) {
        Member     *m     = StructMemberAt(ty, i);
        const char *field = MemberName(m);
        Type       *fty   = MemberType(m);
        if (!field) {
            int tk = GetTypeKind(fty);
            if (tk == TK_STRUCT || tk == TK_UNION)
                __builtin_builder_defaults(block, fty, value);
            continue;
        }
        if (MemberHasAttr(m, "builder_default"))
            BlockAddStmt(block,
                         MakeExprStmt(MakeAssign(
                             MakeMember(value, field),
                             MemberAttrArg(m, "builder_default", 0))));
    }
}

@comptime(attribute("builder")) void __builtin_attr_builder(AttrTarget *target,
                                                            ...) {
    Type       *ty     = __builtin_derive_struct_target(target, "@builder");
    const char *tname  = __builtin_attr_type_name(target, "@builder");
    const char *prefix = __builtin_attr_prefix_arg("@builder");

    char bname[256];
    __builtin_json_fn_name(bname, prefix, tname, "_builder");
    int required = __builtin_builder_required_count(ty);

    Type *bty = MakeStruct(bname);
    StructAddField(bty, "value", ty);
    StructAddField(bty, "set",
                   MakeArray(GetType("unsigned char"), required ? required : 1));
    PublishNode(bty);
    PublishNode(MakeTypedef(bname, bty));

    char nname[256];
    strcpy(nname, bname);
    strcat(nname, "_new");
    Obj *nf = MakeFunction(nname, bty);
    WithFn(nf) {
        Node *block = MakeBlock(NULL, 0);
        Node *b     = MakeLocalVarUnique(bty);
        BlockAddStmt(
            block, MakeExprStmt(MakeAssign(b, InitStruct(bty, NULL, NULL, 0))));
        __builtin_builder_defaults(block, ty, MakeMember(b, "value"));
        BlockAddStmt(block, MakeReturn(b));
        FunctionSetBody(nf, block);
    }
    PublishNode(nf);

    int req = 0;
    __builtin_builder_setters(ty, bty, bname, &req);

    char fname[256];
    __builtin_json_fn_name(fname, prefix, tname, "_build");
    Obj *bf = MakeFunction(fname, GetType("int"));
    FunctionAddParam(bf, "b", MakePointer(MakeConst(bty)));
    FunctionAddParam(bf, "out", MakePointer(ty));
    WithFn(bf) {
        Node *block = MakeBlock(NULL, 0);
        Node *b     = MakeUnary(NK_DEREF, MakeParamRef(bf, "b"));
        for (int k = 0; k < required; k++)
            BlockAddStmt(
                block,
                MakeIf(MakeUnary(NK_NOT, MakeSubscript(MakeMember(b, "set"),
                                                       MakeIntLiteral(k))),
                       MakeReturn(MakeIntLiteral(1)), NULL));
        BlockAddStmt(
            block, MakeExprStmt(MakeAssign(
                       MakeUnary(NK_DEREF, MakeParamRef(bf, "out")),
                       MakeMember(b, "value"))));
        BlockAddStmt(block, MakeReturn(MakeIntLiteral(0)));
        FunctionSetBody(bf, block);
    }
    PublishNode(bf);
}

// ---- @compare ------------------------------------------------------------------

// if (a < b) return -1; if (b < a) return 1; for every leaf, in declaration
// order. char[N] compares as unsigned bytes up to the first NUL.
@comptime void __builtin_compare_value(Node *block, Type *ty, Node *a, Node *b,
                                       const char *path) {
    int k = __builtin_derive_kind(ty);
    if (k == 0 || k == 8)
        __builtin_derive_bad_member("@compare", path);
    if (k == 5) {
        Type *uc   = GetType("unsigned char");
        Node *i    = MakeLocalVarUnique(GetType("int"));
        Node *done = MakeLocalVarUnique(GetType("int"));
        Node *ca   = MakeLocalVarUnique(uc);
        Node *cb   = MakeLocalVarUnique(uc);
        Node *step = MakeBlock(NULL, 0);
        BlockAddStmt(step, MakeExprStmt(MakeAssign(
                               ca, MakeCast(MakeSubscript(a, i), uc))));
        BlockAddStmt(step, MakeExprStmt(MakeAssign(
                               cb, MakeCast(MakeSubscript(b, i), uc))));
        BlockAddStmt(step, MakeIf(MakeBinary(NK_LT, ca, cb),
                                  MakeReturn(MakeIntLiteral(-1)), NULL));
        BlockAddStmt(step, MakeIf(MakeBinary(NK_LT, cb, ca),
                                  MakeReturn(MakeIntLiteral(1)), NULL));
        BlockAddStmt(step,
                     MakeIf(MakeUnary(NK_NOT, ca),
                            MakeExprStmt(MakeAssign(done, MakeIntLiteral(1))),
                            NULL));
        Node *body = MakeBlock(NULL, 0);
        BlockAddStmt(body, MakeIf(MakeUnary(NK_NOT, done), step, NULL));
        BlockAddStmt(block, MakeExprStmt(MakeAssign(done, MakeIntLiteral(0))));
        BlockAddStmt(
            block,
            MakeFor(MakeExprStmt(MakeAssign(i, MakeIntLiteral(0))),
                    MakeBinary(NK_LT, i, MakeIntLiteral(TypeArrayLen(ty))),
                    MakeAssign(i, MakeBinary(NK_ADD, i, MakeIntLiteral(1))),
                    body));
    } else if (k == 6) {
        char elem[256];
        __builtin_derive_path(elem, path, "[]");
        Node *i    = MakeLocalVarUnique(GetType("int"));
        Node *body = MakeBlock(NULL, 0);
        __builtin_compare_value(body, TypeBase(ty), MakeSubscript(a, i),
                                MakeSubscript(b, i), elem);
        BlockAddStmt(
            block,
            MakeFor(MakeExprStmt(MakeAssign(i, MakeIntLiteral(0))),
                    MakeBinary(NK_LT, i, MakeIntLiteral(TypeArrayLen(ty))),
                    MakeAssign(i, MakeBinary(NK_ADD, i, MakeIntLiteral(1))),
                    body));
    } else if (k == 7) {
        const char *names[128];
        Type       *types[128];
        Node       *ea[128];
        Node       *eb[128];
        int         n = 0;
        int         m = 0;
        __builtin_derive_collect(ty, a, "@compare", "equals_skip", names, types,
                                 ea, &n);
        __builtin_derive_collect(ty, b, "@compare", "equals_skip", names, types,
                                 eb, &m);
        for (int j = 0; j < n; j++) {
            char sub[256];
            __builtin_derive_path(sub, path, ".");
            strcat(sub, names[j]);
            __builtin_compare_value(block, types[j], ea[j], eb[j], sub);
        }
    } else {
        BlockAddStmt(block, MakeIf(MakeBinary(NK_LT, a, b),
                                   MakeReturn(MakeIntLiteral(-1)), NULL));
        BlockAddStmt(block, MakeIf(MakeBinary(NK_LT, b, a),
                                   MakeReturn(MakeIntLiteral(1)), NULL));
    }
}

// @compare struct Foo {...}; publishes
//   int Foo_compare(const struct Foo *a, const struct Foo *b);
// returning <0, 0 or >0 by the first differing member in declaration order
// (nested structs member-wise, char[N] as an unsigned C string, arrays
// element-wise). Members carrying @equals_skip are left out, so
// Foo_compare(a, b) == 0 exactly when Foo_equals(a, b). A pointer or union is a
// compile error. A NaN member compares equal to everything.
// @compare("P") publishes P_Foo_compare.
@comptime(attribute("compare")) void __builtin_attr_compare(AttrTarget *target,
                                                            ...) {
    Type       *ty     = __builtin_derive_struct_target(target, "@compare");
    const char *tname  = __builtin_attr_type_name(target, "@compare");
    const char *prefix = __builtin_attr_prefix_arg("@compare");

    char fname[256];
    __builtin_json_fn_name(fname, prefix, tname, "_compare");

    Obj *fn = MakeFunction(fname, GetType("int"));
    FunctionAddParam(fn, "a", MakePointer(MakeConst(ty)));
    FunctionAddParam(fn, "b", MakePointer(MakeConst(ty)));
    WithFn(fn) {
        Node *block = MakeBlock(NULL, 0);
        __builtin_compare_value(
            block, ty, MakeUnary(NK_DEREF, MakeParamRef(fn, "a")),
            MakeUnary(NK_DEREF, MakeParamRef(fn, "b")), tname);
        BlockAddStmt(block, MakeReturn(MakeIntLiteral(0)));
        FunctionSetBody(fn, block);
    }
    PublishNode(fn);
}

// ---- @bitflags

@comptime void __builtin_bitflags_check(Type *ty) {
    int count = EnumValueCount(ty);
    for (int i = 0; i < count; i++) {
        long long v = EnumValue(ty, i);
        if (v < 0 || (v != 0 && (v & (v - 1)) != 0))
            MacroErrorAt(MakeIntLiteral(0),
                         "@bitflags: constant '%s' (%lld) is not a power of two",
                         EnumValueName(ty, i), v);
    }
}

// `v` as a decimal unsigned long long literal ("4ULL") in out.
@comptime void __builtin_bitflags_ull(char *out, long long v) {
    char digits[24];
    int  len = 0;
    do {
        digits[len++]  = (char)('0' + v % 10);
        v             /= 10;
    } while (v > 0);
    int pos = 0;
    while (len > 0)
        out[pos++] = digits[--len];
    out[pos++] = 'U';
    out[pos++] = 'L';
    out[pos++] = 'L';
    out[pos]   = 0;
}

// A flag helper `T <name>(T v, T f)` returning `T` built from `op(v, f)`.
@comptime void __builtin_bitflags_op(Type *ty, const char *prefix,
                                     const char *tname, const char *suffix,
                                     Type *ret, int op, bool negate_f) {
    char fname[256];
    __builtin_json_fn_name(fname, prefix, tname, suffix);
    Obj *fn = MakeFunction(fname, ret);
    FunctionAddParam(fn, "v", ty);
    FunctionAddParam(fn, "f", ty);
    WithFn(fn) {
        Type *wide = GetType("unsigned long long");
        Node *v    = MakeCast(MakeParamRef(fn, "v"), wide);
        Node *f    = MakeCast(MakeParamRef(fn, "f"), wide);
        if (negate_f)
            f = MakeUnary(NK_BITNOT, f);
        Node *r = MakeBinary(op, v, f);
        FunctionSetBody(fn, MakeReturn(MakeCast(r, ret)));
    }
    PublishNode(fn);
}

// @bitflags enum Perm { R = 1, W = 2, X = 4 }; publishes
//   Perm Perm_set(Perm v, Perm f);      Perm Perm_clear(Perm v, Perm f);
//   Perm Perm_toggle(Perm v, Perm f);   _Bool Perm_has(Perm v, Perm f);
//   size_t Perm_format(Perm v, char *buf, size_t cap);
// Every constant must be 0 or a power of two. Perm_format writes the set flags
// as `R|W` in declaration order (bits with no constant as `0x..`, zero as the
// 0 constant's name or `0`) and returns the length it needs, snprintf style.
// @bitflags("P") publishes P_Perm_set, ...
@comptime(attribute("bitflags")) void __builtin_attr_bitflags(AttrTarget *target,
                                                              ...) {
    Type       *ty     = __builtin_attr_type_target(target, "@bitflags", true);
    const char *tname  = __builtin_attr_type_name(target, "@bitflags");
    const char *prefix = __builtin_attr_prefix_arg("@bitflags");
    __builtin_bitflags_check(ty);

    __builtin_bitflags_op(ty, prefix, tname, "_set", ty, NK_BITOR, false);
    __builtin_bitflags_op(ty, prefix, tname, "_clear", ty, NK_BITAND, true);
    __builtin_bitflags_op(ty, prefix, tname, "_toggle", ty, NK_BITXOR, false);

    char fname[256];
    __builtin_json_fn_name(fname, prefix, tname, "_has");
    Obj *has = MakeFunction(fname, GetType("_Bool"));
    FunctionAddParam(has, "v", ty);
    FunctionAddParam(has, "f", ty);
    WithFn(has) {
        Type *wide = GetType("unsigned long long");
        Node *v    = MakeCast(MakeParamRef(has, "v"), wide);
        Node *f    = MakeCast(MakeParamRef(has, "f"), wide);
        Node *both = MakeBinary(NK_BITAND, v, f);
        FunctionSetBody(
            has, MakeReturn(MakeBinary(NK_EQ, both,
                                       MakeCast(MakeParamRef(has, "f"), wide))));
    }
    PublishNode(has);

    // size_t is only declared once the program includes a header for it.
    Type *size_ty =
        FindType("size_t") ? GetType("size_t") : GetType("unsigned long");
    char src[32768];
    strcpy(src, "unsigned long n = 0; int first = 1;"
                " unsigned long long rem = (unsigned long long)$1;");
    int         count     = EnumValueCount(ty);
    const char *zero_name = "0";
    for (int i = 0; i < count; i++) {
        long long   val  = EnumValue(ty, i);
        const char *name = EnumValueName(ty, i);
        if (val == 0) {
            if (zero_name[0] == '0' && !zero_name[1])
                zero_name = name;
            continue;
        }
        bool seen = false;
        for (int j = 0; j < i; j++)
            if (EnumValue(ty, j) == val)
                seen = true;
        if (seen)
            continue;
        if (strlen(src) + 2 * strlen(name) + 400 > sizeof src - 1024)
            MacroErrorAt(MakeIntLiteral(0), "@bitflags: too many or too long constant names");
        char num[32];
        __builtin_bitflags_ull(num, val);
        strcat(src, " if ((rem & ");
        strcat(src, num);
        strcat(src, ") != 0) { if (!first) { if (n + 1 < $3) $2[n] = '|'; "
                    "n++; } for (unsigned long i = 0; ");
        strcat(src, "\"");
        strcat(src, name);
        strcat(src, "\"[i]; i++) { if (n + 1 < $3) $2[n] = \"");
        strcat(src, name);
        strcat(src, "\"[i]; n++; } first = 0; rem &= ~");
        strcat(src, num);
        strcat(src, "; }");
    }
    strcat(src,
           " if (rem != 0) { if (!first) { if (n + 1 < $3) $2[n] = '|'; n++; }"
           " if (n + 1 < $3) $2[n] = '0'; n++;"
           " if (n + 1 < $3) $2[n] = 'x'; n++;"
           " int started = 0;"
           " for (int sh = 60; sh >= 0; sh -= 4) {"
           " unsigned d = (unsigned)((rem >> sh) & 15);"
           " if (d != 0 || started) { if (n + 1 < $3)"
           " $2[n] = \"0123456789abcdef\"[d]; n++; started = 1; } }"
           " first = 0; }"
           " if (first) { for (unsigned long i = 0; \"");
    strcat(src, zero_name);
    strcat(src, "\"[i]; i++) { if (n + 1 < $3) $2[n] = \"");
    strcat(src, zero_name);
    strcat(src, "\"[i]; n++; } }"
                " if ($3 > 0) $2[n < $3 ? n : $3 - 1] = 0; return n;");

    __builtin_json_fn_name(fname, prefix, tname, "_format");
    Obj *fmt = MakeFunction(fname, size_ty);
    FunctionAddParam(fmt, "v", ty);
    FunctionAddParam(fmt, "buf", MakePointer(GetType("char")));
    FunctionAddParam(fmt, "cap", size_ty);
    WithFn(fmt) {
        FunctionSetBody(fmt, QuoteLazy(src, MakeParamRef(fmt, "v"),
                                  MakeParamRef(fmt, "buf"),
                                  MakeParamRef(fmt, "cap")));
    }
    PublishNode(fmt);
}

#endif // CCCC_DERIVE_H
