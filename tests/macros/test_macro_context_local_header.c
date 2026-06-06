#include "macro_context_local.h"

[[jcc::comptime]]
int local_header_type_size(void) {
    macro_context_local_t item;
    item.value = 42;
    return sizeof(item) > 0 && item.value == 42;
}

[[jcc::macro]]
void generate_local_header_context_result(void) {
    $obj_t *fn = $function("local_header_context_result", $get_type("int"));
    $function_set_body(fn, $return($int_literal(local_header_type_size() ? 42 : 1)));
}

generate_local_header_context_result();

int main(void) {
    return local_header_context_result();
}

