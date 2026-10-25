// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: must take one parameter
typedef struct { int x, y; } V;
[[cccc::operator(~)]]
V v_compl(V a, V b) {
    return a;
}
int main(void) {
    return 0;
}
