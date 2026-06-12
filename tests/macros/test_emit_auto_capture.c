// CCCC_FLAGS: -M -G
// CCCC_EXPECT_STDOUT: #include <stddef.h>
// Test: directives outside comptime are auto-captured into -G output.

#include <stddef.h>

#pragma cccc comptime begin

[[cccc::comptime]]
void gen(void) {
    $obj_t *fn = $function("get_answer", $get_type("int"));
    $function_set_body(fn, $return($int_literal(42)));
    $publish(fn);
}

gen();

#pragma cccc comptime end

int get_answer(void);
int main(void) { return get_answer(); }
