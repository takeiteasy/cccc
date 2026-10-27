// CCCC_FLAGS: --testing --checked-pointers
// A pointer's bounds checked against the writes that follow it: bound-naming
// siblings written right after the pointer, aggregate initializers, and plain
// arrays as sources.

struct Count {
    int n;
    int *[[cccc::array, cccc::count(n)]] p;
};

struct CountLast {
    int *[[cccc::array, cccc::count(n)]] p;
    int n;
};

struct Pair {
    int *[[cccc::array, cccc::count(n)]] p;
    int *[[cccc::array, cccc::count(n)]] q;
    int n;
};

struct Holder {
    int n;
    int *[[cccc::array, cccc::count(n)]] p;
    int data[4];
    int tail[4];
};

static int  g_buf[4];
static int  g_dummy;
static char g_text[] = "abc";

static int *[[cccc::array, cccc::count(2)]] two(void) {
    static int pair[2];
    return pair;
}

static void sink(int *[[cccc::array, cccc::count(n)]] p, int n) {
    (void)p;
    (void)n;
}

static int *[[cccc::array, cccc::count(n)]] pass_through(int n) {
    return g_buf;
}

static void widen_parameter(int *[[cccc::array, cccc::count(n)]] p, int n) {
    p = two();
    n = 8;
    (void)p;
}

static void take_array(int a[2]) {
    sink(a, 100);
}

// ---------------------------------------------------------------------
// Sibling written after the pointer.
// ---------------------------------------------------------------------

[[cccc::test(exit_code = 255)]]
void test_group_widen_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    s.n                                        = 8;
}

[[cccc::test(exit_code = 255)]]
void test_group_widen_through_pointer_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count  s                            = {0};
    struct Count *ps                           = &s;
    ps->p                                      = small;
    ps->n                                      = 8;
}

[[cccc::test(exit_code = 255)]]
void test_group_compound_assign_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small  = two();
    struct Count s                              = {0};
    s.n                                         = 2;
    s.p                                         = small;
    s.n                                        += 6;
}

[[cccc::test(exit_code = 255)]]
void test_group_increment_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.n                                        = 2;
    s.p                                        = small;
    s.n++;
    s.n++;
}

[[cccc::test(exit_code = 255)]]
void test_group_comma_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p = small, s.n = 8;
}

[[cccc::test(exit_code = 255)]]
void test_group_indexed_object_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count a[2]                          = {};
    int          k                             = 1;
    a[k].p                                     = small;
    a[k].n                                     = 8;
}

[[cccc::test(exit_code = 255)]]
void test_group_two_pointers_one_count_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Pair u                              = {0};
    u.p                                        = small;
    u.q                                        = small;
    u.n                                        = 8;
}

[[cccc::test(exit_code = 255)]]
void test_group_local_count_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    int n                                      = 2;
    int *[[cccc::array, cccc::count(n)]] q     = small;
    n                                          = 8;
    (void)q;
}

[[cccc::test(exit_code = 255)]]
void test_group_parameter_count_trap(void) {
    widen_parameter(two(), 2);
}

[[cccc::test(exit_code = 255)]]
void test_group_initializer_pointer_first_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct CountLast t                         = {small, 8};
    (void)t;
}

[[cccc::test(exit_code = 255)]]
void test_group_designated_initializer_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct CountLast t                         = {.p = small, .n = 8};
    (void)t;
}

[[cccc::test(exit_code = 255)]]
void test_group_compound_literal_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct CountLast t                         = (struct CountLast){small, 8};
    (void)t;
}

[[cccc::test(exit_code = 255)]]
void test_group_value_reads_pointer_traps_at_pointer_store(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.n                                        = 100;
    s.p                                        = small;
    s.n                                        = s.p[50];
}

[[cccc::test]]
void test_group_narrowing_passes(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    s.n                                        = 8;
    s.n                                        = 2;
    AssertEq(s.n, 2);
}

