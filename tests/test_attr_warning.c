// CCCC_FLAGS: --testing
// Test that calling a function marked __attribute__((warning("msg"))) emits a
// compile-time warning but does NOT fail compilation (non-fatal).
// Also verifies that DCE suppression works: calls in dead branches are silent.

void deprecated_api(void) __attribute__((warning("use new_api() instead")));

void deprecated_api(void) {
    // implementation
}

[[cccc::test]]
void test_attr_warning_compiles(void) {
    // Calling deprecated_api() in a live branch should warn but still compile
    // and run fine.
    deprecated_api();
}

[[cccc::test]]
void test_attr_warning_suppressed_in_dead_branch(void) {
    // Call inside a statically-dead branch must not warn.
    if (0) deprecated_api();
}

[[cccc::test]]
void test_attr_warning_suppressed_true_else(void) {
    // Call in the else of an always-true condition must not warn.
    if (1) { /* live */ } else { deprecated_api(); }
}
