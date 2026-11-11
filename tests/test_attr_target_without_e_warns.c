// --attr-target only changes -E output; elsewhere it warns.
// CCCC_FLAGS: --attr-target=c23
// CCCC_EXPECT_STDERR: --attr-target only affects -E output
int main(void) {
    return 42;
}
