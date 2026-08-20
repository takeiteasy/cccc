// Ticket #515: inherit hook on "math" must NOT fire for suite "mathematics".
// Verifies the '/' boundary in suite_matches prevents false prefix matches.
// CCCC_FLAGS: --testing

static int math_count = 0;

[[cccc::test_setup(suite = "math", inherit)]]
void setup_math(void) {
    math_count++;
}

#pragma cccc suite begin "math"

[[cccc::test]]
void test_in_math(void) {
    AssertEq(math_count, 1); // hook fires for exact match
}

#pragma cccc suite end

#pragma cccc suite begin "mathematics"

[[cccc::test]]
void test_in_mathematics(void) {
    AssertEq(math_count, 0); // hook must NOT fire for "mathematics"
}

#pragma cccc suite end
