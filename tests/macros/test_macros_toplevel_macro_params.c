// Test ticket #122: file-scope macro call publishes generated parameters.

[[cccc::comptime]]
$node_t *generate_add(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("generated_add", int_ty);

    $function_add_param(fn, "a", int_ty);
    $function_add_param(fn, "b", int_ty);

    $node_t *sum = $binary(nk_add, $param_ref(fn, "a"),
                             $param_ref(fn, "b"));
    $function_set_body(fn, $return(sum));

    return $int_literal(0);
}

generate_add();

int main(void) {
    return generated_add(20, 22);
}
