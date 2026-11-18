// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: this type cannot be a template argument

int __attribute__((overloadable)) f(int x) {
    return 1;
}
int __attribute__((overloadable)) f(double x) {
    return 2;
}

@template(T)
int id(T v) {
    return 0;
}

int main(void) {
    return id(f);
}
