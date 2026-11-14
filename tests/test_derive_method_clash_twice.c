// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: method 'equals' is already defined for this type
#include <cccc/derive.h>

@equals("A")
@equals("B")
struct S {
    int a;
};

int main(void) {
    return 0;
}
