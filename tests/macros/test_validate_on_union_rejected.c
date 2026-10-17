// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @validate does not support unions

#include <cccc/derive.h>

@validate union U {
    int  a @range(0, 1);
    char b;
};

int main(void) {
    return 42;
}
