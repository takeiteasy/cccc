// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @equals does not support unions

#include <cccc/derive.h>

@equals union Bits {
    int   i;
    float f;
};

int main(void) {
    return 42;
}
