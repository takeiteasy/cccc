// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: sink\(\(int \*\)small, 2\)
// CCCC_REJECT_STDOUT: cccc::
// CCCC_REJECT_STDOUT: __cccc_tmp
//
// #488: ABI transparency for the caller-side rewrite specifically --
// rewrite_checked_call_args() is gated on CCCC_CHECKED_BOUNDS at parse
// time (mirrors #944's own gate), so with no --checked-pointers flag the
// rewrite never runs at all: no hidden __ca temp, no CHKAB snapshot
// temps at the call site, and (as with every other checked-pointer
// mechanism) no [[cccc::...]] attribute survives into -m's emitted C.
// `small` is a plain array local (not a compound-literal initializer,
// which has its own, unrelated __cccc_tmp desugar) so the only possible
// source of an __cccc_tmp here would be this ticket's own rewrite.

void sink(int *[[cccc::array, cccc::count(n)]] p, int n) {
    (void)p;
    (void)n;
}

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small;
    int backing[2] = {1, 2};
    small          = backing;
    sink(small, 2);
    return 42;
}
