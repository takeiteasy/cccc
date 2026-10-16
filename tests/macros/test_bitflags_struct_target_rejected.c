// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @bitflags expects an enum target

#include <cccc/derive.h>

@bitflags struct Flags {
    int a;
};

int main(void) {
    return 42;
}
