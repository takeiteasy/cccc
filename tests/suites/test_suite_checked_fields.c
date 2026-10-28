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

struct Inner {
    int d[2];
    int t[2];
};

struct Outer {
    int          pad[2];
    struct Inner m[3];
};

struct VolCount {
    volatile unsigned char n;
    int *[[cccc::array, cccc::count(n)]] p;
};

struct WideCount {
    unsigned long n;
    int *[[cccc::array, cccc::count(n)]] p;
};

union Words {
    int  a[2];
    char b[8];
};

union Wrap {
    struct Holder h;
    int           w[16];
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
// A count written inside a value that is used.
// ---------------------------------------------------------------------

static int widen_returned(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    note();
    return s.n = 8;
}

[[cccc::test(exit_code = 255)]]
void test_track_value_condition_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    note();
    if ((s.n = 8)) {
    }
}

[[cccc::test(exit_code = 255)]]
void test_track_value_assigned_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    int          x                             = 0;
    s.p                                        = small;
    note();
    x = (s.n = 8);
    (void)x;
}

[[cccc::test(exit_code = 255)]]
void test_track_value_initializer_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    note();
    int x = (s.n = 8);
    (void)x;
}

[[cccc::test(exit_code = 255)]]
void test_track_value_return_trap(void) {
    (void)widen_returned();
}

[[cccc::test(exit_code = 255)]]
void test_track_value_compound_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    note();
    if ((s.n += 6)) {
    }
}

[[cccc::test(exit_code = 255)]]
void test_track_value_postfix_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    int          x                             = 0;
    s.p                                        = small;
    note();
    s.n = 2;
    x   = s.n++;
    (void)x;
}

[[cccc::test(exit_code = 255)]]
void test_track_value_loop_condition_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    note();
    while ((s.n = 8)) {
        break;
    }
}

[[cccc::test(exit_code = 255)]]
void test_track_value_for_condition_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    note();
    for (; (s.n = 8);)
        break;
}

[[cccc::test(exit_code = 255)]]
void test_track_value_nested_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    int          x                             = 0;
    s.p                                        = small;
    note();
    if (small) {
        x = (s.n = 8);
    }
    (void)x;
}

[[cccc::test(exit_code = 255)]]
void test_track_value_volatile_count_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct VolCount v                          = {0};
    int          x                             = 0;
    v.p                                        = small;
    note();
    x = (v.n = 8);
    (void)x;
}

[[cccc::test(exit_code = 255)]]
void test_track_value_wide_count_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct WideCount w                         = {0};
    w.p                                        = small;
    note();
    if ((w.n = 8)) {
    }
}

[[cccc::test]]
void test_track_value_is_preserved(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    struct VolCount v                          = {0};
    s.p                                        = small;
    v.p                                        = small;
    note();
    int x = (s.n = 2);
    int y = (v.n = 1);
    AssertEq(x, 2);
    AssertEq(y, 1);
    AssertEq(s.n, 2);
}

[[cccc::test]]
void test_track_value_short_circuit_passes(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    int          off                           = 0;
    s.p                                        = small;
    note();
    if (off && (s.n = 8)) {
    }
    AssertEq(s.n, 0);
}

[[cccc::test]]
void test_track_value_pointer_write_ends_tracking_passes(void) {
    int *[[cccc::array, cccc::count(8)]] big   = eight();
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    s.p                                        = big;
    note();
    if ((s.n = 8)) {
    }
    AssertEq(s.n, 8);
}

// A value-used write sequenced before an event that ends tracking is still
// rechecked. `widen(&s)` replaces the pointer and makes `s` non-private.
static int widen(struct Count *c) {
    c->p = eight();
    return 1;
}

[[cccc::test(exit_code = 255)]]
void test_track_value_before_call_and_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    if ((s.n = 8) && widen(&s)) {
    }
}

[[cccc::test(exit_code = 255)]]
void test_track_value_before_call_or_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    if ((s.n = 8) || widen(&s)) {
    }
}

[[cccc::test(exit_code = 255)]]
void test_track_value_before_call_conditional_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    (void)((s.n = 8) ? widen(&s) : 0);
}