[[cccc::test]]
void test_group_exact_count_passes(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    s.n                                        = 2;
    s.n--;
    AssertEq(s.n, 1);
}

// ---------------------------------------------------------------------
// Sibling written after other statements.
// ---------------------------------------------------------------------

static int g_calls;
static void note(void) {
    g_calls++;
}

static int *[[cccc::array, cccc::count(8)]] eight(void) {
    static int block[8];
    return block;
}

static void swap_in_eight(struct Count *c) {
    c->p = eight();
}

[[cccc::test(exit_code = 255)]]
void test_track_after_call_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    note();
    s.n = 8;
}

[[cccc::test(exit_code = 255)]]
void test_track_compound_assign_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    note();
    s.n += 8;
}

[[cccc::test(exit_code = 255)]]
void test_track_branch_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    note();
    if (g_calls > 0)
        s.n = 8;
}

[[cccc::test(exit_code = 255)]]
void test_track_loop_increment_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    note();
    for (int i = 0; i < 4; i++)
        s.n++;
}

[[cccc::test(exit_code = 255)]]
void test_track_indexed_object_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count a[2]                          = {};
    int          k                             = 1;
    a[k].p                                     = small;
    note();
    a[k].n = 8;
}

[[cccc::test(exit_code = 255)]]
void test_track_local_count_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    int n                                      = 2;
    int *[[cccc::array, cccc::count(n)]] q     = small;
    note();
    n = 8;
    (void)q;
}

[[cccc::test(exit_code = 255)]]
void test_track_second_pointer_does_not_stop_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Pair u                              = {0};
    u.p                                        = small;
    note();
    u.q = small;
    u.n = 8;
}

[[cccc::test(exit_code = 255)]]
void test_track_alias_write_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count  s                            = {0};
    struct Count *q                            = &s;
    s.p                                        = small;
    note();
    q->n = 8;
}

[[cccc::test(exit_code = 255)]]
void test_track_alias_pointer_store_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count  s                            = {0};
    struct Count *q                            = &s;
    q->p                                       = small;
    note();
    s.n = 8;
}

[[cccc::test(exit_code = 255)]]
void test_track_alias_to_count_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    int         *pn                            = &s.n;
    s.p                                        = small;
    note();
    *pn = 8;
}

[[cccc::test]]
void test_track_leaked_alias_passes(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count  s                            = {0};
    struct Count *q                            = &s;
    s.p                                        = small;
    swap_in_eight(q);
    s.n = 8;
    AssertEq(s.p[7], 0);
}

[[cccc::test]]
void test_track_member_address_passes(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count  s                            = {0};
    struct Count *q                            = &s;
    s.p                                        = small;
    swap_in_eight(&*q);
    s.n = 8;
    AssertEq(s.p[7], 0);
}

[[cccc::test]]
void test_track_narrowing_passes(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    note();
    s.n = 2;
    s.n = 1;
    AssertEq(s.n, 1);
}

[[cccc::test]]
void test_track_pointer_replaced_passes(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    note();
    s.p = eight();
    s.n = 8;
    AssertEq(s.n, 8);
}

[[cccc::test]]
void test_track_escaping_object_passes(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    swap_in_eight(&s);
    s.n = 8;
    AssertEq(s.p[7], 0);
}

[[cccc::test]]
void test_track_object_index_changed_passes(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count a[2]                          = {};
    int          k                             = 0;
    a[k].p                                     = small;
    note();
    k++;
    a[k].n = 8;
    AssertEq(a[1].n, 8);
}

[[cccc::test]]
void test_track_other_object_passes(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    struct Count t                             = {0};
    s.p                                        = small;
    note();
    t.n = 8;
    AssertEq(t.n, 8);
}

[[cccc::test]]
void test_track_label_ends_tracking_passes(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    goto later;
later:
    s.n = 8;
    AssertEq(s.n, 8);
}

// ---------------------------------------------------------------------
// Plain arrays as sources.
// ---------------------------------------------------------------------

