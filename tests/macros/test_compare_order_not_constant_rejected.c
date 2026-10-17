// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @compare_order expects one integer constant

#include <cccc/derive.h>

int rank = 1;

@compare struct Ranked {
    int a @compare_order(rank);
};

int main(void) {
    return 42;
}
