// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @clone needs #include <stdlib.h>

#include <cccc/derive.h>

@clone @destroy struct S {
    char *s @owned;
};

int main(void) {
    return 42;
}
