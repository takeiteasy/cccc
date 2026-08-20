// CCCC_FLAGS: -m --checked-pointers
// CCCC_EXPECT_STDERR: warning: -m ignores VM runtime safety/debug options
// \(--checked-pointers\): they are enforced by the CCCC VM only
// CCCC_EXPECT_STDOUT: int f\(int \*p\)
// CCCC_REJECT_STDOUT: cccc::
//
// #924: -m/--dump-expanded used to silently accept --checked-pointers and
// do nothing with it (the flag has no effect on serialized output --
// CHKR enforcement is VM-only). Pins the fix: it now warns (this is a
// genuine no-op, not a conflict, so it warns rather than erroring) and
// still emits the same stripped C as without the flag.

int f(int *[[cccc::array, cccc::count(3)]] p) {
    return p[0];
}

int main(void) {
    int x[3] = {1, 2, 3};
    return f(x) + 41;
}
