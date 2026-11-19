// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: parameter 'a' is given twice

int __attribute__((overloadable)) f(int a) {
    return 1;
}
int __attribute__((overloadable)) f(double a) {
    return 2;
}

int main(void) {
    return f(.a = 1, .a = 2);
}
