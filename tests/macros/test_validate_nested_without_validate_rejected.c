// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @validate: member 'in' has @range members; put @validate on its struct, declared before 'in' \(a nested struct is found by its tag\)

#include <cccc/derive.h>

struct Inner {
    int a @range(0, 9);
};

@validate struct Outer {
    struct Inner in;
};

int main(void) {
    return 42;
}
