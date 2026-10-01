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
