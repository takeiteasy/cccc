// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: address of overloaded function 'f'

int __attribute__((overloadable)) f(int x) {
    return 1;
}
int __attribute__((overloadable)) f(double x) {
    return 2;
}

int main(void) {
    int (*p)(int) = f;
    return p(0);
}
