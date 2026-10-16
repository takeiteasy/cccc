// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @equals does not support an anonymous union member

#include <cccc/derive.h>

@equals struct S {
    int tag;
    union {
        int   i;
        float f;
    };
};

int main(void) {
    return 42;
}
