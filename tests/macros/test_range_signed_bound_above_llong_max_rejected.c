// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @range bounds do not fit the member's type

#include <cccc/derive.h>

struct S {
    long long v @range(0, 18446744073709551615ULL);
};

int main(void) {
    return 42;
}
