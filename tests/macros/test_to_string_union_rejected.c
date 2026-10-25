// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @to_string does not support unions

#include <cccc/derive.h>
#include <stdio.h>

@to_string union U {
    int   i;
    float f;
};

int main(void) {
    return 42;
}
