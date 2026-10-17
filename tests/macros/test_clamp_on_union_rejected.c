// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @clamp does not support unions

#include <cccc/derive.h>

@clamp union U {
    int  a @range(0, 1);
    char b;
};

int main(void) {
    return 42;
}
