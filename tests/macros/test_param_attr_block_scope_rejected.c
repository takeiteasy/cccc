// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: custom attributes on parameters are only supported on a function declaration at file scope

#include <cccc/derive.h>

int main(void) {
    int g(int x @ default(1));
    return 42;
}
