// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: positional argument after a keyed argument

int __attribute__((overloadable)) f(int a, int b) {
    return 1;
}
int __attribute__((overloadable)) f(double a, int b) {
    return 2;
}

int main(void) {
    return f(.a = 1, 2);
}
