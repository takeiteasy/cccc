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
// A side-effecting row expression is not re-evaluated for its inner check;
// a pointer to a row checks its elements against the row's extent.
// ---------------------------------------------------------------------

[[cccc::test]]
void test_multidim_side_effecting_row_evaluated_once(void) {
    int a _Checked[3][5] = {{0}};
    int i                = 0;
    a[i++][1]            = 4;
    AssertEq(i, 1);
    AssertEq(a[0][1], 4);
}

[[cccc::test(exit_code = 255)]]
void test_multidim_pointer_to_row_inner_oob(void) {
    int a _Checked[3][5] = {{0}};
    int (*p)[5]          = a;
    volatile int j       = 5;
    p[0][j]              = 3;
}

[[cccc::test]]
void test_multidim_pointer_to_row_in_bounds(void) {
    int a _Checked[3][5] = {{0}};
    int (*p)[5]          = a;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 5; j++)
            p[i][j] = i * 10 + j;
    AssertEq(a[2][4], 24);
    AssertEq(p[1][3], 13);
}

[[cccc::test]]
void test_multidim_pointer_to_row_side_effecting_row(void) {
    int a _Checked[3][5] = {{0}};
    int (*p)[5]          = a;
    int i                = 0;
    p[i++][1]            = 4;
    AssertEq(i, 1);
    AssertEq(a[0][1], 4);
}

// A row pointer that is checked-rooted on only some paths is OPT: its
// element accesses are checked where it was rooted and unchecked elsewhere.
[[cccc::test]]
void test_multidim_optional_row_pointer_in_bounds(void) {
    int a        _Checked[3][5] = {{0}};
    int          plain[3][5]    = {{0}};
    volatile int c              = 1;
    int (*p)[5]                 = plain;
    if (c)
        p = a;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 5; j++)
            p[i][j] = i * 10 + j;
    AssertEq(a[2][4], 24);
}

[[cccc::test(exit_code = 255)]]
void test_multidim_optional_row_pointer_inner_oob(void) {
    int a        _Checked[3][5] = {{0}};
    int          plain[3][5]    = {{0}};
    volatile int c              = 1;
    int (*p)[5]                 = plain;
    if (c)
        p = a;
    volatile int j = 5;
    p[0][j]        = 3;
}

[[cccc::test(exit_code = 255)]]
void test_multidim_optional_row_pointer_inner_oob_compound(void) {
    int a        _Checked[3][5] = {{0}};
    int          plain[3][5]    = {{0}};
    volatile int c              = 1;
    int (*p)[5]                 = plain;
    if (c)
        p = a;
    volatile int j  = 5;
    p[0][j]        += 3;
}

[[cccc::test]]
void test_multidim_optional_row_pointer_unrooted_not_checked(void) {
    int a        _Checked[3][5] = {{0}};
    volatile int c              = 0;
    int (*p)[5]                 = malloc(sizeof(int) * 50);
    if (c)
        p = a;
    p[0][7] = 5; // c==0: p is the malloc'd pointer -- no row check
    AssertEq(p[0][7], 5);
    free(p);
}

// A row of an OPT row pointer is a source for a declared-checked target
// on the path where the pointer was rooted.
[[cccc::test(exit_code = 255)]]
void test_multidim_optional_row_pointer_row_source_trap(void) {
    int a        _Checked[3][5] = {{0}};
    int          plain[3][5]    = {{0}};
    volatile int c              = 1;
    int (*p)[5]                 = plain;
    if (c)
        p = a;
    int n                                  = 8;
    int *[[cccc::array, cccc::count(n)]] t = p[0]; // count(8) from a row of 5
    (void)t;
}

[[cccc::test]]
void test_multidim_optional_row_pointer_row_source_unrooted_ok(void) {
    int          plain[3][5]    = {{0}};
    int a        _Checked[3][5] = {{0}};
    volatile int c              = 0;
    int (*p)[5]                 = plain;
    if (c)
        p = a;
    int n                                  = 5;
    int *[[cccc::array, cccc::count(n)]] t = p[0];
    AssertEq(t[4], 0);
}

// A row of a row pointer passed to a checked parameter is verified against
// the row's extent.
static int row_sink(int *[[cccc::array, cccc::count(n)]] r, int n) {
    return r[n - 1];
}

[[cccc::test]]
void test_multidim_row_argument_in_bounds(void) {
    int a _Checked[3][5] = {{0}};
    int (*p)[5]          = a;
    a[1][4]              = 7;
    AssertEq(row_sink(p[1], 5), 7);
    AssertEq(row_sink(*p, 5), 0);
}

[[cccc::test(exit_code = 255)]]
void test_multidim_row_argument_oob(void) {
    int a _Checked[3][5] = {{0}};
    int (*p)[5]          = a;
    volatile int n       = 8;
    (void)row_sink(p[0], n);
}

[[cccc::test(exit_code = 255)]]
void test_multidim_row_argument_deref_oob(void) {
    int a _Checked[3][5] = {{0}};
    int (*p)[5]          = a;
    (void)row_sink(*p, 8);
}

[[cccc::test(exit_code = 255)]]
void test_multidim_nested_row_argument_oob(void) {
    int a _Checked[2][3][5] = {{{0}}};
    int (*p)[3][5]          = a;
    (void)row_sink(p[0][1], 8);
}

[[cccc::test]]
void test_multidim_optional_row_argument_in_bounds(void) {
    int          plain[3][5]    = {{0}};
    int a        _Checked[3][5] = {{0}};
    volatile int c              = 1;
    int (*p)[5]                 = plain;
    if (c)
        p = a;
    AssertEq(row_sink(p[0], 5), 0);
}

