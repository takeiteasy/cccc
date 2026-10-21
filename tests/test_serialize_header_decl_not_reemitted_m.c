// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int main
// CCCC_REJECT_STDOUT: int printf\(
// -m output never serializes a header-sourced declaration such as printf.
#include <stdio.h>
int main(void) {
    printf("%d\n", 1);
    return 42;
}
