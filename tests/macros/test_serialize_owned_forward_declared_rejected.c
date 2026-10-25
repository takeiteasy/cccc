// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @serialize: owned member 'Outer.in' points to a struct that is not declared yet; declare it first

#include <cccc/derive.h>
#include <stdlib.h>
#include <string.h>

struct Later;

@serialize struct Outer {
    struct Later *in @owned;
};

struct Later {
    int v;
};

int main(void) {
    return 42;
}
