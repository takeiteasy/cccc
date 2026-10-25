// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @to_string_skip applies to a struct member

#include <cccc/derive.h>

@to_string_skip struct Point {
    int x;
};

int main(void) {
    return 42;
}
