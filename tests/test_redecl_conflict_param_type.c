// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: conflicting types for 'g'

int g(int);
int g(double d) {
    return (int)d;
}

int main(void) {
    return 42;
}
