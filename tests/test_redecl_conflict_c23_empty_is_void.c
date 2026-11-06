// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c23
// CCCC_EXPECT_STDERR: conflicting types for 'h'

int h();
int h(int a) {
    return a;
}

int main(void) {
    return 42;
}
