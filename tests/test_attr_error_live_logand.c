// EXPECT_COMPILE_ERROR
// Over-suppression guard: a call in the RHS of && with a runtime LHS must
// still trigger __attribute__((error)).  Only a *statically-false* LHS suppresses.

int __chk_fail_i(void) __attribute__((error("buffer overflow detected")));
int __chk_fail_i(void) { return 0; }

int main(void) {
    int n = 1;
    return n && __chk_fail_i();   // runtime LHS → RHS is live → compile error
}
