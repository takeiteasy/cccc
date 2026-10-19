// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: redefinition of sq
// A gnu_inline definition cannot follow a real one.
int sq(int x) {
    return x;
}

extern inline __attribute__((gnu_inline)) int sq(int x) {
    return x;
}

int main(void) {
    return sq(42);
}
