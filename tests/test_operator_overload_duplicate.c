// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: already defined for these operand types
typedef struct {
    int x;
} A;

[[cccc::operator(+)]]
A add1(A a, A b) {
    return a;
}

[[cccc::operator(+)]]
A add2(A a, A b) {
    return b;
}

int main(void) {
    return 0;
}
