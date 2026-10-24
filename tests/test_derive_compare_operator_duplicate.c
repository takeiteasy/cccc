// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: operator '<=>' is already defined for these operand types \(by 'A_compare'\)
#include <cccc/derive.h>

@compare struct A {
    int x;
};

[[cccc::operator(<=>)]]
int a_cmp(const struct A *a, const struct A *b) {
    return a->x - b->x;
}

int main(void) {
    return 0;
}
