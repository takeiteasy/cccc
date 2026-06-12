// CCCC_FLAGS: -M -G --emit-only
// CCCC_EXPECT_STDOUT: int get_answer\(void\)
// CCCC_REJECT_STDOUT: #include <stddef.h>
// Test: --emit-only suppresses auto-capture; non-annotated directives don't appear.

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
