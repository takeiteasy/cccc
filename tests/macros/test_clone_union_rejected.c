// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @clone does not support unions

#include <cccc/derive.h>
#include <stdlib.h>

@clone union U {
    int a;
    float b;
};

int main(void) {
    return 42;
}
