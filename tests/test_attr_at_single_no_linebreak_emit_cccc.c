// CCCC_FLAGS: -E --emit-cccc
// CCCC_EXPECT_STDOUT: int \* \[\[cccc::single\]\] q;
// CCCC_REJECT_STDOUT: ^\[\[cccc::single\]\]
int * @single q;
int main(void) {
    return 42;
}
