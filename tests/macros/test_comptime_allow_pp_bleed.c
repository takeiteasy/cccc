// CCCC_FLAGS: --allow-comptime-pp-bleed
// Ticket #283: --allow-comptime-pp-bleed restores the pre-isolation shared
// macro table across [[cccc::comptime]] function bodies, so a #define inside
// one body remains visible in a sibling body.

[[cccc::comptime]] int fn_a(void) {
#define PRIV 42
    return PRIV;
}

[[cccc::comptime]]
void generate_result(void) {
    $obj_t *fn = $function("result", $get_type("int"));
    $function_set_body(fn, $return($int_literal(PRIV)));
}

generate_result();

int main(void) {
    return result();
}
