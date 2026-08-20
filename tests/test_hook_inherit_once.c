// Ticket #515: once+inherit setup fires exactly once across the subtree.
// Tests in "a" and "a/b" both see the once-counter == 1.
// CCCC_FLAGS: --testing

static int once_count = 0;

[[cccc::test_setup(suite = "a", once, inherit)]]
void setup_once_a(void) {
    once_count++;
}

#pragma cccc suite begin "a"

[[cccc::test]]
void test_in_a_sees_once(void) {
    AssertEq(once_count, 1); // fired exactly once for the whole subtree
}

#pragma cccc suite begin "b"

[[cccc::test]]
void test_in_a_b_still_once(void) {
    AssertEq(once_count, 1); // still 1 — not re-fired for the sub-suite
}

#pragma cccc suite end       // end b

#pragma cccc suite end       // end a
