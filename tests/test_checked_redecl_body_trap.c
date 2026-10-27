// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// An annotated prototype followed by a plain definition bounds the body too,
// by parameter position: the definition's `m` stands for the prototype's `n`.

int sum(int *[[cccc::array, cccc::count(n)]] p, int n);

int sum(int *q, int m) {
    return q[m];
}

int main(void) {
    int a[2] = {1, 2};
    return sum(a, 2);
}
