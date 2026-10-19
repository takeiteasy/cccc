// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: redefinition of sq
// Two gnu_inline definitions of one function in a translation unit.
extern inline __attribute__((gnu_inline)) int sq(int x) {
    return x;
}

extern inline __attribute__((gnu_inline)) int sq(int x) {
    return x;
}

int main(void) {
    return sq(42);
}
