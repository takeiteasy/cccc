// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: must take two parameters
typedef struct { int x, y; } V;
[[cccc::operator(*)]]
V v_mul(V a) {
    return a;
}
int main(void) {
    return 0;
}
