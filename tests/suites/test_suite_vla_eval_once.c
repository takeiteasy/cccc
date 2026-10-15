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

[[cccc::test]]
void test_cast_runs_length_once(void) {
    calls = 0;
    int   buf[8];
    void *q          = buf;
    int (*p)[fx() + 1] = (int (*)[fx() + 1])q;
    (void)p;
    AssertEq(calls, 2);
}

[[cccc::test]]
void test_cast_indexing_and_sizeof(void) {
    int buf[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    int n       = 3;
    void *q     = buf;
    AssertEq(((int (*)[n])q)[1][0], 3);
    AssertEq((int)sizeof(*(int (*)[n])q), 3 * 4);
    AssertEq((int)((char *)(((int (*)[n])q) + 1) - (char *)q), 3 * 4);
    AssertEq((int)(((int (*)[n])q + 2) - (int (*)[n])q), 2);
}

[[cccc::test]]
void test_cast_two_dimensions(void) {
    int buf[24];
    for (int i = 0; i < 24; i++)
        buf[i] = i;
    int   n = 2, m = 3;
    void *q = buf;
    calls   = 0;
    AssertEq(((int (*)[fx() + 1][m])q)[1][1][2], 11);
    AssertEq(calls, 1);
    AssertEq(((int (*)[n][m])q)[2][1][0], 15);
}

[[cccc::test]]
void test_cast_pointer_subtract_count(void) {
    int buf[12];
    int n   = 3;
    void *q = buf;
    calls   = 0;
    int (*end)[3] = (int (*)[3])buf + 3;
    AssertEq((int)(end - (int (*)[two() + 1])q), 3);
    AssertEq(calls, 1);
}

[[cccc::test]]
void test_compound_literal_runs_length_once(void) {
    int buf[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    void *q     = buf;
    calls       = 0;
    AssertEq((int (*)[fx() + 2]){q}[1][0], 3);
    AssertEq(calls, 1);
}

#pragma cccc suite end
