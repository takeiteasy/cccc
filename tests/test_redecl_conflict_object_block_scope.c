// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: conflicting types for 'x'

int x;
void f(void) {
    extern double x;
}

int main(void) {
    return 42;
}
