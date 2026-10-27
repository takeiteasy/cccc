// CCCC_FLAGS: --testing --checked-pointers
// Checked C-style checked ARRAYS (#487): an array declaration whose extent
// becomes part of its checked type, so CHKR checks it the same way it
// checks a checked pointer's count(n) -- no new opcode or codegen, just
// giving `find_checked_base()` an array-typed base to recognise. Positive
// cases live here (single compiling file, --checked-pointers on for the
// whole suite); compile-error cases and the opt-in-by-default proof cannot
// share a compiling file with these and live in standalone
// tests/test_checked_array_*.c files instead.

#include <stdlib.h>

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

// ---------------------------------------------------------------------
// Inferred extent: the initializer sizes the array.
// ---------------------------------------------------------------------

int g_inferred _Checked[] = {5, 6, 7};

[[cccc::test]]
void test_inferred_extent_global(void) {
    AssertEq(g_inferred[2], 7);
    AssertEq(sizeof(g_inferred) / sizeof(g_inferred[0]), 3);
}

[[cccc::test]]
void test_inferred_extent_local(void) {
    int a _Checked[] = {1, 2, 3, 4};
    int *q           = a;
    AssertEq(a[3], 4);
    AssertEq(q[0], 1);
}

char g_nt_inferred _Nt_checked[] = "hi";

[[cccc::test]]
void test_nt_checked_inferred_extent(void) {
    char s _Nt_checked[] = "hello";
    AssertEq(s[4], 'o');
    AssertEq(sizeof(s), 6);
    AssertEq(g_nt_inferred[1], 'i');
    AssertEq(sizeof(g_nt_inferred), 3);
}

// ---------------------------------------------------------------------
// Multidimensional: every dimension is checked.
// ---------------------------------------------------------------------

int g_grid _Checked[3][5];

struct Grid {
    int cells _Checked[3][5];
};

static int grid_param_at(int a _Checked[3][5], int i, int j) {
    return a[i][j];
}

[[cccc::test]]
void test_multidim_in_bounds(void) {
    int a _Checked[3][5];
    a[2][4] = 42;
    a[0][0] = 1;
    AssertEq(a[2][4], 42);
    AssertEq(a[0][0], 1);
}

[[cccc::test]]
void test_multidim_three_dimensions_in_bounds(void) {
    int a _Checked[2][3][4];
    a[1][2][3] = 7;
    AssertEq(a[1][2][3], 7);
}

[[cccc::test]]
void test_multidim_inferred_outer_extent(void) {
    int a _Checked[][5] = {{1}, {2}, {3}};
    AssertEq(a[2][0], 3);
    AssertEq(sizeof(a) / sizeof(a[0]), 3);
}

[[cccc::test]]
void test_multidim_global_member_param_in_bounds(void) {
    struct Grid s = {0};
    g_grid[1][4]  = 5;
    s.cells[2][4] = 6;
    AssertEq(g_grid[1][4], 5);
    AssertEq(s.cells[2][4], 6);
    AssertEq(grid_param_at(g_grid, 1, 4), 5);
}

[[cccc::test]]
void test_multidim_row_pointer_compiles(void) {
    int a _Checked[3][5] = {{0}};
    int (*p)[5]          = a;
    p[1][4]              = 9;
    AssertEq(a[1][4], 9);
}

[[cccc::test]]
void test_multidim_row_decay_in_bounds(void) {
    int a _Checked[3][5] = {{0}};
    int *r               = a[1];
    r[4]                 = 8;
    AssertEq(a[1][4], 8);
}

[[cccc::test(exit_code = 255)]]
void test_multidim_inner_oob(void) {
    int a _Checked[3][5];
    volatile int j = 5;
    a[1][j]        = 1;
}

[[cccc::test(exit_code = 255)]]
void test_multidim_inner_oob_within_object(void) {
    int a _Checked[3][5];
    volatile int j = 5;
    a[0][j]        = 1; // lands in row 1 -- inside the object, outside the row
}

[[cccc::test(exit_code = 255)]]
void test_multidim_outer_oob(void) {
    int a _Checked[3][5];
    volatile int i = 3;
    a[i][0]        = 1;
}

