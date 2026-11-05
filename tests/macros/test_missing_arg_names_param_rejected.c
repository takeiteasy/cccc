// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: no argument for parameter 'b'

int f(int a, int b);

int main(void) {
    f(1);
    return 42;
}
