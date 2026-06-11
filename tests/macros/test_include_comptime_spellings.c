// Test alternate include qualifier spellings for comptime routing.

#include @comptime <glob.h>
#include __attribute__((comptime)) <stddef.h>

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