[[cccc::test(exit_code = 255)]]
void test_track_value_before_call_conditional_branch_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    volatile int c                             = 1;
    s.p                                        = small;
    (void)(c ? (s.n = 8) : widen(&s));
}

[[cccc::test(exit_code = 255)]]
void test_track_value_before_call_comma_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    if (((s.n = 8), widen(&s))) {
    }
}

[[cccc::test(exit_code = 255)]]
void test_track_value_before_call_assigned_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    int          x                             = 0;
    s.p                                        = small;
    x                                          = (s.n = 8) && widen(&s);
    (void)x;
}

[[cccc::test(exit_code = 255)]]
void test_track_value_before_call_initializer_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    int x                                      = (s.n = 8) && widen(&s);
    (void)x;
}

[[cccc::test]]
void test_track_value_after_call_passes(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    if (widen(&s) && (s.n = 8)) {
    }
    AssertEq(s.n, 8);
}

[[cccc::test]]
void test_track_value_unsequenced_with_pointer_write_passes(void) {
    int *[[cccc::array, cccc::count(8)]] big   = eight();
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    if ((s.n = 8) + (s.p = big, 0)) {
    }
    AssertEq(s.n, 8);
}

[[cccc::test]]
void test_track_value_unsequenced_with_call_passes(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    if (((s.n = 8), 0) + widen(&s)) {
    }
    AssertEq(s.n, 8);
}

// Iteration two writes after iteration one's call replaced the pointer.
[[cccc::test]]
void test_track_value_before_call_in_loop_passes(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    for (int i = 0; i < 2; i++) {
        if ((s.n = i ? 8 : 2) && widen(&s)) {
        }
    }
    AssertEq(s.n, 8);
}

// A write in an earlier unit of a nested statement than the event that ends
// tracking is rechecked.
[[cccc::test(exit_code = 255)]]
void test_track_nested_value_then_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    if ((s.n = 8)) {
        widen(&s);
    }
}

[[cccc::test(exit_code = 255)]]
void test_track_nested_value_else_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    if ((s.n = 8)) {
    } else {
        widen(&s);
    }
}

[[cccc::test(exit_code = 255)]]
void test_track_nested_value_block_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    {
        if ((s.n = 8)) {
        }
        widen(&s);
    }
}

[[cccc::test(exit_code = 255)]]
void test_track_nested_value_switch_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    switch ((s.n = 8)) {
        default:
            widen(&s);
    }
}

[[cccc::test(exit_code = 255)]]
void test_track_nested_value_for_init_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    for (int i = (s.n = 8); i < 9; i++)
        widen(&s);
}

[[cccc::test(exit_code = 255)]]
void test_track_nested_value_before_loop_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    {
        if ((s.n = 8)) {
        }
        for (int i = 0; i < 2; i++)
            widen(&s);
    }
}

[[cccc::test(exit_code = 255)]]
void test_track_nested_discarded_then_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    volatile int c                             = 1;
    s.p                                        = small;
    if (c) {
        s.n = 8;
        widen(&s);
    }
}

[[cccc::test(exit_code = 255)]]
void test_track_nested_discarded_exclusive_trap(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    volatile int c                             = 1;
    s.p                                        = small;
    if (c) {
        s.n = 8;
    } else {
        widen(&s);
    }
}

[[cccc::test]]
void test_track_nested_after_call_passes(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    if (widen(&s)) {
        s.n = 8;
    }
    AssertEq(s.n, 8);
}

// Iteration two writes after iteration one's call replaced the pointer.
[[cccc::test]]
void test_track_nested_value_loop_passes(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    for (int i = 0; i < 2; i++) {
        if ((s.n = i ? 8 : 2)) {
        }
        widen(&s);
    }
    AssertEq(s.n, 8);
}

[[cccc::test]]
void test_track_nested_discarded_loop_passes(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    s.p                                        = small;
    for (int i = 0; i < 2; i++) {
        s.n = i ? 8 : 2;
        widen(&s);
    }
    AssertEq(s.n, 8);
}

[[cccc::test]]
void test_track_nested_value_do_loop_passes(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    int          i                             = 0;
    s.p                                        = small;
    do {
        if ((s.n = i ? 8 : 2)) {
        }
        widen(&s);
    } while (++i < 2);
    AssertEq(s.n, 8);
}

