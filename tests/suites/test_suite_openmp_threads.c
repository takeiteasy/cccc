// CCCC_FLAGS: --testing -fopenmp
// Suite: #pragma omp with a real team under -c=native; the VM runs the same
// source on one thread. Expected values come from the team size, so the
// suite passes in both.

#include <omp.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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
int test_omp_task_nested_parallel_firstprivate(void) {
    int x = 5, seen = 0;
#pragma omp task shared(seen)
    {
#pragma omp parallel num_threads(2) shared(seen)
        {
#pragma omp single
            {
                seen = x;
                x = 99;
            }
        }
    }
#pragma omp taskwait
    return x == 5 && seen == 5 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_task_two_nested_parallels(void) {
    int x = 7, seen = 0;
#pragma omp task shared(seen)
    {
#pragma omp parallel num_threads(1) shared(seen)
        {
#pragma omp parallel num_threads(2) shared(seen)
            {
#pragma omp single
                {
                    seen = x;
                    x = 99;
                }
            }
        }
    }
#pragma omp taskwait
    return x == 7 && seen == 7 ? 42 : 1;
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

[[cccc::test(return = 42)]]
int test_omp_sections_claimed_dynamically(void) {
    // With two threads a static split would give thread 0 sections 0 and 1, so
    // section 0 would wait on section 1 forever; claiming one at a time lets
    // the other thread run 1 to 3.
    int teamed = 0, done = 0, stuck = 0;
#pragma omp parallel sections num_threads(2) shared(teamed, done, stuck)
    {
#pragma omp section
        {
            teamed = omp_get_num_threads() > 1;
            for (long spin = 0;
                 teamed && __atomic_load_n(&done, __ATOMIC_ACQUIRE) < 3; spin++)
                if (spin > 2000000000L) {
                    stuck = 1;
                    break;
                }
        }
#pragma omp section
        __atomic_fetch_add(&done, 1, __ATOMIC_ACQ_REL);
#pragma omp section
        __atomic_fetch_add(&done, 1, __ATOMIC_ACQ_REL);
#pragma omp section
        __atomic_fetch_add(&done, 1, __ATOMIC_ACQ_REL);
    }
    return !stuck && done == 3 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_clause_expressions_default_none(void) {
    int n = 4, s = 0, t = 0, u = 0, ok = 1;
#pragma omp parallel for default(none) shared(s) num_threads(n) reduction(+ : s)
    for (int i = 0; i < 8; i++)
        s += i;
    ok &= s == 28;
#pragma omp parallel default(none) shared(t) if (n > 1) num_threads(n)
    {
#pragma omp atomic
        t++;
    }
    ok &= t >= 1 && t <= n;
#pragma omp parallel sections default(none) shared(u) num_threads(n)
    {
#pragma omp section
        {
#pragma omp atomic
            u++;
        }
#pragma omp section
        {
#pragma omp atomic
            u++;
        }
    }
    ok &= u == 2;
    return ok ? 42 : 1;
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

static void spin(int n) {
    volatile int sink = 0;
    for (int k = 0; k < n * 2000; k++)
        sink += k;
}

static int is_sequence(const int *a, int n) {
    for (int i = 0; i < n; i++)
        if (a[i] != i)
            return 0;
    return 1;
}

[[cccc::test(return = 42)]]
int test_omp_ordered_loop(void) {
    enum { N = 64 };
    int out[N], pos = 0;
#pragma omp parallel for ordered num_threads(4)
    for (int i = 0; i < N; i++) {
        spin(N - i);
#pragma omp ordered
        out[pos++] = i;
    }
    return pos == N && is_sequence(out, N) ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_ordered_skipped_iterations(void) {
    enum { N = 90 };
    int out[N], pos = 0;
#pragma omp parallel for ordered num_threads(4)
    for (int i = 0; i < N; i++) {
        spin(N - i);
        if (i % 3 == 0) {
#pragma omp ordered
            out[pos++] = i / 3;
        }
    }
    return pos == N / 3 && is_sequence(out, N / 3) ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_ordered_fewer_iterations_than_threads(void) {
    int out[4], pos = 0;
#pragma omp parallel for ordered num_threads(4)
    for (int i = 0; i < 2; i++) {
#pragma omp ordered
        out[pos++] = i;
    }
    return pos == 2 && is_sequence(out, 2) ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_ordered_back_to_back_loops(void) {
    enum { N = 40 };
    int a[N], b[N], pa = 0, pb = 0;
#pragma omp parallel num_threads(4)
    {
#pragma omp for ordered nowait
        for (int i = 0; i < N; i++) {
            spin(N - i);
#pragma omp ordered
            a[pa++] = i;
        }
#pragma omp for ordered
        for (int i = 0; i < N; i++) {
            spin(N - i);
#pragma omp ordered
            b[pb++] = i;
        }
    }
    return pa == N && pb == N && is_sequence(a, N) && is_sequence(b, N) ? 42
                                                                       : 1;
}

[[cccc::test(return = 42)]]
int test_omp_ordered_collapse(void) {
    enum { R = 6, C = 7 };
    int out[R * C], pos = 0;
#pragma omp parallel for ordered collapse(2) num_threads(4)
    for (int i = 0; i < R; i++)
        for (int j = 0; j < C; j++) {
            spin(R * C - (i * C + j));
#pragma omp ordered
            out[pos++] = i * C + j;
        }
    return pos == R * C && is_sequence(out, R * C) ? 42 : 1;
}

static int  orphan_out[64], orphan_pos;
static void orphan_record(int i) {
#pragma omp ordered
    orphan_out[orphan_pos++] = i;
}

[[cccc::test(return = 42)]]
int test_omp_ordered_orphaned(void) {
    orphan_pos = 0;
#pragma omp parallel for ordered num_threads(4)
    for (int i = 0; i < 64; i++) {
        spin(64 - i);
        orphan_record(i);
    }
    return orphan_pos == 64 && is_sequence(orphan_out, 64) ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_ordered_nested_team_of_one(void) {
    enum { N = 32 };
    int out[N], pos = 0, inner = 0;
#pragma omp parallel for ordered num_threads(4)
    for (int i = 0; i < N; i++) {
        spin(N - i);
#pragma omp parallel num_threads(3)
        {
#pragma omp critical(inner_count)
            inner++;
        }
#pragma omp ordered
        out[pos++] = i;
    }
    return pos == N && is_sequence(out, N) && inner >= N ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_ordered_with_reduction_and_lastprivate(void) {
    enum { N = 48 };
    int  out[N], pos = 0, last = -1;
    long sum = 0;
#pragma omp parallel for ordered num_threads(4) reduction(+ : sum) lastprivate(last)
    for (int i = 0; i < N; i++) {
        spin(N - i);
#pragma omp ordered
        out[pos++] = i;
        sum += i;
        last = i;
    }
    return pos == N && is_sequence(out, N) && sum == N * (N - 1) / 2 &&
                   last == N - 1
               ? 42
               : 1;
}

#define PRAGMA(...) _Pragma(#__VA_ARGS__)

enum { COVER_MAX = 1100 };
static int cover_hits[COVER_MAX];

static int cover_ok(int n, long sum) {
    for (int i = 0; i < n; i++)
        if (cover_hits[i] != 1)
            return 0;
    return sum == (long)n * (n - 1) / 2;
}

#define COVER_FN(NAME, CLAUSE)                                                 \
    static int NAME(int n) {                                                   \
        memset(cover_hits, 0, sizeof cover_hits);                              \
        long sum = 0;                                                          \
        PRAGMA(omp parallel for num_threads(4) CLAUSE reduction(+ : sum))      \
        for (int i = 0; i < n; i++) {                                          \
            cover_hits[i]++;                                                   \
            sum += i;                                                          \
        }                                                                      \
        return cover_ok(n, sum);                                               \
    }

COVER_FN(cover_none, )
COVER_FN(cover_static, schedule(static))
COVER_FN(cover_static3, schedule(static, 3))
COVER_FN(cover_dynamic, schedule(dynamic))
COVER_FN(cover_dynamic4, schedule(dynamic, 4))
COVER_FN(cover_guided, schedule(guided))
COVER_FN(cover_guided7, schedule(guided, 7))
COVER_FN(cover_auto, schedule(auto))
COVER_FN(cover_modifiers, schedule(nonmonotonic : dynamic, 2))
COVER_FN(cover_runtime, schedule(runtime))

[[cccc::test(return = 42)]]
int test_omp_schedule_kinds_cover_every_iteration(void) {
    static const int sizes[] = {0, 1, 3, 4, 7, 64, 1003};
    for (int k = 0; k < 7; k++) {
        int n = sizes[k];
        if (!cover_none(n) || !cover_static(n) || !cover_static3(n) ||
            !cover_dynamic(n) || !cover_dynamic4(n) || !cover_guided(n) ||
            !cover_guided7(n) || !cover_auto(n) || !cover_modifiers(n) ||
            !cover_runtime(n))
            return 100 + k;
    }
    return 42;
}

[[cccc::test(return = 42)]]
int test_omp_schedule_stepped_and_collapsed_loops(void) {
    long down = 0, guided = 0, chunked = 0;
#pragma omp parallel for num_threads(4) schedule(dynamic, 3) reduction(+ : down)
    for (int i = 100; i > 0; i -= 7)
        down += i;
#pragma omp parallel for collapse(2) num_threads(4) schedule(guided, 2)        \
    reduction(+ : guided)
    for (int i = 0; i < 9; i++)
        for (int j = 0; j < 11; j++)
            guided += i * 100 + j;
#pragma omp parallel for collapse(2) num_threads(4) schedule(static, 5)        \
    reduction(+ : chunked)
    for (int i = 0; i < 9; i++)
        for (int j = 0; j < 11; j++)
            chunked += i * 100 + j;
    return down == 765 && guided == 40095 && chunked == 40095 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_schedule_ordered(void) {
    enum { N = 64 };
    int out[N], pos = 0, ok = 1;
#pragma omp parallel for ordered schedule(static, 1) num_threads(4)
    for (int i = 0; i < N; i++) {
        spin(N - i);
#pragma omp ordered
        out[pos++] = i;
    }
    ok  &= pos == N && is_sequence(out, N);
    pos  = 0;
#pragma omp parallel for ordered schedule(dynamic, 2) num_threads(4)
    for (int i = 0; i < N; i++) {
        spin(N - i);
#pragma omp ordered
        out[pos++] = i;
    }
    ok  &= pos == N && is_sequence(out, N);
    pos  = 0;
#pragma omp parallel for ordered schedule(guided) num_threads(4)
    for (int i = 0; i < N; i++) {
        spin(N - i);
#pragma omp ordered
        out[pos++] = i;
    }
    ok  &= pos == N && is_sequence(out, N);
    pos  = 0;
#pragma omp parallel for ordered schedule(dynamic, 3) num_threads(4)
    for (int i = 0; i < N; i++) {
        spin(N - i);
        if (i % 3 == 0) {
#pragma omp ordered
            out[pos++] = i;
        }
    }
    int seen = (N + 2) / 3;
    for (int i = 0; i < seen; i++)
        ok &= out[i] == i * 3;
    ok         &= pos == seen;
    pos         = 0;
    orphan_pos  = 0;
#pragma omp parallel for ordered schedule(static, 2) num_threads(4)
    for (int i = 0; i < 64; i++) {
        spin(64 - i);
        orphan_record(i);
    }
    ok &= orphan_pos == 64 && is_sequence(orphan_out, 64);
    return ok ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_schedule_lastprivate_and_linear(void) {
    enum { N = 50 };
    int a[N], last = -1, x = 100, ok = 1;
#pragma omp parallel for schedule(dynamic, 3) lastprivate(last) linear(x : 2)  \
    num_threads(4)
    for (int i = 0; i < N; i++) {
        a[i] = x;
        last = i;
    }
    for (int i = 0; i < N; i++)
        ok &= a[i] == 100 + 2 * i;
    ok   &= last == N - 1 && x == 100 + 2 * (N - 1);
    last  = -1;
#pragma omp parallel for schedule(guided) lastprivate(last) num_threads(4)
    for (int i = 0; i < N; i++)
        last = i;
    ok   &= last == N - 1;
    last  = -1;
#pragma omp parallel for schedule(static, 4) lastprivate(last) num_threads(4)
    for (int i = 0; i < N; i++)
        last = i;
    ok &= last == N - 1;
    return ok ? 42 : 1;
}

static int back_hits[4][200];

[[cccc::test(return = 42)]]
int test_omp_schedule_back_to_back_nowait_loops(void) {
    enum { N = 200 };
    memset(back_hits, 0, sizeof back_hits);
#pragma omp parallel num_threads(4)
    {
#pragma omp for schedule(dynamic, 3) nowait
        for (int i = 0; i < N; i++)
            back_hits[0][i]++;
#pragma omp for schedule(guided) nowait
        for (int i = 0; i < N; i++)
            back_hits[1][i]++;
#pragma omp for schedule(static)
        for (int i = 0; i < N; i++)
            back_hits[2][i]++;
#pragma omp for schedule(dynamic) nowait
        for (int i = 0; i < N; i++)
            back_hits[3][i]++;
    }
    for (int l = 0; l < 4; l++)
        for (int i = 0; i < N; i++)
            if (back_hits[l][i] != 1)
                return 10 + l;
    return 42;
}

[[cccc::test(return = 42)]]
int test_omp_schedule_dynamic_balances_work(void) {
    enum { N = 64 };
    int done = 0, nt = team_size(), first_waited = 0;
    // Iteration 0 waits for all the others. Under a static schedule the
    // iterations after it in its own chunk could never run.
#pragma omp parallel for num_threads(4) schedule(dynamic, 1)
    for (int i = 0; i < N; i++) {
        if (i == 0 && nt > 1) {
            while (__atomic_load_n(&done, __ATOMIC_ACQUIRE) < N - 1)
                spin(1);
            first_waited = 1;
        } else {
            __atomic_fetch_add(&done, 1, __ATOMIC_ACQ_REL);
        }
    }
    return done == (nt > 1 ? N - 1 : N) && first_waited == (nt > 1) ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_schedule_chunk_expression(void) {
    volatile int cv = 3;
    int          c = cv, ok = 1;
    long         sum = 0, csum = 0;
    memset(cover_hits, 0, sizeof cover_hits);
#pragma omp parallel for num_threads(4) schedule(dynamic, c) private(c)        \
    reduction(+ : sum)
    for (int i = 0; i < 100; i++) {
        cover_hits[i]++;
        sum += i;
    }
    ok &= cover_ok(100, sum);
    memset(cover_hits, 0, sizeof cover_hits);
#pragma omp parallel for num_threads(4) schedule(guided, c + 1)                \
    firstprivate(c) reduction(+ : csum)
    for (int i = 0; i < 100; i++) {
        cover_hits[i]++;
        csum += c;
    }
    ok            &= csum == 300;
    int local[64]  = {0};
#pragma omp parallel for num_threads(4) default(none) shared(local)            \
    schedule(static, c)
    for (int i = 0; i < 64; i++)
        local[i]++;
    for (int i = 0; i < 64; i++)
        ok &= local[i] == 1;
    return ok ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_schedule_runtime(void) {
    omp_sched_t kind;
    int         chunk, ok = 1;
    omp_set_schedule(omp_sched_dynamic, 2);
    omp_get_schedule(&kind, &chunk);
    ok &= kind == omp_sched_dynamic && chunk == 2;
    ok &= cover_runtime(1003);
    omp_set_schedule(omp_sched_guided, 5);
    omp_get_schedule(&kind, &chunk);
    ok &= kind == omp_sched_guided && chunk == 5;
    ok &= cover_runtime(1003);
    omp_set_schedule(omp_sched_static, 4);
    ok &= cover_runtime(1003);
    omp_set_schedule(omp_sched_auto, 0);
    omp_get_schedule(&kind, &chunk);
    ok &= kind == omp_sched_auto;
    ok &= cover_runtime(1003);
    omp_set_schedule(omp_sched_static, 0);
    return ok ? 42 : 1;
}

enum { CRIT_NAMES = 70, CRIT_REPS = 200 };
static int crit_cnt[CRIT_NAMES];
static int crit_shared;

#define CRIT(N) PRAGMA(omp critical(crit_##N)) crit_cnt[N]++;
#define CRIT10(T)                                                              \
    CRIT(T##0)                                                                 \
    CRIT(T##1) CRIT(T##2) CRIT(T##3) CRIT(T##4) CRIT(T##5) CRIT(T##6)          \
        CRIT(T##7) CRIT(T##8) CRIT(T##9)

[[cccc::test(return = 42)]]
int test_omp_critical_many_names(void) {
    int nt = team_size();
    memset(crit_cnt, 0, sizeof crit_cnt);
    crit_shared = 0;
#pragma omp parallel num_threads(4)
    {
        for (int rep = 0; rep < CRIT_REPS; rep++) {
            CRIT(0)
            CRIT(1) CRIT(2) CRIT(3) CRIT(4) CRIT(5) CRIT(6) CRIT(7) CRIT(8)
                CRIT(9) CRIT10(1) CRIT10(2) CRIT10(3) CRIT10(4) CRIT10(5)
                    CRIT10(6)
#pragma omp critical(shared_name)
                        crit_shared++;
            spin(1);
#pragma omp critical(shared_name)
            crit_shared++;
        }
    }
    for (int i = 0; i < CRIT_NAMES; i++)
        if (crit_cnt[i] != nt * CRIT_REPS)
            return 100 + i;
    return crit_shared == 2 * nt * CRIT_REPS ? 42 : 1;
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

[[cccc::test(return = 42)]]
int test_omp_for_continue(void) {
    int sum = 0;
#pragma omp parallel for reduction(+ : sum) num_threads(4)
    for (int i = 0; i < 100; i++) {
        if (i % 2)
            continue;
        sum += i;
    }
    return sum == 2450 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_for_continue_in_user_loop(void) {
    int sum = 0;
    for (int r = 0; r < 3; r++) {
#pragma omp parallel num_threads(4)
        {
#pragma omp for reduction(+ : sum) schedule(dynamic, 3)
            for (int i = 0; i < 40; i++) {
                if (i == 7 || i == 20)
                    continue;
                sum += 1;
            }
        }
    }
    return sum == 3 * 38 ? 42 : 1;
}

// Doacross: ordered(n) with sink/source. The early iterations sleep longest, so
// a missing wait gives a wrong chain.
#define DOACROSS_PREFIX(name, sched)                                           \
    [[cccc::test(return = 42)]]                                                \
    int name(void) {                                                           \
        enum { N = 64 };                                                       \
        long a[N + 1] = {1};                                                   \
        _Pragma(sched) for (int i = 1; i <= N; i++) {                          \
            spin(N - i);                                                       \
            _Pragma("omp ordered depend(sink: i - 1)") a[i] = a[i - 1] + 1;    \
            _Pragma("omp ordered depend(source)")                              \
        }                                                                      \
        return a[N] == N + 1 ? 42 : 1;                                         \
    }

DOACROSS_PREFIX(test_omp_doacross_static,
                "omp parallel for ordered(1) num_threads(4)")
DOACROSS_PREFIX(
    test_omp_doacross_static_chunk,
    "omp parallel for ordered(1) num_threads(4) schedule(static, 3)")
DOACROSS_PREFIX(
    test_omp_doacross_static_one,
    "omp parallel for ordered(1) num_threads(4) schedule(static, 1)")
DOACROSS_PREFIX(
    test_omp_doacross_dynamic,
    "omp parallel for ordered(1) num_threads(4) schedule(dynamic, 2)")
DOACROSS_PREFIX(test_omp_doacross_guided,
                "omp parallel for ordered(1) num_threads(4) schedule(guided)")
DOACROSS_PREFIX(
    test_omp_doacross_guided_chunk3,
    "omp parallel for ordered(1) num_threads(4) schedule(guided, 3)")
DOACROSS_PREFIX(
    test_omp_doacross_guided_chunk7,
    "omp parallel for ordered(1) num_threads(4) schedule(guided, 7)")

static void wavefront_reference(int r, int c, int *g) {
    for (int i = 0; i < r; i++)
        for (int j = 0; j < c; j++)
            g[i * c + j] =
                (i ? g[(i - 1) * c + j] : 0) + (j ? g[i * c + j - 1] : 0) + 1;
}

[[cccc::test(return = 42)]]
int test_omp_doacross_wavefront(void) {
    enum { R = 9, C = 7 };
    int g[R * C] = {0}, ref[R * C];
    wavefront_reference(R, C, ref);
#pragma omp parallel for ordered(2) num_threads(4) schedule(static, 1)
    for (int i = 0; i < R; i++)
        for (int j = 0; j < C; j++) {
            spin(R * C - (i * C + j));
#pragma omp ordered depend(sink : i - 1, j) depend(sink : i, j - 1)
            g[i * C + j] =
                (i ? g[(i - 1) * C + j] : 0) + (j ? g[i * C + j - 1] : 0) + 1;
#pragma omp ordered depend(source)
        }
    return memcmp(g, ref, sizeof g) == 0 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_doacross_collapse(void) {
    enum { R = 6, C = 8 };
    int g[R * C] = {0}, ref[R * C];
    wavefront_reference(R, C, ref);
#pragma omp parallel for collapse(2) ordered(2) num_threads(4) schedule(dynamic)
    for (int i = 0; i < R; i++)
        for (int j = 0; j < C; j++) {
            spin(R * C - (i * C + j));
#pragma omp ordered depend(sink : i - 1, j) depend(sink : i, j - 1)
            g[i * C + j] =
                (i ? g[(i - 1) * C + j] : 0) + (j ? g[i * C + j - 1] : 0) + 1;
#pragma omp ordered depend(source)
        }
    return memcmp(g, ref, sizeof g) == 0 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_doacross_guided_collapse(void) {
    enum { R = 13, C = 5 };
    int g[R * C] = {0}, ref[R * C];
    wavefront_reference(R, C, ref);
#pragma omp parallel for collapse(2) ordered(2) num_threads(4)                 \
    schedule(guided, 2)
    for (int i = 0; i < R; i++)
        for (int j = 0; j < C; j++) {
            spin(R * C - (i * C + j));
#pragma omp ordered depend(sink : i - 1, j) depend(sink : i, j - 1)
            g[i * C + j] =
                (i ? g[(i - 1) * C + j] : 0) + (j ? g[i * C + j - 1] : 0) + 1;
#pragma omp ordered depend(source)
        }
    return memcmp(g, ref, sizeof g) == 0 ? 42 : 1;
}

// Threads waiting on a slow iteration sleep instead of spinning: spinning
// would cost about 3 * 300 ms of CPU time.
[[cccc::test(return = 42)]]
int test_omp_doacross_wait_sleeps(void) {
    enum { N = 8 };
    long    a[N + 1] = {1};
    clock_t t0       = clock();
#pragma omp parallel for ordered(1) num_threads(4) schedule(static, 1)
    for (int i = 1; i <= N; i++) {
        if (i == 1)
            usleep(300000);
#pragma omp ordered depend(sink : i - 1)
        a[i] = a[i - 1] + 1;
#pragma omp ordered depend(source)
    }
    double cpu_ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
    return a[N] == N + 1 && cpu_ms < 150.0 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_doacross_5_2_spelling(void) {
    enum { N = 48 };
    long a[N + 1] = {1}, b[N + 1] = {1};
#pragma omp parallel for ordered(1) num_threads(4) schedule(static, 2)
    for (int i = 1; i <= N; i++) {
        spin(N - i);
#pragma omp ordered doacross(sink : i - 1)
        a[i] = a[i - 1] + 1;
#pragma omp ordered doacross(source:)
    }
#pragma omp parallel for ordered(1) num_threads(4) schedule(dynamic)
    for (int i = 1; i <= N; i++) {
        spin(N - i);
#pragma omp ordered doacross(sink : omp_cur_iteration - 1)
        b[i] = b[i - 1] + 2;
#pragma omp ordered doacross(source : omp_cur_iteration)
    }
    return a[N] == N + 1 && b[N] == 2 * N + 1 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_doacross_down_step(void) {
    long a[36] = {0};
    a[34]      = 1;
#pragma omp parallel for ordered(1) num_threads(4) schedule(static, 1)
    for (int i = 32; i >= 0; i -= 2) {
        spin(i);
#pragma omp ordered depend(sink : i + 2)
        a[i] = a[i + 2] + 1;
#pragma omp ordered depend(source)
    }
    return a[0] == 18 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_doacross_continue(void) {
    enum { N = 60 };
    long a[N + 1] = {0}, ref[N + 1] = {0};
    for (int i = 1; i <= N; i++)
        if (i % 3)
            ref[i] = ref[i - 1] + 1;
#pragma omp parallel for ordered(1) num_threads(4) schedule(static, 2)
    for (int i = 1; i <= N; i++) {
        spin(N - i);
        if (i % 3 == 0)
            continue;
#pragma omp ordered depend(sink : i - 1)
        a[i] = a[i - 1] + 1;
#pragma omp ordered depend(source)
    }
    return memcmp(a, ref, sizeof a) == 0 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_doacross_back_to_back_loops(void) {
    enum { N = 40 };
    long a[N + 1] = {1}, b[N + 1] = {1};
#pragma omp parallel              num_threads(4)
    {
#pragma omp for ordered(1) nowait schedule(static, 1)
        for (int i = 1; i <= N; i++) {
            spin(N - i);
#pragma omp ordered depend(sink : i - 1)
            a[i] = a[i - 1] + 1;
#pragma omp ordered depend(source)
        }
#pragma omp for ordered(1) schedule(dynamic)
        for (int i = 1; i <= N; i++) {
            spin(N - i);
#pragma omp ordered depend(sink : i - 1)
            b[i] = b[i - 1] + 3;
#pragma omp ordered depend(source)
        }
    }
    return a[N] == N + 1 && b[N] == 3 * N + 1 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_threads_pointer_loop(void) {
    int a[16] = {0}, b[16] = {0}, c[4][8] = {0}, d[16] = {0};
#pragma omp parallel for num_threads(4)
    for (int *p = a; p < a + 16; p++)
        *p += 1;
#pragma omp parallel for num_threads(4)
    for (int *p = b + 15; p >= b; p -= 3)
        *p += 1;
    int *q;
    int *end = 0;
#pragma omp parallel for num_threads(4) lastprivate(q)
    for (q = d; q != d + 16; q += 2)
        *q += 1;
    end = q;
    int *row = c[0];
#pragma omp parallel for collapse(2) num_threads(4)
    for (int i = 0; i < 4; i++)
        for (int *p = row; p < row + 8; p++)
            c[i][p - row] = i + 1;
    int ok = 1;
    for (int i = 0; i < 16; i++) {
        ok &= a[i] == 1;
        ok &= b[i] == (i % 3 == 0);
        ok &= d[i] == (i % 2 == 0);
    }
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++)
            ok &= c[i][j] == i + 1;
    return ok && end == d + 16 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_threads_pointer_loop_doacross(void) {
    enum { N = 40 };
    long a[N + 1] = {1};
#pragma omp parallel num_threads(4)
    {
#pragma omp for ordered(1) schedule(static, 1)
        for (long *p = a + 1; p <= a + N; p++) {
            spin(N - (int)(p - a));
#pragma omp ordered depend(sink : p - 1)
            *p = p[-1] + 1;
#pragma omp ordered depend(source)
        }
    }
    return a[N] == N + 1 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_threads_atomic_capture_block(void) {
    int    x = 0, y = 5, w = 0, seen[256] = {0}, ok = 1;
    double dx = 1.0, dv = 0.0;
    int    sw = 7, so = 0;
    int    nt = team_size();
#pragma omp parallel num_threads(4)
    {
        int v;
#pragma omp atomic capture
        {
            v  = x;
            x += 1;
        }
        seen[v & 255] = 1;
    }
    int distinct = 0;
    for (int i = 0; i < 256; i++)
        distinct += seen[i];
    ok &= distinct == nt && x == nt;
#pragma omp atomic capture
    {
        y *= 2;
        w  = y;
    }
    ok &= y == 10 && w == 10;
#pragma omp atomic capture
    {
        w = y;
        y = y * 3;
    }
    ok &= y == 30 && w == 10;
#pragma omp atomic capture
    {
        dv = dx;
        dx = dx * 4.0;
    }
    ok &= dx == 4.0 && dv == 1.0;
#pragma omp atomic capture
    {
        so = sw;
        sw = 99;
    }
    ok &= sw == 99 && so == 7;
#pragma omp atomic capture
    {
        w = y;
        ++y;
    }
    ok &= y == 31 && w == 30;
#pragma omp atomic capture
    {
        y--;
        w = y;
    }
    ok &= y == 30 && w == 30;
    return ok ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_omp_threads_atomic_compare(void) {
    int    nt = team_size(), mx = -1, mn = 1000, owner = -1, winners = 0;
    int    ok = 1;
    double dmin = 100.0;
#pragma omp parallel num_threads(4)
    {
        int tid = omp_get_thread_num(), won = 0;
#pragma omp atomic compare
        if (mx < tid) { mx = tid; }
#pragma omp atomic compare
        mn = tid < mn ? tid : mn;
#pragma omp atomic compare
        if (dmin > tid + 0.5) { dmin = tid + 0.5; }
#pragma omp atomic compare capture
        { won = owner == -1; if (won) { owner = tid; } }
        if (won)
#pragma omp atomic
            winners++;
    }
    ok &= mx == nt - 1 && mn == 0 && dmin == 0.5;
    ok &= winners == 1 && owner >= 0 && owner < nt;

    int    x = 5, v = 0, r = 0;
    double dz = -0.0;
#pragma omp atomic compare capture
    { v = x; if (x == 5) { x = 6; } }
    ok &= x == 6 && v == 5;
#pragma omp atomic compare capture
    { if (x == 7) { x = 8; } v = x; }
    ok &= x == 6 && v == 6;
#pragma omp atomic compare capture
    if (x == 7) { x = 8; } else { v = x; }
    ok &= x == 6 && v == 6;
#pragma omp atomic capture compare
    { x = x > 3 ? 3 : x; v = x; }
    ok &= x == 3 && v == 3;
#pragma omp atomic compare update weak fail(acquire)
    x = x == 3 ? 4 : x;
    ok &= x == 4;
#pragma omp atomic compare capture weak fail(relaxed)
    { r = x == 4; if (r) { x = 10; } else { v = x; } }
    ok &= x == 10 && r == 1;
#pragma omp atomic compare
    if (dz == 0.0) { dz = 1.0; }
    ok &= 1.0 / dz < 0; // -0.0 is not bitwise-equal to 0.0
    return ok ? 42 : 1;
}

#pragma cccc suite end
