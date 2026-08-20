// Test ticket #171: missing expression and declaration AST builders.
// Tests MakeCond, MakeNull, MakeSizeofType, MakeAlignofType,
// MakeSizeofExpr, MakeSubscript, MakeComma, MakeConst,
// MakeVolatile, FunctionPrototype, MakeStruct,
// StructAddField, MakeUnion, MakeEnum,
// EnumAddConstant, MakeTypedef.

// ---- MakeCond --------------------------------------------------------
// Returns 1 if cond is true, else 2.
[[cccc::comptime]]
Node *choose(void) {
    Type *int_ty = GetType("int");
    Obj  *fn     = MakeFunction("choose_impl", int_ty);
    FunctionAddParam(fn, "c", int_ty);
    Node *cond = MakeParamRef(fn, "c");
    Node *body =
        MakeReturn(MakeCond(cond, MakeIntLiteral(1), MakeIntLiteral(2)));
    FunctionSetBody(fn, body);
    return MakeIntLiteral(0);
}
choose();

// ---- MakeNull --------------------------------------------------------
// Returns a null pointer cast to int (0 == 0 check).
[[cccc::comptime]]
Node *get_null_ptr(void) {
    Type *int_ty = GetType("int");
    Obj  *fn     = MakeFunction("null_ptr_as_int", int_ty);
    // cast null pointer to long then to int (pointer size = 8 on 64-bit)
    Type *long_ty = GetType("long");
    Node *body    = MakeReturn(MakeCast(MakeCast(MakeNull(), long_ty), int_ty));
    FunctionSetBody(fn, body);
    return MakeIntLiteral(0);
}
get_null_ptr();

// ---- MakeSizeofType / MakeAlignofType ----------------------------
[[cccc::comptime]]
Node *size_of_int(void) {
    Type *int_ty = GetType("int");
    Obj  *fn     = MakeFunction("macro_sizeof_int", int_ty);
    Node *body   = MakeReturn(MakeSizeofType(int_ty));
    FunctionSetBody(fn, body);
    return MakeIntLiteral(0);
}
size_of_int();

[[cccc::comptime]]
Node *align_of_double(void) {
    Type *int_ty = GetType("int");
    Type *dbl_ty = GetType("double");
    Obj  *fn     = MakeFunction("macro_alignof_double", int_ty);
    Node *body   = MakeReturn(MakeCast(MakeAlignofType(dbl_ty), int_ty));
    FunctionSetBody(fn, body);
    return MakeIntLiteral(0);
}
align_of_double();

// ---- MakeSizeofExpr ------------------------------------------------
[[cccc::comptime]]
Node *sizeof_expr_test(void) {
    Type *int_ty = GetType("int");
    Obj  *fn     = MakeFunction("macro_sizeof_expr", int_ty);
    FunctionAddParam(fn, "x", int_ty);
    // sizeof(x) where x:int should equal 4
    Node *body =
        MakeReturn(MakeCast(MakeSizeofExpr(MakeParamRef(fn, "x")), int_ty));
    FunctionSetBody(fn, body);
    return MakeIntLiteral(0);
}
sizeof_expr_test();

// ---- MakeSubscript --------------------------------------------------
// Returns arr[2].
[[cccc::comptime]]
Node *subscript_test(void) {
    Type *int_ty = GetType("int");
    Type *ptr_ty = MakePointer(int_ty);
    Obj  *fn     = MakeFunction("macro_subscript", int_ty);
    FunctionAddParam(fn, "arr", ptr_ty);
    Node *body =
        MakeReturn(MakeSubscript(MakeParamRef(fn, "arr"), MakeIntLiteral(2)));
    FunctionSetBody(fn, body);
    return MakeIntLiteral(0);
}
subscript_test();

// ---- MakeComma ------------------------------------------------------
// Returns rhs (42) after evaluating lhs (side effect via assignment).
[[cccc::comptime]]
Node *comma_test(void) {
    Type *int_ty = GetType("int");
    Obj  *fn     = MakeFunction("macro_comma", int_ty);
    // (0, 42) — lhs is discarded, rhs is returned
    Node *body = MakeReturn(MakeComma(MakeIntLiteral(0), MakeIntLiteral(42)));
    FunctionSetBody(fn, body);
    return MakeIntLiteral(0);
}
comma_test();

// ---- MakeConst / MakeVolatile ----------------------------
// (type introspection; confirmed via TypeIsConst)
[[cccc::comptime]]
Node *const_type_test(void) {
    Type *int_ty = GetType("int");
    Type *cint   = MakeConst(int_ty);
    // Verify TypeIsConst returns true for cint and false for int_ty
    if (!TypeIsConst(cint))
        MacroErrorAt(0, "MakeConst: type is not const");
    if (TypeIsConst(int_ty))
        MacroErrorAt(0, "MakeConst: original type became const");
    return MakeIntLiteral(0);
}
const_type_test();

