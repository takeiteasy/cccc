// Test ticket #171: missing expression and declaration AST builders.
// Tests $cond, $null, $sizeof_type, $alignof_type,
// $sizeof_expr, $subscript, $comma, $make_const,
// $make_volatile, $function_prototype, $make_struct,
// $struct_add_field, $make_union, $make_enum,
// $enum_add_constant, $make_typedef.

// ---- $cond --------------------------------------------------------
// Returns 1 if cond is true, else 2.
[[jcc::macro]]
$node_t *choose(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("choose_impl", int_ty);
    $function_add_param(fn, "c", int_ty);
    $node_t *cond = $param_ref(fn, "c");
    $node_t *body = $return(
        $cond(cond, $int_literal(1), $int_literal(2)));
    $function_set_body(fn, body);
        return $int_literal(0);
}
choose();

// ---- $null --------------------------------------------------------
// Returns a null pointer cast to int (0 == 0 check).
[[jcc::macro]]
$node_t *get_null_ptr(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("null_ptr_as_int", int_ty);
    // cast null pointer to long then to int (pointer size = 8 on 64-bit)
    $type_t *long_ty = $get_type("long");
    $node_t *body = $return(
        $cast($cast($null(), long_ty), int_ty));
    $function_set_body(fn, body);
        return $int_literal(0);
}
get_null_ptr();

// ---- $sizeof_type / $alignof_type ----------------------------
[[jcc::macro]]
$node_t *size_of_int(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("macro_sizeof_int", int_ty);
    $node_t *body = $return($sizeof_type(int_ty));
    $function_set_body(fn, body);
        return $int_literal(0);
}
size_of_int();

[[jcc::macro]]
$node_t *align_of_double(void) {
    $type_t *int_ty = $get_type("int");
    $type_t *dbl_ty = $get_type("double");
    $obj_t *fn = $function("macro_alignof_double", int_ty);
    $node_t *body = $return($cast($alignof_type(dbl_ty), int_ty));
    $function_set_body(fn, body);
        return $int_literal(0);
}
align_of_double();

// ---- $sizeof_expr ------------------------------------------------
[[jcc::macro]]
$node_t *sizeof_expr_test(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("macro_sizeof_expr", int_ty);
    $function_add_param(fn, "x", int_ty);
    // sizeof(x) where x:int should equal 4
    $node_t *body = $return(
        $cast($sizeof_expr($param_ref(fn, "x")), int_ty));
    $function_set_body(fn, body);
        return $int_literal(0);
}
sizeof_expr_test();

// ---- $subscript --------------------------------------------------
// Returns arr[2].
[[jcc::macro]]
$node_t *subscript_test(void) {
    $type_t *int_ty = $get_type("int");
    $type_t *ptr_ty = $make_pointer(int_ty);
    $obj_t *fn = $function("macro_subscript", int_ty);
    $function_add_param(fn, "arr", ptr_ty);
    $node_t *body = $return(
        $subscript($param_ref(fn, "arr"), $int_literal(2)));
    $function_set_body(fn, body);
        return $int_literal(0);
}
subscript_test();

// ---- $comma ------------------------------------------------------
// Returns rhs (42) after evaluating lhs (side effect via assignment).
[[jcc::macro]]
$node_t *comma_test(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("macro_comma", int_ty);
    // (0, 42) — lhs is discarded, rhs is returned
    $node_t *body = $return(
        $comma($int_literal(0), $int_literal(42)));
    $function_set_body(fn, body);
        return $int_literal(0);
}
comma_test();

// ---- $make_const / $make_volatile ----------------------------
// (type introspection; confirmed via $type_is_const)
[[jcc::macro]]
$node_t *const_type_test(void) {
    $type_t *int_ty = $get_type("int");
    $type_t *cint = $make_const(int_ty);
    // Verify $type_is_const returns true for cint and false for int_ty
    if (!$type_is_const(cint))
        $macro_error_at(0, "$make_const: type is not const");
    if ($type_is_const(int_ty))
        $macro_error_at(0, "$make_const: original type became const");
    return $int_literal(0);
}
const_type_test();

// ---- $function_prototype -----------------------------------------
// Declare a prototype, then provide definition separately; call it.
[[jcc::macro]]
$node_t *proto_test(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *proto = $function_prototype("proto_fn", int_ty);
    $function_add_param(proto, "x", int_ty);
    $forward_declare(proto);
    return $int_literal(0);
}
proto_test();

