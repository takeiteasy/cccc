// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @clone: 'S.p' has no length member 'count'

#include <cccc/derive.h>
#include <stdlib.h>

@clone @destroy struct S {
    int *p @owned("count");
};

int main(void) {
    return 42;
}