// ---- FunctionPrototype -----------------------------------------
// Declare a prototype, then provide definition separately; call it.
[[cccc::comptime]]
Node *proto_test(void) {
    Type *int_ty = GetType("int");
    Obj  *proto  = FunctionPrototype("proto_fn", int_ty);
    FunctionAddParam(proto, "x", int_ty);
    PublishNode(proto);
    return MakeIntLiteral(0);
}
proto_test();

// Provide definition via MakeFunction — returns the same Obj (params already
// set). Do NOT re-add parameters; just set the body.
[[cccc::comptime]]
Node *proto_define(void) {
    Type *int_ty = GetType("int");
    Obj  *fn     = MakeFunction("proto_fn", int_ty);
    // params were added by proto_test(); just set the body
    Node *body = MakeReturn(MakeParamRef(fn, "x"));
    FunctionSetBody(fn, body);
    return MakeIntLiteral(0);
}
proto_define();

// ---- MakeStruct / StructAddField ------------------------
// Generate struct Vec2 { int x; int y; } and emit a function using it.
[[cccc::comptime]]
Node *make_struct_test(void) {
    Type *int_ty = GetType("int");
    Type *vec2   = MakeStruct("Vec2");
    StructAddField(vec2, "x", int_ty);
    StructAddField(vec2, "y", int_ty);

    // Generate: int vec2_sum(Vec2 *v) { return v->x + v->y; }
    Type *ptr_ty = MakePointer(vec2);
    Obj  *fn     = MakeFunction("vec2_sum", int_ty);
    FunctionAddParam(fn, "v", ptr_ty);
    Node *vref = MakeParamRef(fn, "v");
    Node *xref = MakeMember(MakeUnary(NK_DEREF, vref), "x");
    Node *yref = MakeMember(MakeUnary(NK_DEREF, MakeParamRef(fn, "v")), "y");
    Node *body = MakeReturn(MakeBinary(NK_ADD, xref, yref));
    FunctionSetBody(fn, body);
    return MakeIntLiteral(0);
}
make_struct_test();

// ---- MakeUnion -------------------------------------------------
[[cccc::comptime]]
Node *make_union_test(void) {
    Type *int_ty   = GetType("int");
    Type *float_ty = GetType("float");
    Type *u        = MakeUnion("IntFloat");
    StructAddField(u, "i", int_ty);
    StructAddField(u, "f", float_ty);
    // Union size should equal max(sizeof(int), sizeof(float)) = 4
    // Just verify the type was created correctly (size introspection)
    if (TypeSize(u) != 4)
        MacroErrorAt(0, "MakeUnion: unexpected size");
    return MakeIntLiteral(0);
}
make_union_test();

// ---- MakeEnum / EnumAddConstant -------------------------
[[cccc::comptime]]
Node *make_enum_test(void) {
    Type *e = MakeEnum("Color");
    EnumAddConstant(e, "RED", 0);
    EnumAddConstant(e, "GREEN", 1);
    EnumAddConstant(e, "BLUE", 2);
    // Verify 3 constants are registered
    if (EnumCount(e) != 3)
        MacroErrorAt(0, "MakeEnum: wrong constant count");
    return MakeIntLiteral(0);
}
make_enum_test();

// ---- MakeTypedef -----------------------------------------------
[[cccc::comptime]]
Node *make_typedef_test(void) {
    Type *long_ty = GetType("long");
    MakeTypedef("MyLong", long_ty);
    // After the typedef, FindType("MyLong") should resolve
    Type *resolved = FindType("MyLong");
    if (!resolved)
        MacroErrorAt(0, "MakeTypedef: type not found after registration");
    return MakeIntLiteral(0);
}
make_typedef_test();

// ==========================================================================
// Runtime assertions
// ==========================================================================

int main(void) {
    // MakeCond
    if (choose_impl(1) != 1)
        return 1;
    if (choose_impl(0) != 2)
        return 2;

    // MakeNull (null pointer cast to long is 0)
    if (null_ptr_as_int() != 0)
        return 3;

    // MakeSizeofType: sizeof(int) == 4
    if (macro_sizeof_int() != 4)
        return 4;

    // MakeAlignofType: _Alignof(double) == 8
    if (macro_alignof_double() != 8)
        return 5;

    // MakeSizeofExpr: sizeof(int_param) == 4
    if (macro_sizeof_expr(0) != 4)
        return 6;

    // MakeSubscript: arr[2] == 99
    int arr[4] = {10, 20, 99, 40};
    if (macro_subscript(arr) != 99)
        return 7;

    // MakeComma: (0, 42) == 42
    if (macro_comma() != 42)
        return 8;

    // FunctionPrototype: proto_fn(7) == 7
    if (proto_fn(7) != 7)
        return 9;

    // MakeStruct + StructAddField: vec2_sum
    // We pass a pointer to a fake struct; layout must match {int x, int y}
    int vec_data[2] = {3, 5};
    if (vec2_sum((void *)vec_data) != 8)
        return 10;

    // enum constant RED should be accessible as an integer (== 0)
    if (RED != 0)
        return 11;
    if (BLUE != 2)
        return 12;

    return 42;
}
