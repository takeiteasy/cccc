// CCCC_FLAGS: --testing

// &&, || and ! in a condition branch straight to the targets: every context
// keeps the C short-circuit order and result.

int calls;
int order[16];
int nord;
int tr(int v) {
    calls++;
    order[nord++] = v;
    return v;
}

static void reset(void) {
    calls = 0;
    nord  = 0;
}

[[cccc::test]]
void test_if_and_stops_at_false(void) {
    reset();
    int hit = 0;
    if (tr(0) && tr(1))
        hit = 1;
    AssertEq(hit, 0);
    AssertEq(calls, 1);
}

[[cccc::test]]
void test_if_and_runs_both(void) {
    reset();
    int hit = 0;
    if (tr(2) && tr(3))
        hit = 1;
    AssertEq(hit, 1);
    AssertEq(calls, 2);
    AssertEq(order[1], 3);
}

[[cccc::test]]
void test_if_or_stops_at_true(void) {
    reset();
    int hit = 0;
    if (tr(1) || tr(1))
        hit = 1;
    AssertEq(hit, 1);
    AssertEq(calls, 1);
}

[[cccc::test]]
void test_if_or_runs_both(void) {
    reset();
    int hit = 1;
    if (tr(0) || tr(0))
        hit = 0;
    AssertEq(hit, 1);
    AssertEq(calls, 2);
}

[[cccc::test]]
void test_if_else_arms(void) {
    int x = 5, r = 0;
    if (x < 0 || x > 9)
        r = 1;
    else
        r = 2;
    AssertEq(r, 2);
    x = 12;
    if (x < 0 || x > 9)
        r = 1;
    else
        r = 2;
    AssertEq(r, 1);
}

[[cccc::test]]
void test_not_of_or(void) {
    reset();
    int hit = 0;
    if (!(tr(0) || tr(0)))
        hit = 1;
    AssertEq(hit, 1);
    AssertEq(calls, 2);
}

[[cccc::test]]
void test_not_of_and(void) {
    reset();
    int hit = 0;
    if (!(tr(1) && tr(0)))
        hit = 1;
    AssertEq(hit, 1);
    reset();
    hit = 1;
    if (!(tr(1) && tr(1)))
        hit = 0;
    AssertEq(hit, 1);
}

[[cccc::test]]
void test_double_not(void) {
    int v = 7, hit = 0;
    if (!!v)
        hit = 1;
    AssertEq(hit, 1);
    v = 0;
    if (!!v)
        hit = 2;
    AssertEq(hit, 1);
}

[[cccc::test]]
void test_nested_mix(void) {
    int cases[8][3] = {{0, 0, 0}, {0, 0, 1}, {0, 1, 0}, {0, 1, 1},
                       {1, 0, 0}, {1, 0, 1}, {1, 1, 0}, {1, 1, 1}};
    for (int i = 0; i < 8; i++) {
        int a = cases[i][0], b = cases[i][1], c = cases[i][2];
        int want = (a != 0) && ((b != 0) || !(c != 0));
        int got  = 0;
        if (a && (b || !c))
            got = 1;
        AssertEq(got, want);
        int val = a && (b || !c);
        AssertEq(val, want);
        int val2 = (a || b) && (!a || c);
        AssertEq(val2, ((a != 0) || (b != 0)) && (!(a != 0) || (c != 0)));
    }
}

[[cccc::test]]
void test_value_is_zero_or_one(void) {
    int a = 5, b = 9;
    AssertEq(a && b, 1);
    AssertEq(a || 0, 1);
    AssertEq(0 || 0, 0);
    AssertEq(a && 0, 0);
    AssertEq(!a, 0);
    AssertEq(!0, 1);
}

[[cccc::test]]
void test_discarded_value_still_short_circuits(void) {
    reset();
    tr(0) && tr(1);
    AssertEq(calls, 1);
    reset();
    tr(1) || tr(1);
    AssertEq(calls, 1);
    reset();
    tr(1) && tr(2);
    AssertEq(calls, 2);
}

[[cccc::test]]
void test_while_condition(void) {
    int i = 0, n = 10;
    while (i < n && i != 4)
        i++;
    AssertEq(i, 4);
    i = 0;
    while (i > 3 || i < 6)
        if (++i == 20)
            break;
    AssertEq(i, 20);
}

