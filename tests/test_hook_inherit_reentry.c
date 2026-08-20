// Ticket #515: once+inherit hook fires exactly once even when the parent suite
// is re-entered after a nested child block (declaration order: a, a/b, a).
// Verifies no-double-fire on re-entry: once_count stays 1 for all three tests.
// CCCC_FLAGS: --testing

static int once_count = 0;

[[cccc::test_setup(suite = "a", once, inherit)]]
void setup_once(void) {
    once_count++;
}

#pragma cccc suite begin "a"

[[cccc::test]]
void test_a_first(void) {
    AssertEq(once_count, 1); // setup fired on first entry to "a"
}

#pragma cccc suite begin "b"

[[cccc::test]]
void test_a_b(void) {
    AssertEq(once_count, 1); // not re-fired on descent into "a/b"
}

#pragma cccc suite end       // end b

[[cccc::test]]
void test_a_reentry(void) {
    AssertEq(once_count, 1); // not re-fired on re-entry to "a" from "a/b"
}

#pragma cccc suite end       // end a
