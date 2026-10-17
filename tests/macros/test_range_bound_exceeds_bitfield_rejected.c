// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @range bounds do not fit the member's type

#include <cccc/derive.h>

struct S {
    unsigned lo : 3 @range(0, 10);
};

int main(void) {
    return 42;
}
