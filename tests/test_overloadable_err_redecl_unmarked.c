// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: redeclaration of 'g' must also have the 'overloadable' attribute

int g(int) __attribute__((overloadable));
int g(int x) {
    return x;
}

int main(void) {
    return 0;
}
