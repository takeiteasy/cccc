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
    FunctionAddParam(fn, "self", MakePointer(ty));
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

#endif // CCCC_DERIVE_H
