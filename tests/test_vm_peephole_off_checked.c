// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -3 -d
// CCCC_EXPECT_STDOUT: SLT3
// CCCC_REJECT_STDOUT: SLTI3
// The safety checks observe instructions as emitted, so the clean-up and the
// immediate forms stay off.
int count(int n) {
    int c = 0;
    for (int i = 0; i < 100; i++)
        c += n;
    return c;
}

int main(void) {
    return count(1) == 100 ? 42 : 1;
}
