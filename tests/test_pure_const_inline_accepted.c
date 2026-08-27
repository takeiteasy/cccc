// The VM has no bytecode optimiser, so [[gnu::pure]] / [[gnu::const]] /
// __attribute__((pure|const)) are parsed and then ignored (no dead-call
// elimination or CSE), and `static inline` callees are always emitted as an
// ordinary CALL. This test locks in that all three still parse without error
// and compute correct results -- the semantics-only remainder of the old
// test_suite_optimizer.c after the optimiser was removed.

static __attribute__((pure)) int attr_pure(int x) {
    return x - 1;
}
static __attribute__((const)) int attr_const(int x) {
    return x * x;
}
static [[gnu::pure]] int gnu_pure(int x) {
    return x + 10;
}
static [[gnu::const]] int gnu_const(int x) {
    return x * 3;
}

static inline int inl_add(int a, int b) {
    return a + b;
}
static inline int inl_fact(int n) {
    // A recursive static inline: must simply fall back to CALL and still work.
    if (n <= 1)
        return 1;
    return n * inl_fact(n - 1);
}

int main(void) {
    if (attr_pure(9) != 8)
        return 1;
    if (attr_const(4) != 16)
        return 2;
    if (gnu_pure(5) != 15)
        return 3;
    if (gnu_const(5) != 15)
        return 4;
    if (inl_add(20, 22) != 42)
        return 5;
    if (inl_fact(5) != 120)
        return 6;

    // Result-unused pure/const calls: nothing is elided, and (trivially) the
    // argument side effect still runs.
    int n = 3;
    attr_pure(++n);
    attr_const(++n);
    if (n != 5)
        return 7;

    return 42;
}
