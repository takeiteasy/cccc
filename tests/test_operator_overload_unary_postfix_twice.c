// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: expression is not assignable
typedef struct { int x, y; } V;
[[cccc::operator(+)]]
V v_add(V a, int n) {
    return a;
}
[[cccc::operator(-)]]
V v_sub(V a, int n) {
    return a;
}
int main(void) {
    V v = {1, 2};
    (v++)++;
    return 0;
}
