// Test ticket #229: global macro calls run pre-parse, so generated
// functions are visible everywhere regardless of source order.

[[jcc::comptime]]
void generate_late(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("late_generated", int_ty);
    $function_set_body(fn, $return($int_literal(42)));
}

int main(void) {
    return late_generated();
}

generate_late();
