// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: SLTI3
// CCCC_EXPECT_STDOUT: ADDI3
// CCCC_REJECT_STDOUT: SLT3
// CCCC_REJECT_STDOUT: LI3 +[0-9]+ 4294967295 4294967295
// A compare against a constant and a discarded increment each cost one
// instruction: no constant is loaded for the compare, and the increment does
// not rebuild its old value with a `+ -1`.
int count(int n) {
    int c = 0;
    for (int i = 0; i < 100; i++)
        c += n;
    return c;
}

int main(void) {
    return count(1) == 100 ? 42 : 1;
}
