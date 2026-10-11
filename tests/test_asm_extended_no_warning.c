// CCCC_FLAGS: -Wignored-features
// CCCC_REJECT_STDERR: extended asm
//
// Extended asm with no outputs and no labels has nothing to leave stale, so
// it stays quiet.

int main(void) {
    int x = 42;
    __asm__ volatile("" : : "r"(x) : "memory");
    return x;
}
