// Test ticket #296: $compound_literal, $init_array, $init_struct AST builders.
// Exercises both inline and non-inline comptime macro paths.

struct CLPoint { int x; int y; };

// ---- $compound_literal (inline) ----------------------------------------
// Inline macro: current_fn is the caller, so local var allocation works directly.
[[cccc::comptime(inline)]]
$node_t *cl_point_x(void) {
    $type_t *pt = $get_type("CLPoint");
    // positional: {10, 20} — x=10, y=20
    $node_t *lit = $compound_literal(pt, $int_literal(10), $int_literal(20));
    return $member(lit, "x");
}

[[cccc::comptime(inline)]]
$node_t *cl_point_y(void) {
    $type_t *pt = $get_type("CLPoint");
    $node_t *lit = $compound_literal(pt, $int_literal(10), $int_literal(20));
    return $member(lit, "y");
}

// ---- $init_array (non-inline, via $with_fn) ----------------------------
[[cccc::comptime]]
$node_t *gen_array_fn(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("array_elem1", int_ty);
    $with_fn(fn) {
        // int[3]{7, 14, 21}[1] == 14
        $node_t *arr = $init_array(int_ty,
            $int_literal(7), $int_literal(14), $int_literal(21));
        $function_set_body(fn, $return($subscript(arr, $int_literal(1))));
    }
    return $int_literal(0);
}
gen_array_fn();

// ---- $init_struct (designated, partial) --------------------------------
// Only .x is set; .y should be zero from the ND_MEMZERO.
[[cccc::comptime(inline)]]
$node_t *is_partial_y(void) {
    $type_t *pt = $get_type("CLPoint");
    $node_t *s = $init_struct(pt,
        (const char *[]){"x"},
        ($node_t *[]){$int_literal(5)},
        1);
    return $member(s, "y");
}

// Both fields designated: use local arrays to avoid preprocessor comma confusion.
[[cccc::comptime(inline)]]
$node_t *is_both_sum(void) {
    $type_t *pt = $get_type("CLPoint");
    const char *fields[] = {"x", "y"};
    $node_t *vals_a[] = {$int_literal(3), $int_literal(4)};
    $node_t *s = $init_struct(pt, fields, vals_a, 2);
    $node_t *xv = $member(s, "x");
    // Fresh instance for y access (each $init_struct call creates its own anon var).
    $node_t *vals_b[] = {$int_literal(3), $int_literal(4)};
    $node_t *s2 = $init_struct(pt, fields, vals_b, 2);
    $node_t *yv = $member(s2, "y");
    return $binary(nk_add, xv, yv);
}

// ==========================================================================
// Runtime assertions
// ==========================================================================

int main(void) {
    // $compound_literal positional: {10, 20}
    if (cl_point_x() != 10) return 1;
    if (cl_point_y() != 20) return 2;

    // $init_array: int[3]{7, 14, 21}[1] == 14
    if (array_elem1() != 14) return 3;

    // $init_struct partial: {.x=5}.y == 0
    if (is_partial_y() != 0) return 4;

    // $init_struct both fields: .x=3, .y=4; sum == 7
    if (is_both_sum() != 7) return 5;

    return 42;
}
