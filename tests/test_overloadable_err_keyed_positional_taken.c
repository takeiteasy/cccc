// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: no matching function for call to 'f'

int __attribute__((overloadable)) f(int a, int b @ default(0)) {
    return 1;
}
int __attribute__((overloadable)) f(double a, int b @ default(0)) {
    return 2;
}

int main(void) {
    return f(1, .a = 2);
}