[[cccc::test(exit_code = 255)]]
void test_multidim_three_dimensions_middle_oob(void) {
    int a _Checked[2][3][4];
    volatile int j = 3;
    a[1][j][0]     = 1;
}

[[cccc::test(exit_code = 255)]]
void test_multidim_row_decay_oob(void) {
    int a _Checked[3][5];
    int *r         = a[1];
    volatile int j = 5;
    r[j]           = 1;
}

[[cccc::test(exit_code = 255)]]
void test_multidim_inferred_outer_extent_oob(void) {
    int a _Checked[][5] = {{1}, {2}, {3}};
    volatile int i      = 3;
    int          x      = a[i][0];
    (void)x;
}

[[cccc::test(exit_code = 255)]]
void test_multidim_global_inner_oob(void) {
    volatile int j = 5;
    g_grid[1][j]   = 1;
}

[[cccc::test(exit_code = 255)]]
void test_multidim_member_inner_oob(void) {
    struct Grid  s = {0};
    volatile int j = 5;
    s.cells[1][j]  = 1;
}

[[cccc::test(exit_code = 255)]]
void test_multidim_param_inner_oob(void) {
    int a _Checked[3][5] = {{0}};
    (void)grid_param_at(a, 1, 5);
}

typedef int Row _Checked[5];

[[cccc::test(exit_code = 255)]]
void test_multidim_typedef_row_inner_oob(void) {
    Row          a[3];
    volatile int j = 5;
    a[1][j]        = 1;
}

// ---------------------------------------------------------------------
// Limits: a side-effecting row expression is not re-evaluated for its inner
// check, and a pointer-to-row's element access has no checked root.
// ---------------------------------------------------------------------

[[cccc::test]]
void test_multidim_side_effecting_row_evaluated_once(void) {
    int a _Checked[3][5] = {{0}};
    int i                = 0;
    a[i++][1]            = 4;
    AssertEq(i, 1);
    AssertEq(a[0][1], 4);
}

[[cccc::test]]
void test_multidim_pointer_to_row_element_is_unchecked(void) {
    int a _Checked[3][5] = {{0}};
    int (*p)[5]          = a;
    volatile int j       = 5;
    p[0][j]              = 3; // lands in row 1 -- not trapped
    AssertEq(a[1][0], 3);
}

// ---------------------------------------------------------------------
// Variable-length checked arrays: a local's extent is fixed when the
// declaration runs; a parameter is count(n) on the decayed pointer.
// ---------------------------------------------------------------------

static int vla_param_last(int n, int a _Checked[n]) {
    return a[n - 1];
}

[[cccc::test]]
void test_vla_checked_array_in_bounds(void) {
    int   n = 5;
    int a _Checked[n];
    for (int i = 0; i < n; i++)
        a[i] = i;
    AssertEq(a[4], 4);
    AssertEq(vla_param_last(n, a), 4);
}

[[cccc::test]]
void test_vla_checked_array_extent_is_a_snapshot(void) {
    int   n = 5;
    int a _Checked[n];
    n    += 10;
    a[4]  = 7;
    AssertEq(a[4], 7);
}

[[cccc::test]]
void test_vla_checked_array_extent_evaluated_once(void) {
    int   n = 3;
    int a _Checked[n++];
    a[2] = 5;
    AssertEq(n, 4);
    AssertEq(a[2], 5);
}

[[cccc::test]]
void test_vla_checked_array_outer_dimension(void) {
    int   m = 3;
    int a _Checked[m][5];
    a[2][4] = 9;
    AssertEq(a[2][4], 9);
}

[[cccc::test]]
void test_vla_nt_checked_array_terminator_slot(void) {
    int    n = 5;
    char s _Nt_checked[n];
    s[3] = 'x';
    s[4] = 0;
    AssertEq(s[3], 'x');
}

// ---------------------------------------------------------------------
// Compound literals: `_Checked` in an abstract declarator.
// ---------------------------------------------------------------------

[[cccc::test]]
void test_checked_array_compound_literal(void) {
    int  *p = (int _Checked[4]){1, 2, 3, 4};
    int  *q = (int _Checked[]){5, 6, 7};
    char *s = (char _Nt_checked[]){'a', 'b', 0};
    AssertEq(p[3], 4);
    AssertEq(q[2], 7);
    AssertEq(s[1], 'b');
    AssertEq(sizeof(int _Checked[4]), 4 * sizeof(int));
}

