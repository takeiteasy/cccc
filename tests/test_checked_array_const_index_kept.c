// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: --checked-pointers -d
// CCCC_EXPECT_STDOUT: CHKR[\s\S]*CHKR[\s\S]*CHKR[\s\S]*CHKR
//
// These constant, in-range accesses keep their range check: a pointer base
// can be NULL (a count() pointer, a checked-array parameter, a member reached
// through a pointer), and a non-constant index is not provable.

struct S {
    int m _Checked[8];
};

int by_count(int *[[cccc::array, cccc::count(5)]] p) {
    return p[2];
}

int by_param(int a _Checked[5]) {
    return a[2];
}

int by_member(struct S *sp) {
    return sp->m[3];
}

int by_index(int i) {
    int a _Checked[5] = {1, 2, 3, 4, 5};
    return a[i];
}

int main(void) {
    int      a[5]        = {0};
    int c    _Checked[5] = {0};
    struct S s           = {0};
    return by_count(a) + by_param(c) + by_member(&s) + by_index(1) == 2 ? 42
                                                                        : 1;
}
