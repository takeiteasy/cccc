// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @hash: member 'S.u' has an unsupported type

#include <cccc/derive.h>

@hash struct S {
    union {
        int   i;
        float f;
    } u;
};

int main(void) {
    return 42;
}
