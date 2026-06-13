// Test ticket #284: variadic global-generation macros receive string tails.

[[cccc::comptime]]
void gen_funcs(...) {
    for (int i = 0; i < $vararg_count(); i = i + 1) {
        const char *name = $vararg_str_at(i);
        $obj_t *fn = $function(name, $get_type("int"));
        $function_set_body(fn, $return($int_literal(42 + i)));
    }
}

[[cccc::comptime]]
void gen_fixed_and_tail(char *first, ...) {
    $obj_t *fixed = $function(first, $get_type("int"));
    $function_set_body(fixed, $return($int_literal($vararg_count())));

    for (int i = 0; i < $vararg_count(); i = i + 1) {
        const char *name = $vararg_str_at(i);
        $obj_t *fn = $function(name, $get_type("int"));
        $function_set_body(fn, $return($int_literal(20 + i)));
    }
}

gen_funcs(alpha, beta, gamma);
gen_fixed_and_tail(delta, epsilon, zeta);

int main(void) {
    if (alpha() != 42) return 1;
    if (beta() != 43) return 2;
    if (gamma() != 44) return 3;
    if (delta() != 2) return 4;
    if (epsilon() != 20) return 5;
    if (zeta() != 21) return 6;
    return 42;
}
