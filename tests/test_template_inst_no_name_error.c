// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @inst needs a template name
int main(void) {
    int x = @inst(int) + 3;
    return x;
}
