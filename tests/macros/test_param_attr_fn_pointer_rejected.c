// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: custom attributes on parameters are only supported on a function declaration at file scope

#include <cccc/derive.h>

int (*fp)(int x @ default(1));

int main(void) {
    return 42;
}
