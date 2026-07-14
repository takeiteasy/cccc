// CCCC_FLAGS: --testing
// Ticket #515: stacked once+inherit hooks at two depths verify the snapshot
// stack model. Hook on "a" (outer) and "a/b" (inner) are both once+inherit.
// In "a/b": restored from inner snap (outer+inner effects).
// After leaving "a/b" back to "a": inner closed; restored from outer snap only.

static int outer_setup = 0;
static int inner_setup = 0;

[[cccc::test_setup(suite = "a", once, inherit)]]
void setup_outer(void) { outer_setup++; }

[[cccc::test_setup(suite = "a/b", once, inherit)]]
void setup_inner(void) { inner_setup++; }

#pragma cccc suite begin "a"

[[cccc::test]]
void test_a_entry(void) {
    AssertEq(outer_setup, 1); // outer opened on first entry to "a"
    AssertEq(inner_setup, 0); // inner not yet opened
}

#pragma cccc suite begin "b"

[[cccc::test]]
void test_a_b_entry(void) {
    AssertEq(outer_setup, 1); // restored from inner snap (includes outer effects)
    AssertEq(inner_setup, 1); // inner opened on entry to "a/b"
}

#pragma cccc suite end  // end b — inner closes here

[[cccc::test]]
void test_a_after_b(void) {
    AssertEq(outer_setup, 1); // outer still open (restored from outer snap)
    AssertEq(inner_setup, 0); // inner closed; its effects not in outer snap
}

#pragma cccc suite end  // end a — outer closes here
