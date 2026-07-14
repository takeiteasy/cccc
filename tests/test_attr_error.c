// EXPECT_COMPILE_ERROR
// Test that calling a function marked __attribute__((error("msg"))) is a compile
// error when the call is in a live (reachable) branch.
// DCE suppression is tested separately in test_attr_error_dce.c.

void __chk_fail(void) __attribute__((error("buffer overflow detected")));

int main(void) {
    __chk_fail(); // should trigger compile error: "buffer overflow detected"
    return 0;
}
