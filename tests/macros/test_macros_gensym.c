// Test __cccc_gensym and $gensym for collision-safe generated names.

[[cccc::comptime(inline)]]
$node_t *gensym_test(void) {
    const char *a = $gensym("helper");
    const char *b = __cccc_gensym(_VM, "helper");

    int same = 1;
    for (int i = 0; a[i] || b[i]; i++) {
        if (a[i] != b[i]) {
            same = 0;
            break;
        }
    }

    $type_t *int_ty = $get_type("int");
    $obj_t *fn_a = $function(a, int_ty);
    $obj_t *fn_b = $function(b, int_ty);
    $function_set_body(fn_a, $return($int_literal(1)));
    $function_set_body(fn_b, $return($int_literal(2)));

    if (same || !fn_a || !fn_b || a[0] != 'h' || b[0] != 'h')
        return $int_literal(1);
    return $int_literal(42);
}

int main(void) {
    return gensym_test();
}
