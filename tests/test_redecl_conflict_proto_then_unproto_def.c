// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: conflicting types for 'h'

int h(float f);
int h() {
    return 0;
}

int main(void) {
    return 42;
}
