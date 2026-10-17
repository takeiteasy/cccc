// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @compare_desc and @equals_skip cannot share a member

#include <cccc/derive.h>

@compare struct Ranked {
    int a @equals_skip @compare_desc;
};

int main(void) {
    return 42;
}
