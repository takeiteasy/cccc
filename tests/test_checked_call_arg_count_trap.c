// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #488: bounds-safe interfaces -- the headline case. Annotating a
// prototype's pointer parameter with count(n) is the interface; the
// missing half was a CALLER-side check that the actual argument's own
// declared bounds imply the parameter's, evaluated with the actual call
// arguments (rewrite_checked_call_args(), src/parse_checked.c). `small`
// declares count(2); the call claims count(8) via its own second
// argument -- must trap via CHKAB even though `sink` never itself
// dereferences past small's real extent.

void sink(int *[[cccc::array, cccc::count(n)]] p, int n);

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){1, 2};
    sink(small, 8);
    return 42;
}

void sink(int *p, int n) {
    (void)p;
    (void)n;
}
