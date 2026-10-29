// CCCC_NATIVE_SKIP: VM-only division trap
// EXPECT_RUNTIME_ERROR
// A division by a constant zero is not folded away: it still traps.
int main(void) {
    int x = 10 / 0;
    return x;
}
