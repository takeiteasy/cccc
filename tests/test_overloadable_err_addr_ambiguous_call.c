// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: call to 'g' is ambiguous

int __attribute__((overloadable)) f(int x) {
    return 1;
}
int __attribute__((overloadable)) f(double x) {
    return 2;
}
void __attribute__((overloadable)) g(int (*)(int));
void __attribute__((overloadable)) g(int (*)(int), ...);
int main(void) {
    g(f);
    return 0;
}
