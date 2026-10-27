// CCCC_FLAGS: --checked-pointers
// #488: the ABI-transparency guarantee, positively pinned -- an ordinary
// unchecked `int *raw` argument into an annotated (checked) parameter
// gets no caller-side check at all (find_checked_base()/checked_base_is_
// declared() reject a plain unchecked pointer, the "checked-rooted"
// gate in rewrite_checked_call_args()), so an unchecked caller passing a
// short buffer is unaffected -- unchecked-to-unchecked interop is
// unchanged, exactly as it was before #488. `raw` points at a single int
// but the call claims count(100); this must NOT trap. (A pointer taken from
// a plain array carries the array's extent, so it would trap.)

void sink(int *[[cccc::array, cccc::count(n)]] p, int n);

int main(void) {
    int  one = 1;
    int *raw = &one;
    sink(raw, 100);
    return 42;
}

void sink(int *p, int n) {
    (void)p;
    (void)n;
}
