// Verify that --testing can be combined with -c=native (ticket #345).
// Tests run as a pre-pass; when all pass the native artifact is written.
// Plain C comparisons only (not AssertEq) -- the point here is the
// pre-pass/compile gating, and AssertEq is a VM-only builtin with no
// native-serialization lowering, which would fail for an unrelated reason.
// CCCC_FLAGS: --testing -c=native -o /dev/null

[[cccc::test]]
void test_prepass_basic(void) {
    if (1 + 1 != 2)
        __builtin_trap();
}

[[cccc::test(return = 7)]]
int test_prepass_return(void) {
    return 7;
}

// -c=native links a real executable, unlike the removed -c=bytecode (which
// wrote a standalone artifact with no main() requirement) -- give it one.
int main(void) {
    return 0;
}
