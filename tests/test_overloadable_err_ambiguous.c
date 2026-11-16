// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: call to 'f' is ambiguous

int __attribute__((overloadable)) f(long x) {
    return 1;
}
int __attribute__((overloadable)) f(double x) {
    return 2;
}

int main(void) {
    return f(1);
}
