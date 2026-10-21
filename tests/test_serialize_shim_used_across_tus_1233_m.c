// CCCC_FLAGS: -m tests/fixtures/shim_used_1233_a.c tests/fixtures/shim_used_1233_b.c
// CCCC_EXPECT_STDOUT: __cccc_stderr\(void\)
// -m output defines the shim a real call site uses.
int unrelated_1233(int x);
int report_1233(int x);
int main(void) {
    return report_1233(unrelated_1233(41));
}
