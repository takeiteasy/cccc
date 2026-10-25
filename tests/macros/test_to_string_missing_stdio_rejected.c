// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @to_string needs #include <stdio.h>

#include <cccc/derive.h>

@to_string struct Point {
    int x;
};

int main(void) {
    return 42;
}
