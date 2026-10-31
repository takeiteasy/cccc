// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: CALL
// CCCC_REJECT_STDOUT: CALL[^:]*: MOV3
// A call reads only its argument and saved registers, so the copy of a call's
// result into a temporary that no later instruction reads is removed.
int part(int lo, int hi) {
    return (lo + hi) / 2;
}

int touch(int x) {
    return x + 1;
}

int run(int lo, int hi) {
    int p = part(lo, hi);
    int a = touch(p);
    return touch(a) + p;
}

int main(void) {
    return run(0, 20) == 22 ? 42 : 1;
}
