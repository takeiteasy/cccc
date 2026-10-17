// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @range applies to an integer, enum or bit-field member

#include <cccc/derive.h>

struct S {
    int a[2] @range(0, 1);
};

int main(void) {
    return 42;
}
