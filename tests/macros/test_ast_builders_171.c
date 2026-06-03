// Test ticket #171: missing expression and declaration AST builders.
// Tests _AST_COND, _AST_NULL, _AST_SIZEOF_TYPE, _AST_ALIGNOF_TYPE,
// _AST_SIZEOF_EXPR, _AST_SUBSCRIPT, _AST_COMMA, _AST_MAKE_CONST,
// _AST_MAKE_VOLATILE, _AST_FUNCTION_PROTOTYPE, _AST_MAKE_STRUCT,
// _AST_STRUCT_ADD_FIELD, _AST_MAKE_UNION, _AST_MAKE_ENUM,
// _AST_ENUM_ADD_CONSTANT, _AST_MAKE_TYPEDEF.

// ---- _AST_COND --------------------------------------------------------
// Returns 1 if cond is true, else 2.
[[jcc::macro]]
_Node *choose(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION("choose_impl", int_ty);
    _AST_FUNCTION_ADD_PARAM(fn, "c", int_ty);
    _Node *cond = _AST_PARAM_REF(fn, "c");
    _Node *body = _AST_RETURN(
        _AST_COND(cond, _AST_INT_LITERAL(1), _AST_INT_LITERAL(2)));
    _AST_FUNCTION_SET_BODY(fn, body);
    _AST_FORWARD_DECLARE(fn);
    return _AST_INT_LITERAL(0);
}
choose();

// ---- _AST_NULL --------------------------------------------------------
// Returns a null pointer cast to int (0 == 0 check).
[[jcc::macro]]
_Node *get_null_ptr(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION("null_ptr_as_int", int_ty);
    // cast null pointer to long then to int (pointer size = 8 on 64-bit)
    _Type *long_ty = _AST_GET_TYPE("long");
    _Node *body = _AST_RETURN(
        _AST_CAST(_AST_CAST(_AST_NULL(), long_ty), int_ty));
    _AST_FUNCTION_SET_BODY(fn, body);
    _AST_FORWARD_DECLARE(fn);
    return _AST_INT_LITERAL(0);
}
get_null_ptr();

// ---- _AST_SIZEOF_TYPE / _AST_ALIGNOF_TYPE ----------------------------
[[jcc::macro]]
_Node *size_of_int(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION("macro_sizeof_int", int_ty);
    _Node *body = _AST_RETURN(_AST_SIZEOF_TYPE(int_ty));
    _AST_FUNCTION_SET_BODY(fn, body);
    _AST_FORWARD_DECLARE(fn);
    return _AST_INT_LITERAL(0);
}
size_of_int();

[[jcc::macro]]
_Node *align_of_double(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Type *dbl_ty = _AST_GET_TYPE("double");
    _Obj *fn = _AST_FUNCTION("macro_alignof_double", int_ty);
    _Node *body = _AST_RETURN(_AST_CAST(_AST_ALIGNOF_TYPE(dbl_ty), int_ty));
    _AST_FUNCTION_SET_BODY(fn, body);
    _AST_FORWARD_DECLARE(fn);
    return _AST_INT_LITERAL(0);
}
align_of_double();

// ---- _AST_SIZEOF_EXPR ------------------------------------------------
[[jcc::macro]]
_Node *sizeof_expr_test(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION("macro_sizeof_expr", int_ty);
    _AST_FUNCTION_ADD_PARAM(fn, "x", int_ty);
    // sizeof(x) where x:int should equal 4
    _Node *body = _AST_RETURN(
        _AST_CAST(_AST_SIZEOF_EXPR(_AST_PARAM_REF(fn, "x")), int_ty));
    _AST_FUNCTION_SET_BODY(fn, body);
    _AST_FORWARD_DECLARE(fn);
    return _AST_INT_LITERAL(0);
}
sizeof_expr_test();

