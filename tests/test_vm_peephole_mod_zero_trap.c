// CCCC_NATIVE_SKIP: VM-only division trap
// EXPECT_RUNTIME_ERROR
// A remainder by a constant zero is not folded away: it still traps.
int main(void) {
    int x = 10 % (3 - 3);
    return x;
}