// ---------------------------------------------------------------------
// Flexible array members: the count names a sibling field.
// ---------------------------------------------------------------------

struct Fam {
    int n;
    int d _Checked[] [[cccc::count(n)]];
};

struct NtFam {
    int  n;
    char s _Nt_checked[] [[cccc::count(n)]];
};

static int fam_last(struct Fam *f) {
    int *p = f->d;
    return p[f->n - 1];
}

[[cccc::test]]
void test_checked_flexible_array_member(void) {
    struct Fam *f = malloc(sizeof(struct Fam) + 3 * sizeof(int));
    f->n          = 3;
    f->d[0]       = 1;
    f->d[2]       = 42;
    AssertEq(sizeof(struct Fam), sizeof(int));
    AssertEq(f->d[0], 1);
    AssertEq(fam_last(f), 42);
    free(f);
}

[[cccc::test]]
void test_checked_nt_flexible_array_member(void) {
    struct NtFam *f = malloc(sizeof(struct NtFam) + 4);
    f->n            = 3;
    f->s[0]         = 'a';
    f->s[1]         = 'b';
    f->s[2]         = 'c';
    f->s[3]         = 0;
    AssertEq(f->s[2], 'c');
    free(f);
}

// ---------------------------------------------------------------------
// Multidimensional _Nt_checked: the innermost row holds the terminator.
// ---------------------------------------------------------------------

typedef char NtStr _Nt_checked[10];

[[cccc::test]]
void test_nt_2d_terminator_write_ok(void) {
    char s _Nt_checked[3][10];
    s[2][9] = 0;
    s[0][0] = 'a';
    AssertEq(s[0][0], 'a');
    AssertEq(s[2][9], 0);
}

[[cccc::test(exit_code = 255)]]
void test_nt_2d_terminator_nonnull_traps(void) {
    char s _Nt_checked[3][10];
    s[1][9] = 'x';
}

[[cccc::test(exit_code = 255)]]
void test_nt_2d_row_overrun_traps(void) {
    char s _Nt_checked[3][10];
    int    i = 10;
    s[1][i]  = 0;
}

[[cccc::test(exit_code = 255)]]
void test_nt_2d_outer_overrun_traps(void) {
    char s _Nt_checked[3][10];
    int    i = 3;
    s[i][0]  = 0;
}

[[cccc::test]]
void test_nt_typedef_rows_under_checked(void) {
    NtStr t _Checked[3];
    t[1][8] = 'z';
    t[1][9] = 0;
    AssertEq(t[1][8], 'z');
}

[[cccc::test(exit_code = 255)]]
void test_nt_typedef_rows_terminator_traps(void) {
    NtStr t _Checked[3];
    t[2][9] = 1;
}

[[cccc::test]]
void test_nt_2d_inferred_outer(void) {
    char s _Nt_checked[][4] = {"ab", "cd"};
    AssertEq((int)sizeof(s), 8);
    AssertEq(s[1][1], 'd');
    s[1][3] = 0;
}

[[cccc::test(exit_code = 255)]]
void test_nt_2d_inferred_outer_terminator_traps(void) {
    char s _Nt_checked[][4] = {"ab", "cd"};
    s[0][3]                 = 'x';
}

[[cccc::test]]
void test_nt_2d_vla_outer(void) {
    int    n = 2;
    char s _Nt_checked[n][4];
    s[1][3] = 0;
    s[1][0] = 'q';
    AssertEq(s[1][0], 'q');
}

[[cccc::test(exit_code = 255)]]
void test_nt_2d_vla_outer_terminator_traps(void) {
    int    n = 2;
    char s _Nt_checked[n][4];
    s[1][3] = 1;
}

[[cccc::test]]
void test_nt_3d_terminator_write_ok(void) {
    char s _Nt_checked[2][2][4];
    s[1][1][3] = 0;
    s[1][1][2] = 'c';
    AssertEq(s[1][1][2], 'c');
}

[[cccc::test(exit_code = 255)]]
void test_nt_3d_terminator_nonnull_traps(void) {
    char s _Nt_checked[2][2][4];
    s[0][1][3] = 'x';
}
