// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: no matching function for call to 'f'

int __attribute__((overloadable)) f(int a) {
    return 1;
}
int __attribute__((overloadable)) f(double x) {
    return 2;
}

int main(void) {
    return f(.zz = 1);
}
