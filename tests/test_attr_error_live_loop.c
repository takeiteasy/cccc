// EXPECT_COMPILE_ERROR
// Over-suppression guard: a call inside a live loop body (while(1)) must
// still trigger __attribute__((error)).  Complements test_attr_error_dce.c
// which checks that while(0) bodies are suppressed.

void __chk_fail(void) __attribute__((error("buffer overflow detected")));

int main(void) {
    while (1) __chk_fail();   // live body → compile error
    return 0;
}
