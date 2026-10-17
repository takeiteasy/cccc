// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cannot take the address of 'setjmp'
#include <setjmp.h>

int main(void) {
    int (*p)(jmp_buf) = (int (*)(jmp_buf))setjmp;
    return p ? 1 : 0;
}
