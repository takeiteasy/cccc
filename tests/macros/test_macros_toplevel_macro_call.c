// Test ticket #122: explicit pragma macro calls at file scope.

[[jcc::macro]]
$node_t *generate_answer(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("generated_answer", int_ty);

    $function_set_body(fn, $return($int_literal(42)));

    return $int_literal(0);
}

generate_answer();

int main(void) {
    return generated_answer();
}
