// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: operator '-' is already defined for these operand types
typedef struct { int x, y; } V;
[[cccc::operator(-)]]
V v_neg(V a) {
    return a;
}
[[cccc::operator(-)]]
V v_neg2(V a) {
    return a;
}
int main(void) {
    return 0;
}
