// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: must be declared at file scope

int main(void) {
    int f(int) __attribute__((overloadable));
    return 0;
}
