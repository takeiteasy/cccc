// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @compare: owned member 'Outer.items' points to an array of a struct that is not declared yet

#include <cccc/derive.h>

struct Later;

@compare struct Outer {
    int           n;
    struct Later *items @owned("n");
};

struct Later {
    int v;
};

int main(void) {
    return 42;
}
