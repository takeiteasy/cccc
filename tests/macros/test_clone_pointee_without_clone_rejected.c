// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @clone: owned member 'Outer.in' needs Inner_clone; put @clone and @destroy on 'Inner', declared before 'Outer'

#include <cccc/derive.h>
#include <stdlib.h>

struct Inner {
    char *s @owned;
};

@clone @destroy struct Outer {
    struct Inner *in @owned;
};

int main(void) {
    return 42;
}
