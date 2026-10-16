// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @owned takes at most one argument \(the length member\)

#include <cccc/derive.h>

struct S {
    int n;
    int *p @owned("n", "n");
};

int main(void) {
    return 42;
}
