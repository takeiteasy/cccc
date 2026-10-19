// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: redefinition of sq
// Only a non-inline definition may replace a gnu_inline one.
extern inline __attribute__((gnu_inline)) int sq(int x) {
    return x;
}

inline int sq(int x) {
    return x;
}

int main(void) {
    return sq(42);
}
