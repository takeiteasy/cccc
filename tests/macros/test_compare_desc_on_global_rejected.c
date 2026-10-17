// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @compare_desc applies to a struct member

#include <cccc/derive.h>

int x @compare_desc;

int main(void) {
    return 42;
}
