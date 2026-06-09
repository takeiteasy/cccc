// EXPECT_COMPILE_ERROR
// Generated functions must not clobber existing definitions.

int existing_function(void) {
    return 1;
}

[[cccc::comptime(inline)]]
$node_t *clobber_existing(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("existing_function", int_ty);
    $function_set_body(fn, $return($int_literal(42)));
    return $int_literal(0);
}

int main(void) {
    return clobber_existing();
}
