// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: enum E \{\n +A = 40\n
// CCCC_EXPECT_STDOUT: enum E__cccc_dup[0-9]+ \{\n +A__cccc_dup[0-9]+ = 2\n
// Block-scope enumerators are declared at the top of the function in native
// output, so a second block's same-named enumerator is renamed.
int main(void) {
    int r = 0;
    {
        enum E { A = 40 };
        r += A;
    }
    {
        enum E { A = 2 };
        r += A;
    }
    return r;
}
