// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: undefined variable 'PRIV'
// Ticket #283: a #define inside one [[cccc::comptime]] function body must
// not leak into a sibling comptime function body.

[[cccc::comptime]] int fn_a(void) {
#define PRIV 9
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
