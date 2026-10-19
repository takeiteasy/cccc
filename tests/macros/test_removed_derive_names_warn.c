// CCCC_EXPECT_STDERR: unknown attribute 'generate_constructor' ignored
// CCCC_EXPECT_STDERR: unknown attribute 'builder' ignored

#include <cccc/derive.h>

@generate_constructor struct A {
    int x;
};

@builder struct B {
    int y;
};

int main(void) {
    return 42;
}
