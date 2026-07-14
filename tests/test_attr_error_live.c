// EXPECT_COMPILE_ERROR
// Test that __attribute__((error)) fires when the call is inside a live
// (statically-true) branch.  Complements test_attr_error_dce.c which checks
// that dead branches are suppressed.

void __chk_fail(void) __attribute__((error("buffer overflow detected")));

int main(void) {
    if (1) __chk_fail();   // live branch → compile error
    return 0;
}
