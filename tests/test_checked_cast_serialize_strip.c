// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int f\(int \*p\)
// CCCC_REJECT_STDOUT: cccc::
//
// #486: without --checked-pointers, an assume/dynamic-annotated cast never
// desugars (the rewrite is gated on CCCC_CHECKED_BOUNDS -- see cast(),
// src/parse_expr.c) and the six checked-pointer attributes on a parameter
// are already unconditionally stripped from -m/-c=native/-c=generated
// output (#482/#488 ABI transparency). This proves both together: `f`'s
// signature must serialize as plain `int *p`, and no `cccc::` attribute
// (including `assume`/`dynamic` themselves) may survive into the output.

int f(int *[[cccc::array, cccc::count(3)]] p) {
    int *[[cccc::array, cccc::count(3)]] q =
        (int *[[cccc::array, cccc::count(3), cccc::assume]])p;
    return q[0];
}

int main(void) {
    int x[3] = {1, 2, 3};
    return f(x) + 41;
}