[[cccc::test]]
void test_track_nested_value_loop_condition_passes(void) {
    int *[[cccc::array, cccc::count(2)]] small = two();
    struct Count s                             = {0};
    int          i                             = 0;
    s.p                                        = small;
    while ((s.n = i ? 8 : 2) && i < 2) {
        widen(&s);
        i++;
    }
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

[[cccc::test(exit_code = 255)]]
void test_plain_array_element_member_trap(void) {
    struct Holder arr[3] = {0};
    sink(arr[0].data, 100);
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_element_member_runtime_index_trap(void) {
    struct Holder arr[3] = {0};
    int           k      = 1;
    sink(arr[k].data, 100);
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_element_trailing_member_trap(void) {
    struct Holder arr[3] = {0};
    int           k      = 2;
    sink(arr[k].tail, 100);
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_element_index_out_of_range_is_empty(void) {
    struct Holder arr[3] = {0};
    int           k      = 5;
    sink(arr[k].data, 4);
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_nested_element_member_trap(void) {
    struct Outer o[2] = {0};
    int          k    = 1;
    sink(o[k].m[1].d, 100);
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_inner_index_out_of_range_is_empty(void) {
    struct Outer o[2] = {0};
    int          k    = 0;
    sink(o[k].m[5].d, 2);
}

[[cccc::test]]
void test_plain_array_element_member_exact_fit_passes(void) {
    struct Holder arr[3] = {0};
    struct Outer  o[2]   = {0};
    int           k      = 2;
    sink(arr[k].data, 4);
    sink(arr[k].tail, 4);
    sink(o[1].m[2].d, 2);
}

[[cccc::test]]
void test_plain_array_element_member_through_pointer(void) {
    struct Holder  arr[3] = {0};
    struct Holder *ps     = arr;
    int            k      = 1;
    sink(ps[k].tail, 100);
}

[[cccc::test]]
void test_plain_array_element_member_carry_is_snapshotted(void) {
    struct Holder arr[3] = {0};
    int           k      = 1;
    int          *q      = arr[k].data;
    k                    = 7;
    sink(q, 4);
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_element_member_carry_trap(void) {
    struct Holder arr[3] = {0};
    int           k      = 1;
    int          *q      = arr[k].data;
    k                    = 7;
    sink(q, 100);
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_union_member_trap(void) {
    union Words u = {0};
    sink(u.a, 100);
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_union_element_member_trap(void) {
    union Words ua[2] = {0};
    int         k     = 1;
    sink(ua[k].a, 100);
}

[[cccc::test(exit_code = 255)]]
void test_plain_array_union_nested_tail_trap(void) {
    union Wrap uh = {0};
    sink(uh.h.tail, 100);
}

[[cccc::test]]
void test_plain_array_union_member_exact_fit_passes(void) {
    union Words u = {0};
    sink(u.a, 2);
}

[[cccc::test]]
void test_plain_array_union_member_through_pointer_is_not_a_source(void) {
    union Words  u  = {0};
    union Words *up = &u;
    sink(up->a, 100);
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
static int *[[cccc::array, cccc::count(1)]] g_elem        = &g_arr[1];
static int *[[cccc::array, cccc::bounds(g_arr, g_arr + 2)]] g_range = g_arr;
static int *[[cccc::array, cccc::bounds(g_arr + 1, g_arr + 2)]] g_inner_range =
    &g_arr[1];

struct Range {
    int *lo;
    int *hi;
    int *[[cccc::array, cccc::bounds(lo, hi)]] p;
};

static struct Range g_member_range = {g_arr, g_arr + 2, g_arr};

[[cccc::test]]
void test_static_initializers_that_fit(void) {
    AssertEq(g_exact.n, 2);
    AssertEq(g_inner.n, 1);
    AssertEq(g_late.n, 2);
    AssertEq(g_nt[0], 'a');
    (void)g_null;
    (void)g_dynamic;
    (void)g_elem;
    (void)g_range;
    (void)g_inner_range;
    AssertEq(g_member_range.hi - g_member_range.lo, 2);
}
