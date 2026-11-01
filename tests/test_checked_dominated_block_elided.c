// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: --checked-pointers -d
// CCCC_EXPECT_STDOUT: (CHKR .*){1}
// CCCC_REJECT_STDOUT: (CHKR .*){2}
//
// An access whose address and bounds match an earlier statement's check,
// with nothing in between writing what they read, needs no second check.

int f(int *[[cccc::array, cccc::count(n)]] p, int n, int i) {
    int x = p[i];
    p[i]  = x + 1;
    return p[i];
}

int main(void) {
    int a[4] = {0, 0, 5, 0};
    return f(a, 4, 2) == 6 ? 42 : 1;
}
