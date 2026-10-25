// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: invalid operand to unary !
typedef struct { int x, y; } V;
int main(void) {
    V v = {1, 2};
    return !v;
}
