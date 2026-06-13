// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: too many arguments
// In C23 (default), int foo() is a prototype accepting no arguments.
// Calling it with arguments must be a hard error.
int add();

int main(void) {
    return add(1, 2);
}
