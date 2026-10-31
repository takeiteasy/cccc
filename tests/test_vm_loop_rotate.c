// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: : ADDI3 +[0-9]+ 1 0[^:]*: SLT3 +[0-9]+[^:]*: JNZ3
// A for loop tests its condition once before the first iteration and again at
// the bottom, which branches back into the body: one branch per iteration. The
// counter is incremented in place, with no copy between the ADDI3 and the test.
int sum(int n) {
    int s = 0;
    for (int i = 0; i < n; i++)
        s += i;
    return s;
}

int main(void) {
    return sum(10) == 45 ? 42 : 1;
}
