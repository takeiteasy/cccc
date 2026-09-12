// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #488: caller-side verification for [[cccc::single]] -- its implicit
// range [p, p + sizeof(T)) references no parameter at all, so it needs no
// substitution, but still gets a real caller-side check: `zero` is
// declared count(0) (valid for zero elements), which cannot satisfy
// `sink`'s single-object [p, p+sizeof(int)) requirement.

void sink(int *[[cccc::single]] p);

int main(void) {
    int *[[cccc::array, cccc::count(0)]] zero = (int[1]){9};
    sink(zero);
    return 42;
}

void sink(int *p) {
    (void)p;
}
