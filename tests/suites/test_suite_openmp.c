// CCCC_NATIVE_SKIP: OpenMP runs in the VM only
// CCCC_FLAGS: --testing -fopenmp
// Suite: #pragma omp under -fopenmp runs every region on one thread with
// OpenMP's data-sharing semantics. It asserts the one-thread results, so it
// is VM-only; test_suite_openmp_threads.c covers a real team. Negative cases
// live in tests/test_openmp_*_error.c.

#include <omp.h>

#ifndef _OPENMP
#error expected _OPENMP under -fopenmp
#endif

#pragma cccc suite begin "openmp"

[[cccc::test(return = 42)]]
int test_openmp_reductions(void) {
    long sum = 0, prod = 1;
    int  hi = 3, lo = 3;
    int  all = 1, any = 0;
#pragma omp parallel for reduction(+ : sum) reduction(* : prod)
    for (int i = 1; i <= 5; i++) {
        sum += i;
        prod *= i;
    }
#pragma omp parallel for reduction(max : hi) reduction(min : lo)
    for (int i = 0; i < 10; i++) {
        if (i > hi)
            hi = i;
        if (i - 5 < lo)
            lo = i - 5;
    }
#pragma omp parallel for reduction(&& : all) reduction(|| : any)
    for (int i = 0; i < 4; i++) {
        all = all && i < 4;
        any = any || i == 2;
    }
    return sum == 15 && prod == 120 && hi == 9 && lo == -5 && all && any ? 42
                                                                         : 1;
}

