// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @clamp: member 'in' has @range members; put @clamp on its struct, declared before 'in' \(a nested struct is found by its tag\)

#include <cccc/derive.h>

@validate struct Inner {
    int a @range(0, 9);
};

@clamp struct Outer {
    struct Inner in[2];
};

int main(void) {
    return 42;
}
