#include <glob.h>

[[jcc::comptime]]
int glob_type_size(void) {
    glob_t g;
    return sizeof(g) > 0;
}

[[jcc::comptime]]
void generate_glob_context_result(void) {
    $obj_t *fn = $function("glob_context_result", $get_type("int"));
    $function_set_body(fn, $return($int_literal(glob_type_size() ? 42 : 1)));
}

generate_glob_context_result();

int main(void) {
    return glob_context_result();
}

