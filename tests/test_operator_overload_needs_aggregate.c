// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: needs a struct or union operand
[[cccc::operator(+)]]
int add(int a, int b) {
    return a + b;
}

int main(void) {
    return 0;
}
