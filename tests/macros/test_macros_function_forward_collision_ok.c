// Test that generated functions may still promote existing declarations.

int generated_answer(void);

[[cccc::comptime(inline)]]
$node_t *generate_answer(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("generated_answer", int_ty);
    $function_set_body(fn, $return($int_literal(42)));
    return $int_literal(0);
}

int main(void) {
    generate_answer();
    return generated_answer();
}
