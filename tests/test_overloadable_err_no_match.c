// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: no matching function for call to 'f'

struct S {
    int a;
};
int __attribute__((overloadable)) f(int *x) {
    return 1;
}
int __attribute__((overloadable)) f(double x) {
    return 2;
}

int main(void) {
    struct S s;
    return f(s);
}