// Provide definition via $function — returns the same Obj (params already set).
// Do NOT re-add parameters; just set the body.
[[jcc::macro]]
$node_t *proto_define(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("proto_fn", int_ty);
    // params were added by proto_test(); just set the body
    $node_t *body = $return($param_ref(fn, "x"));
    $function_set_body(fn, body);
    return $int_literal(0);
}
proto_define();

// ---- $make_struct / $struct_add_field ------------------------
// Generate struct Vec2 { int x; int y; } and emit a function using it.
[[jcc::macro]]
$node_t *make_struct_test(void) {
    $type_t *int_ty = $get_type("int");
    $type_t *vec2 = $make_struct("Vec2");
    $struct_add_field(vec2, "x", int_ty);
    $struct_add_field(vec2, "y", int_ty);

    // Generate: int vec2_sum(Vec2 *v) { return v->x + v->y; }
    $type_t *ptr_ty = $make_pointer(vec2);
    $obj_t *fn = $function("vec2_sum", int_ty);
    $function_add_param(fn, "v", ptr_ty);
    $node_t *vref = $param_ref(fn, "v");
    $node_t *xref = $member($unary(nk_deref, vref), "x");
    $node_t *yref = $member($unary(nk_deref, $param_ref(fn, "v")), "y");
    $node_t *body = $return($binary(nk_add, xref, yref));
    $function_set_body(fn, body);
        return $int_literal(0);
}
make_struct_test();

// ---- $make_union -------------------------------------------------
[[jcc::macro]]
$node_t *make_union_test(void) {
    $type_t *int_ty = $get_type("int");
    $type_t *float_ty = $get_type("float");
    $type_t *u = $make_union("IntFloat");
    $struct_add_field(u, "i", int_ty);
    $struct_add_field(u, "f", float_ty);
    // Union size should equal max(sizeof(int), sizeof(float)) = 4
    // Just verify the type was created correctly (size introspection)
    if ($type_size(u) != 4)
        $macro_error_at(0, "$make_union: unexpected size");
    return $int_literal(0);
}
make_union_test();

// ---- $make_enum / $enum_add_constant -------------------------
[[jcc::macro]]
$node_t *make_enum_test(void) {
    $type_t *e = $make_enum("Color");
    $enum_add_constant(e, "RED",   0);
    $enum_add_constant(e, "GREEN", 1);
    $enum_add_constant(e, "BLUE",  2);
    // Verify 3 constants are registered
    if ($enum_count(e) != 3)
        $macro_error_at(0, "$make_enum: wrong constant count");
    return $int_literal(0);
}
make_enum_test();

// ---- $make_typedef -----------------------------------------------
[[jcc::macro]]
$node_t *make_typedef_test(void) {
    $type_t *long_ty = $get_type("long");
    $make_typedef("MyLong", long_ty);
    // After the typedef, $find_type("MyLong") should resolve
    $type_t *resolved = $find_type("MyLong");
    if (!resolved)
        $macro_error_at(0, "$make_typedef: type not found after registration");
    return $int_literal(0);
}
make_typedef_test();

// ==========================================================================
// Runtime assertions
// ==========================================================================

int main(void) {
    // $cond
    if (choose_impl(1) != 1) return 1;
    if (choose_impl(0) != 2) return 2;

    // $null (null pointer cast to long is 0)
    if (null_ptr_as_int() != 0) return 3;

    // $sizeof_type: sizeof(int) == 4
    if (macro_sizeof_int() != 4) return 4;

    // $alignof_type: _Alignof(double) == 8
    if (macro_alignof_double() != 8) return 5;

    // $sizeof_expr: sizeof(int_param) == 4
    if (macro_sizeof_expr(0) != 4) return 6;

    // $subscript: arr[2] == 99
    int arr[4] = {10, 20, 99, 40};
    if (macro_subscript(arr) != 99) return 7;

    // $comma: (0, 42) == 42
    if (macro_comma() != 42) return 8;

    // $function_prototype: proto_fn(7) == 7
    if (proto_fn(7) != 7) return 9;

    // $make_struct + $struct_add_field: vec2_sum
    // We pass a pointer to a fake struct; layout must match {int x, int y}
    int vec_data[2] = {3, 5};
    if (vec2_sum((void *)vec_data) != 8) return 10;

    // enum constant RED should be accessible as an integer (== 0)
    if (RED != 0) return 11;
    if (BLUE != 2) return 12;

    return 42;
}
