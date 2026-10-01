// CCCC_FLAGS: --testing -fopenmp
// Suite: #pragma omp with a real team under -c=native; the VM runs the same
// source on one thread. Expected values come from the team size, so the
// suite passes in both.

#include <omp.h>

#pragma cccc suite begin "openmp_threads"

static int team_size(void) {
    int nt = 1;
#pragma omp parallel num_threads(4)
    {
#pragma omp master
        nt = omp_get_num_threads();
    }
    return nt;
}

[[cccc::test(return = 42)]]
int test_omp_threads_reduction_operators(void) {
    long sum = 0, prod = 1;
    int  hi = -1, lo = 1000, bits = 0, mask = -1, all = 1, any = 0, x = 0;
#pragma omp parallel for num_threads(4) reduction(+ : sum) reduction(* : prod)
    for (int i = 1; i <= 100; i++) {
        sum += i;
        prod *= (i % 7 == 0) ? 1 : 1;
    }
#pragma omp parallel for num_threads(4) reduction(max : hi) reduction(min : lo)
    for (int i = 0; i < 100; i++) {
        if (i > hi)
            hi = i;
        if (i < lo)
            lo = i;
    }
#pragma omp parallel for num_threads(4) reduction(| : bits) reduction(& : mask)
    for (int i = 0; i < 16; i++) {
        bits |= 1 << i;
        mask &= ~(1 << i);
    }
#pragma omp parallel for num_threads(4) reduction(&& : all) reduction(|| : any) reduction(^ : x)
    for (int i = 0; i < 10; i++) {
        all = all && i < 10;
        any = any || i == 7;
        x ^= i;
    }
    return sum == 5050 && prod == 1 && hi == 99 && lo == 0 && bits == 0xffff &&
                   mask == ~0xffff && all && any && x == 1
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_omp_threads_data_sharing(void) {
    int p = 5, fp = 7, last = -1, i;
    int count[64] = {0};
#pragma omp parallel private(p) firstprivate(fp) num_threads(4)
    {
        p = omp_get_thread_num() + 100;
        fp += omp_get_thread_num();
        if (fp < 7)
            count[63] = 1;
    }
#pragma omp parallel for num_threads(4) lastprivate(i, last)
    for (i = 0; i < 37; i++)
        last = i * 3;
    return p == 5 && fp == 7 && count[63] == 0 && i == 37 && last == 108 ? 42
                                                                         : 1;
}

[[cccc::test(return = 42)]]
int test_omp_threads_critical_and_atomic(void) {
    int    n = 0, a = 0, ticket = 0, seen[256] = {0};
    double d = 0.0;
    long   big = 0;
    int    nt = team_size();
#pragma omp parallel num_threads(4)
    {
        for (int k = 0; k < 1000; k++) {
#pragma omp critical(counter)
            n++;
#pragma omp atomic
            a += 2;
#pragma omp atomic
            d += 0.5;
#pragma omp atomic
            big |= 1L << (omp_get_thread_num() + 8);
        }
        int mine;
#pragma omp atomic capture
        mine = ticket++;
        seen[mine & 255] = 1;
    }
    int distinct = 0;
    for (int i = 0; i < 256; i++)
        distinct += seen[i];
    return n == nt * 1000 && a == nt * 2000 && d == nt * 500.0 &&
                   distinct == nt && ticket == nt && (big >> 8) == (1L << nt) - 1
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_omp_threads_barrier_phases(void) {
    int phase1[8] = {0}, ok = 1;
#pragma omp parallel num_threads(4)
    {
        int me = omp_get_thread_num();
        phase1[me] = 1;
#pragma omp barrier
        int total = 0;
        for (int i = 0; i < omp_get_num_threads(); i++)
            total += phase1[i];
        if (total != omp_get_num_threads())
#pragma omp atomic write
            ok = 0;
    }
    return ok ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_threads_single_copyprivate(void) {
    int runs = 0, bad = 0;
#pragma omp parallel num_threads(4)
    {
        int v = 0;
#pragma omp single copyprivate(v)
        {
            runs++;
            v = 77;
        }
        if (v != 77)
#pragma omp atomic
            bad += 1;
    }
    return runs == 1 && bad == 0 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_threads_collapse(void) {
    long sum = 0;
#pragma omp parallel for collapse(2) num_threads(4) reduction(+ : sum)
    for (int i = 0; i < 13; i++)
        for (int j = 0; j < 7; j++)
            sum += i * 100 + j;
    long want = 0;
    for (int i = 0; i < 13; i++)
        for (int j = 0; j < 7; j++)
            want += i * 100 + j;
    return sum == want ? 42 : 1;
}

static void orphaned_sum(int *data, long *out) {
#pragma omp for
    for (int i = 0; i < 64; i++) {
#pragma omp atomic
        *out += data[i];
    }
}

[[cccc::test(return = 42)]]
int test_omp_threads_orphaned_for(void) {
    int  data[64];
    long out = 0;
    for (int i = 0; i < 64; i++)
        data[i] = i;
#pragma omp parallel num_threads(4)
    orphaned_sum(data, &out);
    return out == 2016 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_threads_nested_and_if(void) {
    int inner = -1, guarded = -1;
#pragma omp parallel num_threads(4)
    {
#pragma omp parallel num_threads(4)
        {
#pragma omp atomic write
            inner = omp_get_num_threads();
        }
    }
#pragma omp parallel num_threads(4) if (0)
    {
        guarded = omp_get_num_threads();
    }
    return inner == 1 && guarded == 1 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_threads_loop_forms(void) {
    int  up[50] = {0}, down[50] = {0};
    long step3 = 0;
#pragma omp parallel for num_threads(4)
    for (int i = 0; i < 50; i++)
        up[i] += 1;
#pragma omp parallel for num_threads(4)
    for (int i = 49; i >= 0; i--)
        down[i] += 1;
#pragma omp parallel for num_threads(4) reduction(+ : step3) schedule(static, 2)
    for (long i = 3; i <= 40; i += 3)
        step3 += i;
    int ok = 1;
    for (int i = 0; i < 50; i++)
        ok &= up[i] == 1 && down[i] == 1;
    return ok && step3 == 3 + 6 + 9 + 12 + 15 + 18 + 21 + 24 + 27 + 30 + 33 +
                              36 + 39
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_omp_threads_locks(void) {
    omp_lock_t lock;
    int        n = 0;
    omp_init_lock(&lock);
#pragma omp parallel num_threads(4)
    {
        for (int k = 0; k < 500; k++) {
            omp_set_lock(&lock);
            n++;
            omp_unset_lock(&lock);
        }
    }
    omp_destroy_lock(&lock);
    return n == team_size() * 500 ? 42 : 1;
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
