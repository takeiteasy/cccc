// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: conflicting types for 'g'
void f(void) {
    int g(int);
}
void h(void) {
    int g(double);
}

int main(void) {
    return 42;
}
