// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @equals: owned member 'Outer.in' needs Inner_equals; put @equals on 'Inner', declared before 'Outer'

#include <cccc/derive.h>

struct Inner {
    char *s @owned;
};

@equals struct Outer {
    struct Inner *in @owned;
};

int main(void) {
    return 42;
}
