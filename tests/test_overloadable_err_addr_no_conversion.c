// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: no overloaded function 'f' matches the target type

int __attribute__((overloadable)) f(int x) {
    return 1;
}
int __attribute__((overloadable)) f(double x) {
    return 2;
}

int main(void) {
    int (*p)(long) = f;
    return p(0);
}
