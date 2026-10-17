// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @compare_order and @equals_skip cannot share a member

#include <cccc/derive.h>

@compare struct Ranked {
    int a @compare_order(1) @equals_skip;
};

int main(void) {
    return 42;
}
