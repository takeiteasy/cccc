// EXPECT_COMPILE_ERROR
// Test that calling a function marked __attribute__((error("msg"))) is a compile error.

void __chk_fail(void) __attribute__((error("buffer overflow detected")));

int main(void) {
    __chk_fail(); // should trigger compile error: "buffer overflow detected"
    return 0;
}
