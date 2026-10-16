// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @serialize_skip cannot apply to a bit-field
#include <cccc/derive.h>

@serialize struct B {
    int                      a;
    @serialize_skip unsigned b : 3;
};

int main(void) {
    return 42;
}
