// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @builder: member 'new' clashes with S_builder_new

#include <cccc/derive.h>

@builder struct S {
    int new;
};

int main(void) {
    return 42;
}
