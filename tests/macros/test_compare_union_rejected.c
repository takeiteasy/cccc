// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @compare does not support unions

#include <cccc/derive.h>

@compare union U {
    int a;
    float b;
};

int main(void) {
    return 42;
}
