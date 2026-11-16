// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cannot have a @default parameter

int __attribute__((overloadable)) f(int a, int b @default(1)) {
    return a + b;
}

int main(void) {
    return 0;
}
