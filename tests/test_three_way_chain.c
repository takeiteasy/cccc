// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: <=> does not associate
int main(void) {
    return 1 <=> 2 <=> 3;
}
