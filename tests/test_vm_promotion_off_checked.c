// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -3 -d
// CCCC_EXPECT_STDOUT: STR_LOCAL_W
// CCCC_REJECT_STDOUT: STR_LOCAL_D +18
// The safety checks observe a local's frame slot, so they keep it in memory.
int sum(void) {
    int s = 0;
    for (int i = 0; i < 100; i++)
        s += i;
    return s;
}

int main(void) {
    return sum() == 4950 ? 42 : 1;
}