[[cccc::test(return = 42)]]
int test_openmp_private_and_firstprivate(void) {
    int p = 5, fp = 7, arr[3] = {1, 2, 3};
#pragma omp parallel private(p) firstprivate(fp, arr)
    {
        p = 100;
        fp += 1;
        arr[0] = 50;
        if (fp != 8 || arr[1] != 2)
            p = -1;
    }
    return p == 5 && fp == 7 && arr[0] == 1 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_openmp_synchronisation_constructs(void) {
    int n = 0;
#pragma omp parallel
    {
#pragma omp critical(counter)
        n++;
#pragma omp barrier
#pragma omp single
        { n += 10; }
#pragma omp master
        n += 100;
#pragma omp atomic
        n += 1000;
    }
    return n == 1111 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_openmp_collapse_and_nested_for(void) {
    int n = 0;
#pragma omp parallel
    {
#pragma omp for collapse(2) nowait
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 4; j++)
                n++;
    }
    return n == 12 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_openmp_clauses_evaluated_once(void) {
    int calls = 0;
#pragma omp parallel num_threads((calls++, 4)) if (calls++ >= 0)
    { }
    return calls == 2 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_openmp_lastprivate(void) {
    int i, last = 0, both = 3, arr[2] = {0, 0};
#pragma omp parallel for lastprivate(i, last) firstprivate(both) lastprivate(both)
    for (i = 0; i < 10; i++) {
        last = i * 2;
        both += 1;
    }
#pragma omp parallel for lastprivate(arr)
    for (int j = 0; j < 4; j++)
        arr[j & 1] = j;
    return i == 10 && last == 18 && both == 13 && arr[0] == 2 && arr[1] == 3
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_openmp_single_copyprivate(void) {
    int x = 1;
#pragma omp parallel private(x)
    {
#pragma omp single copyprivate(x)
        { x = 9; }
    }
    return x == 1 ? 42 : 1;
}

#define PAR_FOR _Pragma("omp parallel for reduction(+ : total)")

[[cccc::test(return = 42)]]
int test_openmp_pragma_operator(void) {
    int total = 0;
    PAR_FOR for (int i = 0; i < 7; i++) total += i;
    return total == 21 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_openmp_runtime(void) {
    int ok = 1;
#pragma omp parallel
    {
        ok &= omp_get_num_threads() == 1 && omp_get_thread_num() == 0 &&
              !omp_in_parallel();
    }
    omp_lock_t lock;
    omp_init_lock(&lock);
    omp_set_lock(&lock);
    ok &= !omp_test_lock(&lock);
    omp_unset_lock(&lock);
    ok &= omp_test_lock(&lock) && omp_get_wtime() > 0 && omp_get_num_procs() > 0;
    return ok ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_default_none(void) {
    int  n = 8, i, r = 0;
    long total = 0;
    int  a[8]  = {0};
#pragma omp parallel default(none) shared(a, n) private(i)
    {
#pragma omp for
        for (i = 0; i < n; i++)
            a[i] = (int)sizeof(n) + i;
    }
#pragma omp parallel for default(none) shared(a, n) reduction(+ : total)
    for (int k = 0; k < n; k++)
        total += a[k];
#pragma omp parallel default(none) shared(r)
    {
        int n = 3;
#pragma omp single
        r = n;
    }
    return total == 4 * 8 + 28 && r == 3 ? 42 : 1;
}

static int omp_fib(int n) {
    if (n < 2)
        return n;
    int a, b;
#pragma omp task shared(a)
    a = omp_fib(n - 1);
#pragma omp task shared(b)
    b = omp_fib(n - 2);
#pragma omp taskwait
    return a + b;
}

[[cccc::test(return = 42)]]
int test_omp_tasks(void) {
    int x = 1, y = 5, g = 0, f = 0, z = 7, w = 0;
    // A task works on its own copies of variables it does not share.
#pragma omp task
    {
        x  = 2;
        y += x;
    }
#pragma omp task firstprivate(z) shared(w)
    {
        z++;
        w = z;
    }
#pragma omp taskwait
#pragma omp parallel shared(f) num_threads(4)
#pragma omp single
    f = omp_fib(10);
#pragma omp taskgroup
    {
#pragma omp task shared(g) if (1) final(0) priority(1) untied mergeable
        g = 3;
#pragma omp taskyield
    }
    return x == 1 && y == 5 && z == 7 && w == 8 && g == 3 && f == 55 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_task_in_region(void) {
    int outer = 10, inner_ok = 0, shared_seen = 0;
#pragma omp parallel num_threads(2) shared(outer, shared_seen, inner_ok)
    {
        int local = 5;
#pragma omp single
        {
#pragma omp task shared(outer, shared_seen)
            {
                outer++;
                shared_seen = 1;
            }
#pragma omp task
            {
                local = 99;
            }
#pragma omp taskwait
            inner_ok = local == 5;
        }
    }
    return outer == 11 && shared_seen && inner_ok ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_sections(void) {
    int sum = 0, last = 0, a = 0, b = 0, c = 0;
#pragma omp parallel sections num_threads(3) reduction(+ : sum)                \
    lastprivate(last)
    {
#pragma omp section
        {
            sum  += 1;
            last  = 1;
        }
#pragma omp section
        {
            sum  += 10;
            last  = 2;
        }
#pragma omp section
        {
            sum  += 100;
            last  = 3;
        }
    }
#pragma omp parallel num_threads(4)
    {
#pragma omp sections nowait
        {
            {
#pragma omp atomic
                a++;
            }
#pragma omp section
            {
#pragma omp atomic
                b++;
            }
#pragma omp section
            {
#pragma omp atomic
                c++;
            }
        }
    }
    return sum == 111 && last == 3 && a == 1 && b == 1 && c == 1 ? 42 : 1;
}

static int omp_tp = 1;
#pragma omp threadprivate(omp_tp)

[[cccc::test(return = 42)]]
int test_omp_threadprivate_copyin(void) {
    static int block_tp;
#pragma omp threadprivate(block_tp)
    int bad = 0, again = 0;
#pragma omp parallel num_threads(4)
    omp_tp = omp_get_thread_num() + 10;
#pragma omp parallel num_threads(4)
    {
        // Each thread finds the value it stored in the earlier region.
        if (omp_tp != omp_get_thread_num() + 10) {
#pragma omp atomic
            again++;
        }
    }
    omp_tp   = 77;
    block_tp = 5;
#pragma omp parallel copyin(omp_tp, block_tp) num_threads(4) default(none)     \
    shared(bad)
    {
        if (omp_tp != 77 || block_tp != 5) {
#pragma omp atomic
            bad++;
        }
        omp_tp = omp_get_thread_num();
    }
    return !bad && !again && omp_tp == 0 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_linear(void) {
    int  a[10], b[9], x = 100, z = 5, w = 7, y = 1, s = 0, count = 0;
    int *p = b;
    for (int i = 0; i < 9; i++)
        b[i] = 0;
#pragma omp parallel for linear(x : 2) num_threads(4)
    for (int i = 0; i < 10; i++)
        a[i] = x;
#pragma omp parallel for linear(z : 3) lastprivate(w) num_threads(2)
    for (int i = 0; i < 4; i++)
        w = z;
#pragma omp parallel for linear(p) num_threads(3)
    for (int i = 0; i < 9; i++)
        *p = i;
#pragma omp simd linear(y : 2)
    for (int i = 0; i < 5; i++)
        s += y;
#pragma omp parallel num_threads(4)
    {
        int stepn = 3, v = 10;
#pragma omp for linear(v : stepn)
        for (int i = 0; i < 6; i++)
            if (v != 10 + 3 * i) {
#pragma omp atomic
                count++;
            }
    }
    int ok = x == 118 && z == 14 && w == 14 && p == b + 8 && s == 25 &&
             y == 9 && !count;
    for (int i = 0; i < 10; i++)
        ok &= a[i] == 100 + 2 * i;
    for (int i = 0; i < 9; i++)
        ok &= b[i] == (i == 8 ? 8 : 0) || b[i] == 0 || b[i] == i;
    return ok ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_first_and_lastprivate_race(void) {
    int bad = 0;
    for (int rep = 0; rep < 300; rep++) {
        int v = 5;
#pragma omp parallel for firstprivate(v) lastprivate(v) num_threads(4)
        for (int i = 0; i < 7; i++) {
            // A chunk starts from the original value and then continues from
            // its own previous iteration.
            if (v != 5 && v != 1000 + i - 1) {
#pragma omp atomic
                bad++;
            }
            v = 1000 + i;
        }
        if (v != 1006)
            bad++;
    }
    return bad == 0 ? 42 : 1;
}

[[cccc::comptime]]
void gen_quote_omp_sum(void) {
    Obj *fn = MakeFunction("quote_omp_sum", GetType("int"));
    WithFn(fn) {
        FunctionSetBody(
            fn, Quote("int sum = 0;"
                      "_Pragma(\"omp parallel for reduction(+:sum)\")"
                      "for (int i = 0; i < 10; i++) sum += i;"
                      "return sum;"));
    }
}
gen_quote_omp_sum();

[[cccc::test(return = 42)]]
int test_omp_quote_pragma_operator(void) {
    return quote_omp_sum() == 45 ? 42 : 1;
}

#pragma cccc suite end
