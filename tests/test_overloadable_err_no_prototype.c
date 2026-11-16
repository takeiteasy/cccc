// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: must have a prototype

int h() __attribute__((overloadable));

int main(void) {
    return 0;
}
