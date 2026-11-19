// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: call to 'f' is ambiguous
// CCCC_EXPECT_STDERR: candidate: f\(int a, int b = 0\)

int __attribute__((overloadable)) f(int a) {
    return 1;
}
int __attribute__((overloadable)) f(int a, int b @ default(0)) {
    return 2;
}

int main(void) {
    return f(1);
}
