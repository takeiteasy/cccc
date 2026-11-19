// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cannot be a nested function definition

int main(void) {
    int __attribute__((overloadable)) f(int x) {
        return x;
    }
    return 0;
}
