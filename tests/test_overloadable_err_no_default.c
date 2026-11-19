// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: no matching function for call to 'f'
// CCCC_EXPECT_STDERR: candidate: f\(int a, int b = 3\)

int __attribute__((overloadable)) f(int a, int b @ default(3)) {
    return 1;
}
int __attribute__((overloadable)) f(double a, int b) {
    return 2;
}

int main(void) {
    return f();
}
