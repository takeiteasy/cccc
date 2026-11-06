// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: conflicting types for 'h'

int h(f)
float f;
{
    return 0;
}
int h(float f);

int main(void) {
    return 42;
}
