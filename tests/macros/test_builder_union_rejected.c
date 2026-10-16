// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @builder does not support unions

#include <cccc/derive.h>

@builder union U {
    int a;
    float b;
};

int main(void) {
    return 42;
}
