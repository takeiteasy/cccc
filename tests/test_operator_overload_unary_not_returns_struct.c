// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: must return a scalar
typedef struct { int x, y; } V;
[[cccc::operator(!)]]
V v_not(V a) {
    return a;
}
int main(void) {
    return 0;
}
