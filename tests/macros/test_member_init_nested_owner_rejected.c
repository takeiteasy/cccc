// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: member initializer needs @ctor on struct 'I'

#include <cccc/derive.h>

@ctor struct O {
    struct I {
        int x = 1;
    } in;
};

int main(void) {
    return 42;
}
