// CCCC_FLAGS: --strict-comptime-includes
// EXPECT_COMPILE_ERROR
// Regular #include declarations must NOT be forwarded to comptime when
// --strict-comptime-includes is set. glob_t from <glob.h> should be
// unavailable inside the comptime function.

#include <glob.h>

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
