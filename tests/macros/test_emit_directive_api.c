// CCCC_FLAGS: -M -G
// CCCC_EXPECT_STDOUT: #ifdef _WIN32.*int macro_body_emit\(void\);.*#endif
[[cccc::comptime]]
void gen(void) {
    $emit_directive("#ifdef _WIN32");
    $obj_t *fn = $function("macro_body_emit", $get_type("int"));
    $function_set_body(fn, $quote("return 42;"));
    $publish(fn);
    $emit_directive("#endif");
}

gen();

int main(void) { return 42; }
