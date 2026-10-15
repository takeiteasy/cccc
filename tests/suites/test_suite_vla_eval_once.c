// CCCC_FLAGS: --testing
// A VLA length expression runs exactly once, where its declarator is reached,
// in the VM and under -c=native.

#pragma cccc suite begin "vla_eval_once"

static int calls;
static int fx(void) { return ++calls; }
static int two(void) {
    calls++;
    return 2;
}

[[cccc::test]]
void test_one_dimension(void) {
    calls = 0;
    int a[fx() + 2];
    (void)a;
    AssertEq(calls, 1);
    AssertEq((int)sizeof(a), 3 * 4);
}

[[cccc::test]]
void test_two_dimensions(void) {
    calls = 0;
    int b[two()][two() + 1];
    AssertEq(calls, 2);
    AssertEq((int)sizeof(b), 2 * 3 * 4);
    b[1][2] = 7;
    AssertEq(b[1][2], 7);
}

[[cccc::test]]
void test_pointer_to_vla_with_initializer(void) {
    calls = 0;
    int buf[16] = {0};
    int (*p)[two()] = (int (*)[2])buf;
    p[1][1] = 9;
    AssertEq(calls, 1);
    AssertEq(buf[3], 9);
}

[[cccc::test]]
void test_pointer_to_vla_without_initializer(void) {
    calls = 0;
    int buf[16] = {0};
    int (*p)[two() + 1];
    p = (void *)buf;
    p[2][1] = 5;
    AssertEq(calls, 1);
    AssertEq(buf[7], 5);
    AssertEq((int)((char *)(p + 1) - (char *)p), 3 * 4);
}

[[cccc::test]]
void test_vla_in_loop(void) {
    calls = 0;
    for (int i = 0; i < 3; i++) {
        int v[fx()];
        (void)v;
    }
    AssertEq(calls, 3);
}

[[cccc::test]]
void test_sizeof_type(void) {
    calls = 0;
    int n = (int)sizeof(int[fx() + 1]);
    AssertEq(calls, 1);
    AssertEq(n, 2 * 4);
}

[[cccc::test]]
void test_typedef_evaluates_at_typedef(void) {
    calls = 0;
    typedef int T[fx() + 3];
    AssertEq(calls, 1);
    T t;
    T u;
    (void)t;
    (void)u;
    AssertEq(calls, 1);
    AssertEq((int)sizeof(T), 4 * 4);
    AssertEq((int)sizeof(t), 4 * 4);
}

[[cccc::test]]
void test_typedef_in_loop(void) {
    calls = 0;
    int total = 0;
    for (int i = 0; i < 3; i++) {
        typedef int T[fx()];
        T t;
        total += (int)(sizeof(t) / sizeof(int));
    }
    AssertEq(calls, 3);
    AssertEq(total, 1 + 2 + 3);
}

[[cccc::test]]
void test_typedef_size_fixed_at_typedef(void) {
    int n = 3;
    typedef int T[n];
    n = 5;
    T t;
    (void)t;
    AssertEq((int)sizeof(T), 3 * 4);
    AssertEq((int)sizeof(t), 3 * 4);
}

#pragma cccc suite end
