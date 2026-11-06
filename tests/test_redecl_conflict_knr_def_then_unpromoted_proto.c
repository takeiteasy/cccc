// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: conflicting types for 'h'

int h(f)
char f;
{
    return 0;
}
int h(char c);

int main(void) {
    return 42;
}
