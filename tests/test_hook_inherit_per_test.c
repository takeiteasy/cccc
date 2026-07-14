// Ticket #515: per-test hook inheritance to sub-suites.
// An inherit setup on "a" fires for tests in "a/b". A non-inherit setup on "a"
// does NOT fire for tests in "a/b". Counters are checked within each test run
// (the snapshot is restored before each test, so they start at 0 each time).
// CCCC_FLAGS: --testing

static int inherit_count = 0;
static int exact_count   = 0;

[[cccc::test_setup(suite = "a", inherit)]]
void setup_inherit(void) { inherit_count++; }

[[cccc::test_setup(suite = "a")]]
void setup_exact(void) { exact_count++; }

#pragma cccc suite begin "a"

[[cccc::test]]
void test_in_a(void) {
    // Both hooks match suite "a" exactly
    AssertEq(inherit_count, 1);
    AssertEq(exact_count,   1);
}

#pragma cccc suite begin "b"

[[cccc::test]]
void test_in_a_b(void) {
    // inherit hook fires via suite_matches("a/b", "a"); exact hook does not
    AssertEq(inherit_count, 1);
    AssertEq(exact_count,   0);
}

#pragma cccc suite end  // end b

#pragma cccc suite end  // end a
