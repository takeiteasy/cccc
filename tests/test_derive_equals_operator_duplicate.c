// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: operator '==' is already defined for these operand types \(by 'A_equals'\)
#include <cccc/derive.h>

@equals struct A {
    int x;
};

[[cccc::operator(==)]]
bool a_eq(const struct A *a, const struct A *b) {
    return a->x == b->x;
}

int main(void) {
    return 0;
}
