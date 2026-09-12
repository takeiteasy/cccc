// CCCC_FLAGS: --testing --checked-pointers
// Checked C-style checked ARRAYS (#487): an array declaration whose extent
// becomes part of its checked type, so CHKR checks it the same way it
// checks a checked pointer's count(n) -- no new opcode or codegen, just
// giving `find_checked_base()` an array-typed base to recognise. Positive
// cases live here (single compiling file, --checked-pointers on for the
// whole suite); compile-error cases and the opt-in-by-default proof cannot
// share a compiling file with these and live in standalone
// tests/test_checked_array_*.c files instead.

// ---------------------------------------------------------------------
// Local, global, static-local and member checked arrays.
// ---------------------------------------------------------------------

int g_a _Checked[4] = {10, 20, 30, 40};

[[cccc::test]]
void test_global_checked_array_in_bounds(void) {
    AssertEq(g_a[0], 10);
    AssertEq(g_a[3], 40);
}

[[cccc::test]]
void test_local_checked_array_in_bounds(void) {
    int a _Checked[5];
    for (int i = 0; i < 5; i++)
        a[i] = i * i;
    AssertEq(a[0], 0);
    AssertEq(a[4], 16);
}

[[cccc::test]]
void test_static_local_checked_array_in_bounds(void) {
    static int a _Checked[3];
    a[0] = 1;
    a[1] = 2;
    a[2] = 3;
    AssertEq(a[0] + a[1] + a[2], 6);
}

struct Pair {
    int   n;
    int m _Checked[8];
};

[[cccc::test]]
void test_member_checked_array_in_bounds(void) {
    struct Pair p = {0};
    p.m[0]        = 7;
    p.m[7]        = 9;
    AssertEq(p.m[0] + p.m[7], 16);
}

// ---------------------------------------------------------------------
// Read + write + compound-assign + increment.
// ---------------------------------------------------------------------

[[cccc::test]]
void test_checked_array_compound_assign(void) {
    int a _Checked[4]  = {1, 2, 3, 4};
    a[1]              += 10;
    a[2]++;
    ++a[3];
    AssertEq(a[1], 12);
    AssertEq(a[2], 4);
    AssertEq(a[3], 5);
}

// ---------------------------------------------------------------------
// Parameter adjustment: `int a _Checked[N]` -> checked pointer.
// ---------------------------------------------------------------------

static int checked_array_param_sum(int a _Checked[5]) {
    int sum = 0;
    for (int i = 0; i < 5; i++)
        sum += a[i];
    return sum;
}

[[cccc::test]]
void test_checked_array_param_in_bounds(void) {
    int a _Checked[5] = {1, 2, 3, 4, 5};
    AssertEq(checked_array_param_sum(a), 15);
}

[[cccc::test(exit_code = 255)]]
void test_checked_array_param_oob(void) {
    int a _Checked[5] = {1, 2, 3, 4, 5};
    (void)checked_array_param_sum(a);
    volatile int i = 5;
    int          x = a[i]; // trap: index one past the declared extent
    (void)x;
}

// ---------------------------------------------------------------------
// Decay: an unchecked pointer local snapshotted from a checked array (#919
// bounds propagation) stays checked through it.
// ---------------------------------------------------------------------

[[cccc::test]]
void test_checked_array_decay_propagation_in_bounds(void) {
    int a _Checked[6] = {1, 2, 3, 4, 5, 6};
    int  *q           = a;
    AssertEq(q[0], 1);
    AssertEq(q[5], 6);
}

[[cccc::test(exit_code = 255)]]
void test_checked_array_decay_propagation_oob(void) {
    int a        _Checked[6] = {1, 2, 3, 4, 5, 6};
    int         *q           = a;
    volatile int i           = 6;
    int          x           = q[i]; // trap: propagated snapshot, still checked
    (void)x;
}

// ---------------------------------------------------------------------
// Out-of-bounds traps, one per storage class this suite claims is checked.
// ---------------------------------------------------------------------

[[cccc::test(exit_code = 255)]]
void test_local_checked_array_oob(void) {
    int a        _Checked[5];
    volatile int i = 5;
    a[i]           = 1;
}

[[cccc::test(exit_code = 255)]]
void test_global_checked_array_oob(void) {
    volatile int i = 4;
    int          x = g_a[i];
    (void)x;
}

[[cccc::test(exit_code = 255)]]
void test_static_local_checked_array_oob(void) {
    static int a _Checked[3];
    volatile int i = 3;
    a[i]           = 1;
}

[[cccc::test(exit_code = 255)]]
void test_member_checked_array_oob(void) {
    struct Pair  p = {0};
    volatile int i = 8;
    p.m[i]         = 1;
}

// ---------------------------------------------------------------------
// _Nt_checked -- terminator-slot write.
// ---------------------------------------------------------------------

[[cccc::test]]
void test_nt_checked_array_terminator_null_ok(void) {
    char s       _Nt_checked[6] = {'h', 'e', 'l', 'l', 'o', 0};
    volatile int i              = 5;
    s[i] = 0; // writing null to the terminator slot is fine
    AssertEq(s[5], 0);
}

[[cccc::test(exit_code = 255)]]
void test_nt_checked_array_terminator_nonnull_trap(void) {
    char s       _Nt_checked[6] = {'h', 'e', 'l', 'l', 'o', 0};
    volatile int i              = 5;
    s[i] = 'x'; // trap: non-null write to terminator slot
}