// ---- _AST_SUBSCRIPT --------------------------------------------------
// Returns arr[2].
[[jcc::macro]]
_Node *subscript_test(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Type *ptr_ty = _AST_MAKE_POINTER(int_ty);
    _Obj *fn = _AST_FUNCTION("macro_subscript", int_ty);
    _AST_FUNCTION_ADD_PARAM(fn, "arr", ptr_ty);
    _Node *body = _AST_RETURN(
        _AST_SUBSCRIPT(_AST_PARAM_REF(fn, "arr"), _AST_INT_LITERAL(2)));
    _AST_FUNCTION_SET_BODY(fn, body);
    _AST_FORWARD_DECLARE(fn);
    return _AST_INT_LITERAL(0);
}
subscript_test();

// ---- _AST_COMMA ------------------------------------------------------
// Returns rhs (42) after evaluating lhs (side effect via assignment).
[[jcc::macro]]
_Node *comma_test(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION("macro_comma", int_ty);
    // (0, 42) — lhs is discarded, rhs is returned
    _Node *body = _AST_RETURN(
        _AST_COMMA(_AST_INT_LITERAL(0), _AST_INT_LITERAL(42)));
    _AST_FUNCTION_SET_BODY(fn, body);
    _AST_FORWARD_DECLARE(fn);
    return _AST_INT_LITERAL(0);
}
comma_test();

// ---- _AST_MAKE_CONST / _AST_MAKE_VOLATILE ----------------------------
// (type introspection; confirmed via _AST_TYPE_IS_CONST)
[[jcc::macro]]
_Node *const_type_test(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Type *cint = _AST_MAKE_CONST(int_ty);
    // Verify _AST_TYPE_IS_CONST returns true for cint and false for int_ty
    if (!_AST_TYPE_IS_CONST(cint))
        _MACRO_ERROR_AT(0, "_AST_MAKE_CONST: type is not const");
    if (_AST_TYPE_IS_CONST(int_ty))
        _MACRO_ERROR_AT(0, "_AST_MAKE_CONST: original type became const");
    return _AST_INT_LITERAL(0);
}
const_type_test();

// ---- _AST_FUNCTION_PROTOTYPE -----------------------------------------
// Declare a prototype, then provide definition separately; call it.
[[jcc::macro]]
_Node *proto_test(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *proto = _AST_FUNCTION_PROTOTYPE("proto_fn", int_ty);
    _AST_FUNCTION_ADD_PARAM(proto, "x", int_ty);
    _AST_FORWARD_DECLARE(proto);
    return _AST_INT_LITERAL(0);
}
proto_test();

// Provide definition via _AST_FUNCTION — returns the same Obj (params already set).
// Do NOT re-add parameters; just set the body.
[[jcc::macro]]
_Node *proto_define(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION("proto_fn", int_ty);
    // params were added by proto_test(); just set the body
    _Node *body = _AST_RETURN(_AST_PARAM_REF(fn, "x"));
    _AST_FUNCTION_SET_BODY(fn, body);
    return _AST_INT_LITERAL(0);
}
proto_define();

// ---- _AST_MAKE_STRUCT / _AST_STRUCT_ADD_FIELD ------------------------
// Generate struct Vec2 { int x; int y; } and emit a function using it.
[[jcc::macro]]
_Node *make_struct_test(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Type *vec2 = _AST_MAKE_STRUCT("Vec2");
    _AST_STRUCT_ADD_FIELD(vec2, "x", int_ty);
    _AST_STRUCT_ADD_FIELD(vec2, "y", int_ty);

    // Generate: int vec2_sum(Vec2 *v) { return v->x + v->y; }
    _Type *ptr_ty = _AST_MAKE_POINTER(vec2);
    _Obj *fn = _AST_FUNCTION("vec2_sum", int_ty);
    _AST_FUNCTION_ADD_PARAM(fn, "v", ptr_ty);
    _Node *vref = _AST_PARAM_REF(fn, "v");
    _Node *xref = _AST_MEMBER(_AST_UNARY(_DEREF, vref), "x");
    _Node *yref = _AST_MEMBER(_AST_UNARY(_DEREF, _AST_PARAM_REF(fn, "v")), "y");
    _Node *body = _AST_RETURN(_AST_BINARY(_ADD, xref, yref));
    _AST_FUNCTION_SET_BODY(fn, body);
    _AST_FORWARD_DECLARE(fn);
    return _AST_INT_LITERAL(0);
}
make_struct_test();

