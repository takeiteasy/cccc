// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: STR_LOCAL_D +18
// CCCC_REJECT_STDOUT: STR_LOCAL_W
// CCCC_REJECT_STDOUT: LDR_LOCAL_W
// A counter and an accumulator used in a loop live in S registers: the frame
// has only the register save slot, no int slot traffic.
int sum(void) {
    int s = 0;
    for (int i = 0; i < 100; i++)
        s += i;
    return s;
}

int main(void) {
    return sum() == 4950 ? 42 : 1;
}
