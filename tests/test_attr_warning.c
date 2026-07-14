// CCCC_FLAGS: --testing
// Test that calling a function marked __attribute__((warning("msg"))) emits a
// compile-time warning but does NOT fail compilation (non-fatal).

void deprecated_api(void) __attribute__((warning("use new_api() instead")));

void deprecated_api(void) {
    // implementation
}

[[cccc::test]]
void test_attr_warning_compiles(void) {
    // Calling deprecated_api() should warn but compile and run fine.
    deprecated_api();
}
