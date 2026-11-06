// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: conflicting types for 'h'

int h(char *p);
int h(const char *p);

int main(void) {
    return 42;
}
