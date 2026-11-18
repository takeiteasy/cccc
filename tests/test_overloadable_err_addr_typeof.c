// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: reference to overloaded function 'f' could not be resolved

int __attribute__((overloadable)) f(int x) {
    return 1;
}
int __attribute__((overloadable)) f(double x) {
    return 2;
}

int main(void) {
    __typeof__(f) *p = 0;
    return p != 0;
}
