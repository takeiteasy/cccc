// Test ticket #304: file-scope compound literals in AST builders.
// $compound_literal / $init_array / $init_struct called with current_fn == NULL
// (outside any $with_fn block in a non-inline comptime macro) produce a static
// anonymous global var instead of a stack local.

struct FPt { int x; int y; };

// ---- $compound_literal outside $with_fn (current_fn == NULL) ---------------
// The global_pt literal lives in static storage; the function captures it.
[[jcc::comptime]]
$node_t *gen_gvar_cl(void) {
    $type_t *pt_ty = $get_type("FPt");
    $type_t *int_ty = $get_type("int");

    $node_t *gpt = $compound_literal(pt_ty, $int_literal(7), $int_literal(13));

    $obj_t *fn = $function("gvar_cl_x", int_ty);
    $with_fn(fn) {
        $function_set_body(fn, $return($member(gpt, "x")));
    }
    return $int_literal(0);
}
gen_gvar_cl();

// ---- $init_array outside $with_fn ------------------------------------------
[[jcc::comptime]]
$node_t *gen_gvar_arr(void) {
    $type_t *int_ty = $get_type("int");

    $node_t *garr = $init_array(int_ty,
        $int_literal(10), $int_literal(20), $int_literal(30));

    $obj_t *fn = $function("gvar_arr_elem2", int_ty);
    $with_fn(fn) {
        $function_set_body(fn, $return($subscript(garr, $int_literal(2))));
    }
    return $int_literal(0);
}
gen_gvar_arr();

// ---- $init_struct outside $with_fn, partial (y should be zero) --------------
[[jcc::comptime]]
$node_t *gen_gvar_struct(void) {
    $type_t *pt_ty = $get_type("FPt");
    $type_t *int_ty = $get_type("int");

    const char *flds[] = {"x"};
    $node_t *vals[] = {$int_literal(99)};
    $node_t *gs = $init_struct(pt_ty, flds, vals, 1);

    $obj_t *fn_x = $function("gvar_struct_x", int_ty);
    $with_fn(fn_x) {
        $function_set_body(fn_x, $return($member(gs, "x")));
    }

    $obj_t *fn_y = $function("gvar_struct_y", int_ty);
    $with_fn(fn_y) {
        $function_set_body(fn_y, $return($member(gs, "y")));
    }
    return $int_literal(0);
}
gen_gvar_struct();

int main(void) {
    if (gvar_cl_x()      != 7)  return 1;
    if (gvar_arr_elem2() != 30) return 2;
    if (gvar_struct_x()  != 99) return 3;
    if (gvar_struct_y()  != 0)  return 4;
    return 42;
}
