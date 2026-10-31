// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: exercises the VM's bytecode clean-up
// A compare, and the branch on it, that the known range of its operands
// decides is folded; the arm that cannot run is dropped without changing what
// runs. Each function keeps `a` in a register by looping over it.

// Under `a < 5`, every test below is decided one way or the other.
[[cccc::test]] void signed_below(void) {
    int total = 0;
    for (int a = -3; a < 5; a++) {
        int r = 0;
        for (int k = 0; k < 2; k++) {
            if (a < 5) {
                if (a < 10) r += 1; else r += 100;
                if (a <= 4) r += 2; else r += 200;
                if (a >= 5) r += 300; else r += 4;
                if (a > 4) r += 500; else r += 8;
                if (a == 5) r += 700; else r += 16;
                if (a != 5) r += 32; else r += 900;
            }
        }
        total += r;
    }
    AssertEq(total, 8 * 2 * 63);
}

// Under `a >= 5` the same tests are decided the other way.
[[cccc::test]] void signed_above(void) {
    int total = 0;
    for (int a = 5; a < 9; a++) {
        int r = 0;
        for (int k = 0; k < 2; k++) {
            if (a >= 5) {
                if (a < 5) r += 100; else r += 1;
                if (a <= 4) r += 200; else r += 2;
                if (a >= 5) r += 4; else r += 300;
                if (a > 4) r += 8; else r += 500;
                if (a == 4) r += 700; else r += 16;
            }
        }
        total += r;
    }
    AssertEq(total, 4 * 2 * 31);
}

// A value that is always inside a mask cannot reach the bound.
[[cccc::test]] void masked(void) {
    int total = 0;
    for (int a = 0; a < 40; a++) {
        int m = a & 7;
        if (m > 20) total += 1000;
        if (m <= 7) total += 1;
        if (m == 9) total += 1000;
        if (m != 9) total += 1;
        if (m < 0) total += 1000;
    }
    AssertEq(total, 80);
}

// Unsigned orders are decided only when both sides are non-negative.
[[cccc::test]] void unsigned_orders(void) {
    int total = 0;
    for (unsigned a = 0; a < 6; a++) {
        if (a < 6u) {
            if (a < 10u) total += 1; else total += 100;
            if (a >= 6u) total += 100; else total += 2;
        }
    }
    AssertEq(total, 18);
    // A negative signed value is a huge unsigned one.
    int hits = 0;
    for (int a = -2; a < 0; a++) {
        if ((unsigned)a > 10u) hits++;
        if ((unsigned)a < 10u) hits += 100;
    }
    AssertEq(hits, 2);
    long long wide = 0;
    for (long long a = -2; a < 0; a++) {
        if ((unsigned long long)a > 10ull) wide++;
        if ((unsigned long long)a < 10ull) wide += 100;
    }
    AssertEq(wide, 2);
}

// The arm a decided branch removes holds a call, a label and a goto target.
int bump(int *p) { return ++*p; }

[[cccc::test]] void dead_arm_with_call(void) {
    int calls = 0;
    int total = 0;
    for (int a = 0; a < 4; a++) {
        if (a < 4) {
            if (a < 8) total += 1;
            else {
                bump(&calls);
                goto out;
            }
        }
    }
out:
    AssertEq(calls, 0);
    AssertEq(total, 4);
}

// A label whose address is taken stays reachable.
[[cccc::test]] void address_taken_label(void) {
    int x = 0;
    void *where = &&second;
    for (int a = 0; a < 1; a++) {
        if (a < 4) {
            if (a < 8) goto *where;
        }
        x += 100;
    }
first:
    x += 1;
second:
    x += 10;
    AssertEq(x, 10);
    (void)&&first;
}

// A switch on a value of known range.
[[cccc::test]] void switch_known_range(void) {
    int total = 0;
    for (int a = 0; a < 3; a++) {
        int m = a & 3;
        switch (m) {
            case 0: total += 1; break;
            case 1: total += 10; break;
            case 2: total += 100; break;
            case 3: total += 1000; break;
            default: total += 100000; break;
        }
    }
    AssertEq(total, 111);
}