[[cccc::test]]
void test_for_condition(void) {
    int s = 0;
    for (int i = 0; i < 10 && s < 12; i++)
        s += i;
    AssertEq(s, 15);
}

[[cccc::test]]
void test_do_while_condition(void) {
    int i = 0;
    do {
        i++;
    } while (i < 3 || i == 7);
    AssertEq(i, 3);
    int j = 10;
    do {
        j--;
    } while (j > 5 && j != 8);
    AssertEq(j, 8);
}

[[cccc::test]]
void test_ternary_condition(void) {
    int a = 1, b = 0;
    AssertEq((a && b) ? 10 : 20, 20);
    AssertEq((a || b) ? 10 : 20, 10);
    AssertEq(!a ? 10 : 20, 20);
    AssertEq(!b ? 10 : 20, 10);
}

[[cccc::test]]
void test_ternary_inside_logical(void) {
    reset();
    int a = 1, b = 0, c = 1;
    int r = (a ? b : c) && tr(1);
    AssertEq(r, 0);
    AssertEq(calls, 0);
    r = (b ? b : c) && tr(1);
    AssertEq(r, 1);
    AssertEq(calls, 1);
}

[[cccc::test]]
void test_comma_in_condition(void) {
    reset();
    int hit = 0;
    if ((tr(4), tr(0)) || tr(5))
        hit = 1;
    AssertEq(hit, 1);
    AssertEq(calls, 3);
    AssertEq(order[0], 4);
}

[[cccc::test]]
void test_constant_conditions(void) {
    int n = 0;
    if (0)
        n = 1;
    if (1)
        n += 2;
    if (0 && n)
        n = 100;
    if (1 || n)
        n += 4;
    if (!0)
        n += 8;
    if (!1)
        n = 100;
    AssertEq(n, 14);
    int i = 0;
    while (1) {
        if (++i == 5)
            break;
    }
    AssertEq(i, 5);
    for (;;) {
        if (++i == 9)
            break;
    }
    AssertEq(i, 9);
    do {
        i++;
    } while (0);
    AssertEq(i, 10);
}

[[cccc::test]]
void test_constant_with_side_effect_operand(void) {
    reset();
    int hit = 0;
    if (tr(1) && 0)
        hit = 1;
    AssertEq(hit, 0);
    AssertEq(calls, 1);
    if (0 || tr(1))
        hit = 2;
    AssertEq(hit, 2);
    AssertEq(calls, 2);
}

[[cccc::test]]
void test_pointer_operands(void) {
    int  x   = 3;
    int *p   = &x;
    int *nul = 0;
    int  hit = 0;
    if (p && !nul)
        hit |= 1;
    if (nul || !p)
        hit |= 2;
    if (p && x)
        hit |= 4;
    AssertEq(hit, 5);
}

[[cccc::test]]
void test_wide_bitint_operands(void) {
    unsigned _BitInt(200) big  = 1;
    unsigned _BitInt(200) zero = 0;
    int hit                    = 0;
    if (big && !zero)
        hit |= 1;
    if (zero || !big)
        hit |= 2;
    if (!big)
        hit |= 4;
    if (big && big)
        hit |= 8;
    AssertEq(hit, 9);
}

[[cccc::test]]
void test_deep_chains_in_branch_and_value(void) {
    int a = 1, b = 2, c = 3, d = 4, e = 5, f = 6, g = 7, h = 8, i = 9, j = 10,
        k = 11, l = 12, m = 13;
    int hit = 0;
    if (a > 0 && b > 0 && c > 0 && d > 0 && e > 0 && f > 0 && g > 0 && h > 0 &&
        i > 0 && j > 0 && k > 0 && l > 0 && m > 0)
        hit = 1;
    AssertEq(hit, 1);
    if (!(a < 0 || b < 0 || c < 0 || d < 0 || e < 0 || f < 0 || g < 0 ||
          h < 0 || i < 0 || j < 0 || k < 0 || l < 0 || m < 0 || m > 99))
        hit += 1;
    AssertEq(hit, 2);
    int v = ((a ? b : c) && (d ? e : f) && (g ? h : i) && (j ? k : l)) &&
            ((a ? m : 0) || (b ? 0 : c));
    AssertEq(v, 1);
}
