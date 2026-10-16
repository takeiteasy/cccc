// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @clone: length member 'f' of 'S.p' is not an integer

#include <cccc/derive.h>
#include <stdlib.h>

@clone @destroy struct S {
    float f;
    int  *p @owned("f");
};

int main(void) {
    return 42;
}
