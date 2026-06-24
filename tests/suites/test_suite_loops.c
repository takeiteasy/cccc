// CCCC_FLAGS: --testing
// Consolidated suite: loops and control flow
// Source tests: test_break_continue, test_do_while, test_if_simple

#pragma cccc suite begin "loops"

// ── from test_if_simple ──
[[cccc::test]]
void test_if_basic(void) {
    int check = 42;
    Assert(check == 42);
}

// ── from test_break_continue ──
static int break_for(void) {
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        if (i == 5) break;
        sum += i;
    }
    return sum;  // 0+1+2+3+4 = 10
}
static int continue_for(void) {
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        if (i == 2 || i == 4) continue;
        sum += i;
    }
    return sum;  // 0+1+3+5+6+7+8+9 = 39
}
static int break_while(void) {
    int sum = 0, i = 0;
    while (i < 10) {
        if (i == 4) break;
        sum += i++;
    }
    return sum;  // 0+1+2+3 = 6
}
static int continue_while(void) {
    int sum = 0, i = 0;
    while (i < 6) {
        i++;
        if (i == 2 || i == 4) continue;
        sum += i;
    }
    return sum;  // 1+3+5+6 = 15
}
static int break_do_while(void) {
    int sum = 0, i = 0;
    do { if (i == 3) break; sum += i++; } while (i < 10);
    return sum;  // 0+1+2 = 3
}
static int continue_do_while(void) {
    int sum = 0, i = 0;
    do { i++; if (i == 2) continue; sum += i; } while (i < 4);
    return sum;  // 1+3+4 = 8
}
static int nested_break(void) {
    int result = 0;
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 5; j++) { result++; if (j == 2) break; }
    return result;  // 5*3 = 15
}
static int nested_continue(void) {
    int result = 0;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 5; j++) { if (j == 2) continue; result++; }
    return result;  // 3*4 = 12
}

[[cccc::test]]
void test_break_continue(void) {
    AssertEq(break_for(),        10);
    AssertEq(continue_for(),     39);
    AssertEq(break_while(),       6);
    AssertEq(continue_while(),   15);
    AssertEq(break_do_while(),    3);
    AssertEq(continue_do_while(), 8);
    AssertEq(nested_break(),     15);
    AssertEq(nested_continue(),  12);
}

// ── from test_do_while ──
static int basic_do_while(void) {
    int sum = 0, i = 0;
    do { sum += i++; } while (i < 5);
    return sum;  // 10
}
static int single_iteration(void) {
    int x = 0;
    do { x = 5; } while (0);
    return x;    // 5
}
static int do_while_countdown(void) {
    int count = 10, result = 0;
    do { result += count--; } while (count > 5);
    return result;  // 10+9+8+7+6 = 40
}
static int nested_do_while(void) {
    int outer = 0, total = 0;
    do {
        int inner = 0;
        do { total++; inner++; } while (inner < 2);
        outer++;
    } while (outer < 3);
    return total;  // 3*2 = 6
}

[[cccc::test]]
void test_do_while(void) {
    AssertEq(basic_do_while(),    10);
    AssertEq(single_iteration(),   5);
    AssertEq(do_while_countdown(), 40);
    AssertEq(nested_do_while(),    6);
}

#pragma cccc suite end
