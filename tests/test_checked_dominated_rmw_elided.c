// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: --checked-pointers -d
// CCCC_EXPECT_STDOUT: (CHKR .*){3}
// CCCC_REJECT_STDOUT: (CHKR .*){4}
//
// The store half of a read-modify-write repeats the address check of its
// load, so each of these accesses is range-checked once.

int add(int *[[cccc::array, cccc::count(n)]] p, int n, int i) {
    p[i] += 3;
    return p[i];
}

int inc(int *[[cccc::array, cccc::count(n)]] p, int n, int i) {
    p[i]++;
    return 0;
}

int mul(int *[[cccc::array, cccc::count(n)]] p, int n, int i) {
    p[i] *= 3;
    return 0;
}

int main(void) {
    int a[4] = {1, 2, 3, 4};
    add(a, 4, 0);
    inc(a, 4, 1);
    mul(a, 4, 2);
    return a[0] == 4 && a[1] == 3 && a[2] == 9 ? 42 : 1;
}
