// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --no-implicit-derive
// CCCC_EXPECT_STDERR: undefined custom attribute 'equals'

@equals struct Point {
    int x;
};

int main(void) {
    return 42;
}
