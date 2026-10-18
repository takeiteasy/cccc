// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @range applies to an integer, enum or bit-field member, or an array of them, not a char array

#include <cccc/derive.h>

struct S {
    char name[8] @range(97, 122);
};

int main(void) {
    return 42;
}
