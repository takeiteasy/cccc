// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @destroy needs #include <stdlib.h>

#include <cccc/derive.h>

@destroy struct S {
    char *s @owned;
};

int main(void) {
    return 42;
}
