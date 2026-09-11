// #1272: a @test function alongside main() with no --testing/--build flag.
// The attribute-position scan auto-injects testing.h (Assert* + the
// __builtin_assert_* runtime) so the file compiles, but the test itself is
// never run -- only main() executes. Also exercised under -c=native by the
// native sub-suite, proving the assert runtime is emitted (inert) and links.

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
