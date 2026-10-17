// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @dump: 'Outer.vals' has no length member 'count'

#include <cccc/derive.h>
#include <stdio.h>

@dump struct Outer {
    int *vals @owned("count");
};

int main(void) {
    return 42;
}
