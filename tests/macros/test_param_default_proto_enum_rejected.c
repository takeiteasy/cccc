// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a @default cannot use a name declared in the parameter list

#include <cccc/derive.h>

int f(enum {A, B} v @ default(B));

int main(void) {
    return 42;
}
