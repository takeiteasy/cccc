// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Suite: malformed statement assumptions are rejected.

#pragma cccc suite begin "attr_assume_errors"

[[cccc::test(error = "undefined variable 'nope'")]]
void test_assume_expression_is_checked(void) {
    [[gnu::assume(nope)]];
}

[[cccc::test(error = "must be on an empty statement")]]
void test_assume_needs_empty_statement(void) {
    int x                = 1;
    [[gnu::assume(x)]] x = 2;
}

[[cccc::test(error = "requires an expression argument")]]
void test_assume_needs_argument(void) {
    [[clang::assume]];
}

[[cccc::test(error = "is cccc's bounds cast")]]
void test_bare_assume_statement_hint(void) {
    int x = 1;
    [[assume(x)]];
}

[[cccc::test(error = "requires an expression argument")]]
void test_vendor_assume_is_not_a_cast(void) {
    int *p                 = 0;
    int *[[gnu::assume]] q = p;
    (void)q;
}

#pragma cccc suite end
