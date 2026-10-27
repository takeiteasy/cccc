// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int d\[\];
// CCCC_REJECT_STDOUT: _Checked|count|bounds
//
// A checked flexible array member serializes as an ordinary one.

struct S {
    int n;
    int d _Checked[] [[cccc::count(n)]];
};

struct B {
    int  *end;
    int d _Checked[] [[cccc::bounds(d, end)]];
};

struct U {
    int d _Checked[] [[cccc::bounds(unknown)]];
};

int last(struct S *s) {
    return s->d[s->n - 1];
}

int main(void) {
    return 42;
}
