// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unchecked array local variable
// Only a header array typedef is exempt; an array of jmp_buf is not.

#include <setjmp.h>

[[cccc::checked]]
int f(void) {
    jmp_buf envs[2];
    return setjmp(envs[0]);
}

int main(void) {
    return 42;
}
