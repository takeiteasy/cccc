// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: shares its name with an overloadable function

int __attribute__((overloadable)) f(int a);
@template(T) T f(T a) {
    return a;
}

int main(void) {
    return 0;
}
