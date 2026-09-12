// CCCC_FLAGS: --checked-pointers
// #488: the ABI-transparency guarantee, positively pinned -- an ordinary
// unchecked `int *raw` argument into an annotated (checked) parameter
// gets no caller-side check at all (find_checked_base()/checked_base_is_
// declared() reject a plain unchecked pointer, the "checked-rooted"
// gate in rewrite_checked_call_args()), so an unchecked caller passing a
// short buffer is unaffected -- unchecked-to-unchecked interop is
// unchanged, exactly as it was before #488. `raw` only has 2 elements
// but the call claims count(100); this must NOT trap.

void sink(int *[[cccc::array, cccc::count(n)]] p, int n);

int main(void) {
    int raw[2] = {1, 2};
    sink(raw, 100);
    return 42;
}

void sink(int *p, int n) {
    (void)p;
    (void)n;
}
