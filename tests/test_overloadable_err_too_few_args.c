// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: no matching function for call to 'f'

int __attribute__((overloadable)) f(int x, int y) {
    return 1;
}
int __attribute__((overloadable)) f(double x, double y) {
    return 2;
}

int main(void) {
    return f(1);
}
