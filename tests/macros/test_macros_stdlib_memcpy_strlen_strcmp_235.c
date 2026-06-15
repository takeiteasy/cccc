// Ticket #235: $memcpy/$strlen/$strcmp thin AST wrappers over <string.h>.

[[cccc::comptime]]
void generate_stdlib_wrappers(void) {
    $obj_t *cpy = $function("wrap_memcpy", $make_pointer($get_type("void")));
    $function_add_param(cpy, "dst", $make_pointer($get_type("void")));
    $function_add_param(cpy, "src", $make_pointer($get_type("void")));
    $function_add_param(cpy, "n", $get_type("size_t"));
    $with_fn(cpy) {
        $function_set_body(cpy, $return($memcpy($param_ref(cpy, "dst"),
                                                  $param_ref(cpy, "src"),
                                                  $param_ref(cpy, "n"))));
    }

    $obj_t *len = $function("wrap_strlen", $get_type("size_t"));
    $function_add_param(len, "s", $make_pointer($get_type("char")));
    $with_fn(len) {
        $function_set_body(len, $return($strlen($param_ref(len, "s"))));
    }

    $obj_t *cmp = $function("wrap_strcmp", $get_type("int"));
    $function_add_param(cmp, "a", $make_pointer($get_type("char")));
    $function_add_param(cmp, "b", $make_pointer($get_type("char")));
    $with_fn(cmp) {
        $function_set_body(cmp, $return($strcmp($param_ref(cmp, "a"),
                                                  $param_ref(cmp, "b"))));
    }
}

generate_stdlib_wrappers();

int main(void) {
    char src[6] = "hello";
    char dst[6] = {0};

    wrap_memcpy(dst, src, 6);
    if (wrap_strcmp(dst, "hello") != 0)
        return 1;
    if ((int)wrap_strlen(dst) != 5)
        return 2;

    return 42;
}
