// CCCC_FLAGS: -Wignored-features
// CCCC_EXPECT_STDERR: extended asm is not executed by the VM
//
// The VM skips asm, so an output keeps its previous value; an extended asm
// with outputs says so under -Wignored-features.

int main(void) {
    int y = 42;
    __asm__ volatile("" : "=r"(y));
    return y;
}
