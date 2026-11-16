// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cannot mangle a parameter type of overloadable 'f'

int __attribute__((overloadable)) f(struct { int a; } *p);

int main(void) {
    return 0;
}
