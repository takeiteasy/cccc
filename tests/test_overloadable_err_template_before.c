// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: shares its name with a template

@template(T) T f(T a) {
    return a;
}
int __attribute__((overloadable)) f(int a);

int main(void) {
    return 0;
}
