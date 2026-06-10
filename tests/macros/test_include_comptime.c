// Test #include @comptime: glob.h is visible only during the comptime pass.
// The runtime translation unit never sees glob_t or glob().
// Mirrors test_macro_context_glob.c but uses #include @comptime instead of #include.

#include @comptime <glob.h>

[[cccc::comptime]]
int glob_type_size(void) {
    glob_t g;
    return sizeof(g) > 0;
}

[[cccc::comptime]]
void generate_result(void) {
    $obj_t *fn = $function("result", $get_type("int"));
    $function_set_body(fn, $return($int_literal(glob_type_size() ? 42 : 1)));
}

generate_result();

int main(void) {
    return result();
}