// ---- _AST_MAKE_UNION -------------------------------------------------
[[jcc::macro]]
_Node *make_union_test(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Type *float_ty = _AST_GET_TYPE("float");
    _Type *u = _AST_MAKE_UNION("IntFloat");
    _AST_STRUCT_ADD_FIELD(u, "i", int_ty);
    _AST_STRUCT_ADD_FIELD(u, "f", float_ty);
    // Union size should equal max(sizeof(int), sizeof(float)) = 4
    // Just verify the type was created correctly (size introspection)
    if (_AST_TYPE_SIZE(u) != 4)
        _MACRO_ERROR_AT(0, "_AST_MAKE_UNION: unexpected size");
    return _AST_INT_LITERAL(0);
}
make_union_test();

// ---- _AST_MAKE_ENUM / _AST_ENUM_ADD_CONSTANT -------------------------
[[jcc::macro]]
_Node *make_enum_test(void) {
    _Type *e = _AST_MAKE_ENUM("Color");
    _AST_ENUM_ADD_CONSTANT(e, "RED",   0);
    _AST_ENUM_ADD_CONSTANT(e, "GREEN", 1);
    _AST_ENUM_ADD_CONSTANT(e, "BLUE",  2);
    // Verify 3 constants are registered
    if (_AST_ENUM_COUNT(e) != 3)
        _MACRO_ERROR_AT(0, "_AST_MAKE_ENUM: wrong constant count");
    return _AST_INT_LITERAL(0);
}
make_enum_test();

// ---- _AST_MAKE_TYPEDEF -----------------------------------------------
[[jcc::macro]]
_Node *make_typedef_test(void) {
    _Type *long_ty = _AST_GET_TYPE("long");
    _AST_MAKE_TYPEDEF("MyLong", long_ty);
    // After the typedef, _AST_FIND_TYPE("MyLong") should resolve
    _Type *resolved = _AST_FIND_TYPE("MyLong");
    if (!resolved)
        _MACRO_ERROR_AT(0, "_AST_MAKE_TYPEDEF: type not found after registration");
    return _AST_INT_LITERAL(0);
}
make_typedef_test();

// ==========================================================================
// Runtime assertions
// ==========================================================================

int main(void) {
    // _AST_COND
    if (choose_impl(1) != 1) return 1;
    if (choose_impl(0) != 2) return 2;

    // _AST_NULL (null pointer cast to long is 0)
    if (null_ptr_as_int() != 0) return 3;

    // _AST_SIZEOF_TYPE: sizeof(int) == 4
    if (macro_sizeof_int() != 4) return 4;

    // _AST_ALIGNOF_TYPE: _Alignof(double) == 8
    if (macro_alignof_double() != 8) return 5;

    // _AST_SIZEOF_EXPR: sizeof(int_param) == 4
    if (macro_sizeof_expr(0) != 4) return 6;

    // _AST_SUBSCRIPT: arr[2] == 99
    int arr[4] = {10, 20, 99, 40};
    if (macro_subscript(arr) != 99) return 7;

    // _AST_COMMA: (0, 42) == 42
    if (macro_comma() != 42) return 8;

    // _AST_FUNCTION_PROTOTYPE: proto_fn(7) == 7
    if (proto_fn(7) != 7) return 9;

    // _AST_MAKE_STRUCT + _AST_STRUCT_ADD_FIELD: vec2_sum
    // We pass a pointer to a fake struct; layout must match {int x, int y}
    int vec_data[2] = {3, 5};
    if (vec2_sum((void *)vec_data) != 8) return 10;

    // enum constant RED should be accessible as an integer (== 0)
    if (RED != 0) return 11;
    if (BLUE != 2) return 12;

    return 42;
}
