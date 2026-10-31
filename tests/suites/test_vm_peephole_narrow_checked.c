// CCCC_FLAGS: --testing -B
// CCCC_NATIVE_SKIP: exercises the VM's bytecode clean-up
// The same cases under bounds checks, where the clean-up is off.

// Every test of `i` is decided: `i` is within [0, 4].
[[cccc::test]] void counter_up(void) {
    int r = 0;
    for (int i = 0; i < 5; i++) {
        if (i < 10)
            r += 1;
        else
            r += 100;
        if (i <= 4)
            r += 2;
        else
            r += 200;
        if (i >= 5)
            r += 300;
        else
            r += 4;
        if (i > 4)
            r += 500;
        else
            r += 8;
        if (i == 7)
            r += 700;
        else
            r += 16;
        if (i != 7)
            r += 32;
        else
            r += 900;
    }
    AssertEq(r, 5 * 63);
}

[[cccc::test]] void counter_down(void) {
    int r = 0;
    for (int i = 4; i >= 0; i--) {
        if (i >= 0)
            r += 1;
        else
            r += 100;
        if (i < 5)
            r += 2;
        else
            r += 200;
        if (i > 4)
            r += 300;
        else
            r += 4;
        if (i < -1)
            r += 500;
        else
            r += 8;
    }
    AssertEq(r, 5 * 15);
}

// The inner counter stays below the outer one, which stays below 5.
[[cccc::test]] void nested(void) {
    int r = 0;
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < i; j++) {
            if (j < 4)
                r += 1;
            else
                r += 100;
            if (j <= i)
                r += 2;
            else
                r += 1000;
        }
    }
    AssertEq(r, 10 * 3);
}

[[cccc::test]] void unsigned_counter(void) {
    int r = 0;
    for (unsigned u = 0; u < 5; u++) {
        if (u < 10u)
            r += 1;
        else
            r += 100;
        if (u >= 5u)
            r += 300;
        else
            r += 4;
    }
    AssertEq(r, 5 * 5);
}

// A step of two stops at 8: `i >= 8` is true once and `i == 9` never, and
// neither test may be decided the wrong way.
[[cccc::test]] void step_of_two(void) {
    int r = 0;
    for (int i = 0; i < 10; i += 2) {
        if (i < 10)
            r += 1;
        else
            r += 100;
        if (i >= 8)
            r += 10;
        if (i == 9)
            r += 1000;
    }
    AssertEq(r, 5 + 10);
}

// A bound that is only known at run time leaves the counter at least 0.
static int sum_below(int n) {
    int r = 0;
    for (int i = 0; i < n; i++) {
        if (i < 0)
            r += 1000;
        else
            r += 1;
    }
    return r;
}

[[cccc::test]] void runtime_bound(void) {
    AssertEq(sum_below(0), 0);
    AssertEq(sum_below(3), 3);
    AssertEq(sum_below(-4), 0);
}

[[cccc::test]] void early_break(void) {
    int r = 0;
    for (int i = 0; i < 100; i++) {
        if (i == 7)
            break;
        if (i < 7)
            r += 1;
        else
            r += 1000;
    }
    AssertEq(r, 7);
}

// The counter is also changed after the test that bounds it.
[[cccc::test]] void bumped_in_body(void) {
    int r = 0;
    for (int i = 0; i < 5; i++) {
        i += 1;
        if (i < 6)
            r += 1;
        else
            r += 100;
        if (i == 5)
            r += 10;
    }
    AssertEq(r, 3 + 10);
}
