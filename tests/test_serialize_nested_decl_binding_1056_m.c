// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int main
// CCCC_REJECT_STDOUT: static int helper\(int x\)
// -m output does not mark the file-scope function static.
int helper(int x) {
    return x + 1;
}
int main(void) {
    int helper(int); // redundant block-scope prototype
    return helper(41);
}
