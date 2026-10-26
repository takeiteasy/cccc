// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int d\[\];
// CCCC_REJECT_STDOUT: _Checked|count
//
// A checked flexible array member serializes as an ordinary one.

struct S {
    int n;
    int d _Checked[] [[cccc::count(n)]];
};

int last(struct S *s) {
    return s->d[s->n - 1];
}

int main(void) {
    return 42;
}
