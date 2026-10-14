// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: Inner/Inner.h: cannot open file
// CCCC_FLAGS: -F./tests/fixtures/frameworks
// Inner.framework is only visible from inside Outer.framework's own headers.
#include <Inner/Inner.h>

int main(void) {
    return INNER_VALUE;
}
