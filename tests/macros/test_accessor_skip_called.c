// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: undeclared function 'A_get_hidden'
#include <cccc/derive.h>

@getters struct A {
    int shown;
    int hidden @accessor_skip;
};

int main(void) {
    struct A a = {1, 2};
    return A_get_hidden(&a);
}
