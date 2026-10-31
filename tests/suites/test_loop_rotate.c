// CCCC_FLAGS: --testing

// A for or while loop tests its condition once before the first iteration and
// again after each one. The condition runs exactly n + 1 times for n
// iterations, and break, continue, goto and clean-up keep their meaning.

int calls;

static int lt(int i, int n) {
    calls++;
    return i < n;
}

static int run_for(int n) {
    int s = 0;
    calls = 0;
    for (int i = 0; lt(i, n); i++)
        s += i;
    return s;
}

static int run_while(int n) {
    int i = 0, s = 0;
    calls = 0;
    while (lt(i, n)) {
        s += i;
        i++;
    }
    return s;
}

[[cccc::test]]
void test_condition_runs_n_plus_one_times(void) {
    AssertEq(run_for(5), 10);
    AssertEq(calls, 6);
    AssertEq(run_while(5), 10);
    AssertEq(calls, 6);
}

[[cccc::test]]
void test_zero_trip_loops_test_once(void) {
    AssertEq(run_for(0), 0);
    AssertEq(calls, 1);
    AssertEq(run_while(0), 0);
    AssertEq(calls, 1);
    AssertEq(run_for(-3), 0);
    AssertEq(calls, 1);
}

[[cccc::test]]
void test_one_trip_loop(void) {
    AssertEq(run_for(1), 0);
    AssertEq(calls, 2);
}

[[cccc::test]]
void test_continue_runs_the_increment_and_the_test(void) {
    int s = 0;
    for (int i = 0; i < 10; i++) {
        if (i % 2)
            continue;
        s += i;
    }
    AssertEq(s, 20);

    int j = 0, t = 0;
    while (j < 10) {
        j++;
        if (j % 3)
            continue;
        t += j;
    }
    AssertEq(t, 18);
}

[[cccc::test]]
void test_break_skips_the_bottom_test(void) {
    calls = 0;
    int i = 0;
    for (; lt(i, 100); i++)
        if (i == 3)
            break;
    AssertEq(i, 3);
    AssertEq(calls, 4);
}

[[cccc::test]]
void test_continue_in_a_loop_with_no_increment(void) {
    int i = 0, s = 0;
    while (i < 6) {
        i++;
        if (i == 2)
            continue;
        s += i;
    }
    AssertEq(s, 19);
}

static int next_value(void) {
    static int v;
    return v++;
}

[[cccc::test]]
void test_assignment_in_the_condition(void) {
    int c, s = 0;
    while ((c = next_value()) < 4)
        s += c;
    AssertEq(s, 6);
    AssertEq(c, 4);
}

[[cccc::test]]
void test_short_circuit_conditions(void) {
    int a[6] = {1, 2, 3, 0, 5, 6};
    int i    = 0;
    while (i < 6 && a[i])
        i++;
    AssertEq(i, 3);

    int n = 0;
    for (int k = 0; k < 2 || n < 5; k++)
        n++;
    AssertEq(n, 5);

    int hits = 0;
    for (int k = 0; k < 4 && lt(k, 100); k++)
        hits++;
    AssertEq(hits, 4);
}

[[cccc::test]]
void test_unsigned_countdown(void) {
    unsigned long n = 5;
    int           s = 0;
    while (n--)
        s++;
    AssertEq(s, 5);
    AssertEq((long)n, -1L);

    unsigned char b = 3;
    int           c = 0;
    for (; b != 0; b--)
        c++;
    AssertEq(c, 3);
}

[[cccc::test]]
void test_pointer_walk(void) {
    char  s[] = "hello";
    char *p   = s;
    int   n   = 0;
    while (*p) {
        p++;
        n++;
    }
    AssertEq(n, 5);
}

[[cccc::test]]
void test_nested_loops(void) {
    int s = 0;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < i; j++)
            s += j + 1;
    AssertEq(s, 10);

    int hit = 0;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (j == 1)
                break;
            hit++;
        }
    }
    AssertEq(hit, 3);
}

[[cccc::test]]
void test_float_condition(void) {
    double d = 3.0;
    int    n = 0;
    while (d) {
        d -= 1.0;
        n++;
    }
    AssertEq(n, 3);

    int m = 0;
    for (double x = 0.0; x < 2.0; x += 0.5)
        m++;
    AssertEq(m, 4);
}

[[cccc::test]]
void test_goto_out_of_and_into_a_loop(void) {
    int i = 0;
    for (; i < 10; i++)
        if (i == 4)
            goto out;
out:
    AssertEq(i, 4);

    int k = 0, s = 0;
    goto inside;
    while (k < 5) {
        s += 100;
    inside:
        s += 1;
        k++;
    }
    AssertEq(s, 405);
}

static int released;
static void release(int *p) {
    released += *p;
}

[[cccc::test]]
void test_cleanup_runs_each_iteration(void) {
    released = 0;
    for (int i = 0; i < 3; i++) {
        int held __attribute__((cleanup(release))) = i + 1;
        if (i == 1)
            continue;
        (void)held;
    }
    AssertEq(released, 6);

    released = 0;
    int j    = 0;
    while (j < 5) {
        int held __attribute__((cleanup(release))) = 10;
        j++;
        if (j == 2)
            break;
    }
    AssertEq(released, 20);
}

// Not generated twice: a statement expression in the condition keeps the test
// at the top.
[[cccc::test]]
void test_statement_expression_condition(void) {
    int n = 0;
    while (({
        int t = n < 3;
        t;
    }))
        n++;
    AssertEq(n, 3);

    int s = 0;
    for (int i = 0; ({ i < 4; }); i++)
        s += i;
    AssertEq(s, 6);
}

[[cccc::test]]
void test_infinite_loops(void) {
    int n = 0;
    for (;;) {
        if (++n == 4)
            break;
    }
    AssertEq(n, 4);

    int m = 0;
    while (1) {
        if (++m == 5)
            break;
    }
    AssertEq(m, 5);

    int z = 0;
    while (0)
        z++;
    AssertEq(z, 0);
}

[[cccc::test]]
void test_a_call_in_the_increment(void) {
    int s = 0;
    for (int i = 0; i < 5; i += lt(0, 1) + 0)
        s += 2;
    AssertEq(s, 10);
}

[[cccc::test]]
void test_struct_member_condition(void) {
    struct {
        int  left;
        int *p;
    } st;
    int v   = 7;
    st.left = 3;
    st.p    = &v;
    int n   = 0;
    while (st.left > 0 && *st.p == 7) {
        st.left--;
        n++;
    }
    AssertEq(n, 3);
}
