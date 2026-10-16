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

#endif // CCCC_DERIVE_H
