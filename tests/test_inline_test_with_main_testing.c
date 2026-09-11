// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDOUT: ok 1 - add_works
// CCCC_NATIVE_SKIP: this file defines its own main(), which --testing=native
// (the tools/tests.py --native round-trip for a --testing test) always
// rejects -- the generated harness supplies its own main() (src/main.c);
// unrelated to #1272, see test_inline_test_with_main.c's own native leg for
// the assert-runtime-links coverage instead.
//
// #1272: same file as test_inline_test_with_main.c, run under --testing:
// the inline @test is discovered and actually executed (TAP "ok 1"), proving
// auto-injection is not merely cosmetic -- the test really runs.

#include <stdio.h>

int add(int a, int b) {
    return a + b;
}

int main(void) {
    printf("%d\n", add(2, 3));
    return 42;
}

@test void add_works(void) {
    AssertEq(add(2, 3), 5);
}