[[cccc::test(exit_code = 255)]]
void test_multidim_optional_row_argument_oob_rooted(void) {
    int          plain[3][5]    = {{0}};
    int a        _Checked[3][5] = {{0}};
    volatile int c              = 1;
    int (*p)[5]                 = plain;
    if (c)
        p = a;
    (void)row_sink(p[0], 8);
}

[[cccc::test]]
void test_multidim_optional_row_argument_oob_unrooted_ok(void) {
    int          plain[3][8]    = {{0}};
    int a        _Checked[3][5] = {{0}};
    volatile int c              = 0;
    int (*p)[5]                 = (int (*)[5])plain;
    if (c)
        p = a;
    AssertEq(row_sink(p[0], 8), 0);
}

[[cccc::test]]
void test_multidim_unrooted_row_argument_not_checked(void) {
    int (*p)[5] = malloc(sizeof(int) * 50);
    p[0][7]     = 3;
    AssertEq(row_sink(p[0], 8), 3);
    free(p);
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
void test_vla_checked_array_inner_dimension(void) {
    int   n = 4;
    int a _Checked[3][n];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < n; j++)
            a[i][j] = i * 10 + j;
    AssertEq(a[2][3], 23);
    AssertEq((int)sizeof(a[0]), 4 * (int)sizeof(int));
}

[[cccc::test]]
void test_vla_checked_array_every_dimension_varies(void) {
    int   m = 3, n = 4;
    int a _Checked[m][n];
    int c _Checked[2][m][3];
    a[m - 1][n - 1] = 7;
    c[1][2][2]      = 5;
    AssertEq(a[2][3], 7);
    AssertEq(c[1][2][2], 5);
}

[[cccc::test]]
void test_vla_checked_array_inner_extent_is_a_snapshot(void) {
    int   n = 4;
    int a _Checked[2][n];
    n       += 10;
    a[1][3]  = 9;
    AssertEq(a[1][3], 9);
}

[[cccc::test(exit_code = 255)]]
void test_vla_checked_array_inner_oob_traps(void) {
    int          n = 4;
    int a        _Checked[3][n];
    volatile int j = n;
    a[1][j]        = 1;
}

[[cccc::test(exit_code = 255)]]
void test_vla_checked_array_outer_of_vla_rows_traps(void) {
    int          n = 4;
    int a        _Checked[3][n];
    volatile int i = 3;
    a[i][0]        = 1;
}

[[cccc::test]]
void test_vla_nt_checked_array_inner_terminator_slot(void) {
    int    n = 4;
    char s _Nt_checked[2][n];
    s[1][2] = 'x';
    s[1][3] = 0;
    AssertEq(s[1][2], 'x');
}

[[cccc::test(exit_code = 255)]]
void test_vla_nt_checked_array_inner_terminator_traps(void) {
    int          n = 4;
    char s       _Nt_checked[2][n];
    volatile int j = n - 1;
    s[1][j]        = 'x';
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

typedef struct {
    int a;
    int b;
} NtRowCell;

[[cccc::test]]
void test_nt_2d_struct_rows_member_write_ok(void) {
    NtRowCell s _Nt_checked[2][3] = {{{1, 2}, {3, 4}, {0, 0}},
                                     {{5, 6}, {7, 8}, {0, 0}}};
    s[1][1].b                     = 9;
    s[1][2].a                     = 0;
    AssertEq(s[1][1].b, 9);
}

[[cccc::test(exit_code = 255)]]
void test_nt_2d_struct_rows_member_write_traps(void) {
    NtRowCell s _Nt_checked[2][3] = {{{1, 2}, {3, 4}, {0, 0}},
                                     {{5, 6}, {7, 8}, {0, 0}}};
    s[1][2].b                     = 1;
}

struct BytesFam {
    int   bytes;
    int d _Checked[] [[cccc::byte_count(bytes)]];
};

struct RangeFam {
    int  *end;
    int d _Checked[] [[cccc::bounds(d, end)]];
};

struct GnuFam {
    int   n;
    int d _Checked[] __attribute__((__count__(n)));
};

struct NtBytesFam {
    int    bytes;
    char s _Nt_checked[] [[cccc::byte_count(bytes)]];
};

struct OptOutFam {
    int   n;
    int d _Checked[] [[cccc::bounds(unknown)]];
};

[[cccc::test]]
void test_checked_flexible_array_member_forms(void) {
    struct BytesFam *b = malloc(sizeof(struct BytesFam) + 3 * sizeof(int));
    b->bytes           = 3 * sizeof(int);
    b->d[2]            = 7;
    AssertEq(b->d[2], 7);
    free(b);

    struct RangeFam *r = malloc(sizeof(struct RangeFam) + 3 * sizeof(int));
    r->end             = r->d + 3;
    r->d[2]            = 8;
    AssertEq(r->d[2], 8);
    free(r);

    struct GnuFam *g = malloc(sizeof(struct GnuFam) + 3 * sizeof(int));
    g->n             = 3;
    g->d[2]          = 9;
    AssertEq(g->d[2], 9);
    free(g);

    struct NtBytesFam *s = malloc(sizeof(struct NtBytesFam) + 4);
    s->bytes             = 3;
    s->s[0]              = 'a';
    s->s[3]              = 0;
    AssertEq(s->s[0], 'a');
    free(s);

    struct OptOutFam *o = malloc(sizeof(struct OptOutFam) + 8 * sizeof(int));
    o->n                = 1;
    o->d[5]             = 6;
    AssertEq(o->d[5], 6);
    free(o);
}