[[cccc::test(exit_code = 255)]]
void test_plain_array_local_trap(void) {
    int buf[2];
    int *[[cccc::array, cccc::count(8)]] p = buf;
    (void)p;
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_global_trap(void) {
    int *[[cccc::array, cccc::count(8)]] p = g_buf;
    (void)p;
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_offset_trap(void) {
    int *[[cccc::array, cccc::count(4)]] p = g_buf + 1;
    (void)p;
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_string_literal_trap(void) {
    char *[[cccc::array, cccc::count(8)]] p = "abc";
    (void)p;
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_argument_trap(void) {
    sink(g_buf, 8);
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_return_trap(void) {
    (void)pass_through(8);
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_member_trap(void) {
    struct Holder h = {0};
    sink(h.data, 8);
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_into_member_trap(void) {
    struct Count s = {0};
    s.n            = 8;
    s.p            = g_buf;
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_dynamic_cast_trap(void) {
    int *[[cccc::array, cccc::count(8)]] p =
        (int *[[cccc::array, cccc::count(8), cccc::dynamic]])g_buf;
    (void)p;
}

[[cccc::test]]
void test_plain_array_exact_fit_passes(void) {
    int *[[cccc::array, cccc::count(4)]] p = g_buf;
    sink(g_buf, 4);
    AssertEq(p[3], 0);
}

[[cccc::test]]
void test_plain_array_dynamic_cast_passes(void) {
    int *[[cccc::array, cccc::count(4)]] p =
        (int *[[cccc::array, cccc::count(4), cccc::dynamic]])g_buf;
    AssertEq(p[0], 0);
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_trailing_member_trap(void) {
    struct Holder h = {0};
    sink(h.tail, 100);
}

[[cccc::test]]
void test_plain_array_trailing_member_through_pointer_is_not_a_source(void) {
    struct Holder  h  = {0};
    struct Holder *hp = &h;
    sink(hp->tail, 100);
}

[[cccc::test]]
void test_plain_array_trailing_member_exact_fit_passes(void) {
    struct Holder h = {0};
    sink(h.tail, 4);
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_address_of_element_trap(void) {
    int buf[2];
    int *[[cccc::array, cccc::count(2)]] p = &buf[1];
    (void)p;
}

[[cccc::test]]
void test_plain_array_address_of_element_passes(void) {
    int buf[8] = {0};
    int *[[cccc::array, cccc::count(4)]] p = &buf[1];
    sink(&buf[2], 4);
    AssertEq(p[0], 0);
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_address_of_member_element_trap(void) {
    struct Holder h = {0};
    sink(&h.data[1], 8);
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_row_trap(void) {
    int m[3][4];
    int *[[cccc::array, cccc::count(8)]] p = m[1];
    (void)p;
}

[[cccc::test]]
void test_plain_array_row_passes(void) {
    int m[3][4] = {0};
    int *[[cccc::array, cccc::count(4)]] p = m[2];
    sink(m[0], 4);
    AssertEq(p[3], 0);
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_row_out_of_range_trap(void) {
    int          m[3][4];
    volatile int i = 7;
    sink(m[i], 4);
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_inner_row_trap(void) {
    int m[2][3][4];
    sink(m[1][2], 8);
}

[[cccc::test]]
void test_plain_array_row_loop_passes(void) {
    int m[3][4];
    int total = 0;
    for (int i = 0; i < 3; i++) {
        sink(m[i], 4);
        for (int j = 0; j < 4; j++) {
            m[i][j] = i + j;
            total += m[i][j];
        }
    }
    AssertEq(total, 30);
}

[[cccc::test]]
void test_plain_array_row_through_pointer_is_not_a_source(void) {
    int  m[3][4];
    int (*pm)[4] = m;
    sink(pm[1], 100);
}

[[cccc::test]]
void test_plain_array_parameter_is_not_a_source(void) {
    int raw[2];
    take_array(raw);
}

[[cccc::test]]
void test_unchecked_pointer_without_a_source_is_not_checked(void) {
    int *raw = &g_dummy;
    sink(raw, 100);
}

// ---------------------------------------------------------------------
// A plain array held by an unchecked local.
// ---------------------------------------------------------------------

[[cccc::test(exit_code = 255)]]
void test_plain_local_into_checked_local_trap(void) {
    int  buf[2];
    int *q                                 = buf;
    int *[[cccc::array, cccc::count(8)]] p = q;
    (void)p;
}

[[cccc::test(exit_code = 255)]]
void test_plain_local_into_field_trap(void) {
    int          buf[2];
    int         *q = buf;
    struct Count s = {0};
    s.n            = 8;
    s.p            = q;
}

[[cccc::test(exit_code = 255)]]
void test_plain_local_argument_trap(void) {
    int  buf[2];
    int *q = buf;
    sink(q, 8);
}

[[cccc::test(exit_code = 255)]]
void test_plain_local_offset_trap(void) {
    int  buf[4];
    int *q = buf + 2;
    sink(q, 3);
}

[[cccc::test(exit_code = 255)]]
void test_plain_local_chain_trap(void) {
    int  buf[4];
    int *q = buf;
    int *r = q + 1;
    sink(r, 4);
}

[[cccc::test(exit_code = 255)]]
void test_plain_local_row_trap(void) {
    int  m[3][4];
    int *q = m[1];
    sink(q, 8);
}

[[cccc::test(exit_code = 255)]]
void test_plain_local_trailing_member_trap(void) {
    struct Holder h = {0};
    int          *q = h.tail;
    sink(q, 8);
}

[[cccc::test]]
void test_plain_local_fits_passes(void) {
    int          buf[8] = {0};
    int         *q      = buf + 2;
    int         *r      = q + 1;
    struct Count s      = {0};
    s.n                 = 5;
    s.p                 = r;
    sink(q, 6);
    sink(r, 5);
    AssertEq(s.p[4], 0);
}

[[cccc::test]]
void test_plain_local_derefs_stay_unchecked(void) {
    int          buf[2] = {7, 9};
    int         *q      = buf;
    volatile int i      = 1;
    AssertEq(q[i], 9);
    AssertEq((q + 1)[0], 9);
}

[[cccc::test]]
void test_plain_local_reassigned_from_a_pointer_is_not_verified(void) {
    int          buf[2] = {0};
    int         *q      = buf;
    volatile int z      = 1;
    if (z)
        q = g_buf;
    sink(q, 4);
}

[[cccc::test]]
void test_plain_local_mixed_with_checked_keeps_deref_checks(void) {
    int *[[cccc::array, cccc::count(4)]] c = g_buf;
    int  buf[8]                            = {0};
    int *q                                 = buf;
    q                                      = c;
    AssertEq(q[3], 0);
}

[[cccc::test(exit_code = 255)]]
void test_plain_local_mixed_with_checked_still_checks_derefs(void) {
    int *[[cccc::array, cccc::count(4)]] c = g_buf;
    int          buf[8]                    = {0};
    int         *q                         = buf;
    volatile int i                         = 6;
    q                                      = c;
    (void)q[i];
}

// ---------------------------------------------------------------------
// Constant file-scope initializers that fit.
// ---------------------------------------------------------------------

static int              g_arr[2];
static struct Count     g_exact                           = {2, g_arr};
static struct Count     g_inner                           = {1, g_arr + 1};
static struct Count     g_null                            = {4, 0};
static struct CountLast g_late                            = {g_arr, 2};
static int              g_len                             = 100;
static int *[[cccc::array, cccc::count(g_len)]] g_dynamic = g_arr;
static char *[[cccc::ntarray, cccc::count(3)]] g_nt       = g_text;

[[cccc::test]]
void test_static_initializers_that_fit(void) {
    AssertEq(g_exact.n, 2);
    AssertEq(g_inner.n, 1);
    AssertEq(g_late.n, 2);
    AssertEq(g_nt[0], 'a');
    (void)g_null;
    (void)g_dynamic;
}
