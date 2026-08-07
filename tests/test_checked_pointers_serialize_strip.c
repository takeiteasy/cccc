// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int f\(int \*p\)
// CCCC_REJECT_STDOUT: cccc::
//
// #924: the missing regression test for the #482/#488 ABI-transparency
// guarantee -- "checked pointer declarations are never emitted in
// -m/-c=native output" was only ever manually verified during the #770
// session, never pinned by a committed test. serialize_type()'s comment
// (src/serialize.c) documents that ty->checked_kind is deliberately never
// serialized; this proves it end to end: the [[cccc::array, cccc::count]]
// attributes on `p` must not survive into -m's emitted C, and the plain
// `int *p` signature must.

int f(int * [[cccc::array, cccc::count(3)]] p) {
    return p[0];
}

int main(void) {
    int x[3] = {1, 2, 3};
    return f(x) + 41;
}
