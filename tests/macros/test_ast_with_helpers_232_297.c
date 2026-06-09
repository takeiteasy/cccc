// Tickets #232 and #297: scoped AST builder helpers and generated switch cases.

[[cccc::comptime]]
$node_t *make_scoped_switch(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("scoped_switch", int_ty);
    $function_add_param(fn, "x", int_ty);

    $with_fn(fn) {
        $node_t *sw = $switch($param_ref(fn, "x"));
        $with_switch(sw) {
            $switch_add_case($int_literal(0), $return($int_literal(10)));
            $switch_add_case($int_literal(1), $return($int_literal(20)));
            $switch_set_default($return($int_literal(-1)));
        }
        $function_set_body(fn, sw);
    }

    return $int_literal(0);
}
make_scoped_switch();

[[cccc::comptime]]
$node_t *make_explicit_switch(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("explicit_switch", int_ty);
    $function_add_param(fn, "x", int_ty);

    $with_fn(fn) {
        $node_t *sw = $switch($param_ref(fn, "x"));
        $switch_add_case(sw, $int_literal(0), $return($int_literal(30)));
        $switch_set_default(sw, $return($int_literal(-3)));
        $function_set_body(fn, sw);
    }

    return $int_literal(0);
}
make_explicit_switch();

[[cccc::comptime]]
$node_t *make_scoped_struct(void) {
    $type_t *int_ty = $get_type("int");
    $type_t *point = $make_struct("ScopedPoint");
    $with_struct(point) {
        $struct_add_field("x", int_ty);
        $struct_add_field("y", int_ty);
    }

    $obj_t *fn = $function("scoped_point_sum", int_ty);
    $function_add_param(fn, "p", $make_pointer(point));
    $node_t *p = $param_ref(fn, "p");
    $node_t *x = $member($unary(nk_deref, p), "x");
    $node_t *y = $member($unary(nk_deref, $param_ref(fn, "p")), "y");
    $function_set_body(fn, $return($binary(nk_add, x, y)));

    return $int_literal(0);
}
make_scoped_struct();

[[cccc::comptime]]
$node_t *make_scoped_enum(void) {
    $type_t *tag = $make_enum("ScopedTag");
    $with_enum(tag) {
        $enum_add_constant("SCOPED_A", 4);
        $enum_add_constant("SCOPED_B", 8);
    }
    return $int_literal(0);
}
make_scoped_enum();

[[cccc::comptime]]
$node_t *make_scoped_block(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("scoped_block_value", int_ty);
    $node_t *block = $block(($node_t*[]){0}, 0);

    $with_block(block) {
        $block_add_stmt($return($int_literal(6)));
    }
    $function_set_body(fn, block);

    return $int_literal(0);
}
make_scoped_block();

int main(void) {
    if (scoped_switch(0) != 10) return 1;
    if (scoped_switch(1) != 20) return 2;
    if (scoped_switch(7) != -1) return 3;

    if (explicit_switch(0) != 30) return 4;
    if (explicit_switch(7) != -3) return 5;

    int point[2] = {3, 5};
    if (scoped_point_sum((void *)point) != 8) return 6;

    if (SCOPED_A != 4) return 7;
    if (SCOPED_B != 8) return 8;

    if (scoped_block_value() != 6) return 9;

    return 42;
}
