// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @dump needs #include <stdio.h>

#include <cccc/derive.h>

@dump struct Point {
    int x;
};

int main(void) {
    return 42;
}
