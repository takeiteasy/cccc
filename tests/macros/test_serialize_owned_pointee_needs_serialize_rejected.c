// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @serialize: owned member 'Outer.in' needs Inner_serialize

#include <cccc/derive.h>
#include <stdlib.h>
#include <string.h>

struct Inner {
    char *s @owned;
};

@serialize struct Outer {
    struct Inner *in @owned;
};

int main(void) {
    return 42;
}
