// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @clone: owned member 'S.p' points to void

#include <cccc/derive.h>
#include <stdlib.h>

@clone @destroy struct S {
    void *p @owned;
};

int main(void) {
    return 42;
}
