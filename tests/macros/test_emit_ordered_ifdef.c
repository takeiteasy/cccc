// CCCC_FLAGS: -M -G
// CCCC_EXPECT_STDOUT: #ifdef _WIN32.*int generated_answer\(void\);.*#endif
// CCCC_REJECT_STDOUT: #endif.*int generated_answer\(void\);
#include @emit <stddef.h>

[[cccc::comptime]]
void gen(void) {
    $obj_t *fn = $function("generated_answer", $get_type("int"));
    $function_set_body(fn, $quote("return 42;"));
    $publish(fn);
}

#ifdef @emit _WIN32
gen();
#endif @emit

int main(void) { return 42; }
