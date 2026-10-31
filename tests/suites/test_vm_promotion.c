// CCCC_FLAGS: --testing
// Loop locals live in callee-saved registers: values must match what the same
// code computes through memory, across calls, longjmp, signals and tail calls.

#include <setjmp.h>
#include <signal.h>
#include <stdckdint.h>
#include <stdlib.h>

static jmp_buf      pm_jb;
static volatile int pm_sig;

static void pm_on_sig(int s) {
    (void)s;
    pm_sig++;
}

[[cccc::test]]
void test_narrow_counters_wrap(void) {
    unsigned char uc = 250;
    signed char   sc = 125;
    short         sh = 32765;
    unsigned      un = 4294967294u;
    for (int i = 0; i < 10; i++) {
        uc++;
        sc++;
        sh++;
        un++;
    }
    AssertEq(uc, 4);
    AssertEq(sc, -121);
    AssertEq(sh, -32761);
    AssertEq(un, 8u);
}

[[cccc::test]]
void test_bool_and_bitint_counters(void) {
    bool b       = false;
    _BitInt(7) n = 60;
    for (int i = 0; i < 5; i++) {
        b = !b;
        n++;
    }
    AssertEq(b, true);
    AssertEq((int)n, -63);
}

[[cccc::test]]
void test_float_rounds_like_memory(void) {
    float  f = 0;
    float  r = 0;
    float *p = &r;
    for (int i = 0; i < 100; i++) {
        f  += 0.1f;
        *p += 0.1f;
    }
    AssertEq(f, r);
    double d = 0.5;
    for (int i = 0; i < 4; i++)
        d += 1.25;
    AssertEq(d, 5.5);
}

[[cccc::test]]
void test_address_taken_stays_in_memory(void) {
    int  x = 0;
    int *p = &x;
    for (int i = 0; i < 5; i++)
        x += 2;
    AssertEq(*p, 10);
    for (int i = 0; i < 5; i++)
        (*p)++;
    AssertEq(x, 15);
}

static int pm_thrower(int n) {
    int t = 0;
    for (int i = 0; i < n; i++)
        t += i * 3;
    longjmp(pm_jb, t + 1);
}

static int pm_guard(int n) {
    if (setjmp(pm_jb) == 0) {
        pm_thrower(n);
        return -1;
    }
    return 7;
}

static int pm_frame(int n) {
    int a = 1, b = 2, c = 3, d = 4;
    for (int i = 0; i < n; i++) {
        a += i;
        b += pm_guard(4);
        c += a;
        d += b;
    }
    return a + b + c + d;
}

[[cccc::test]]
void test_longjmp_keeps_outer_locals(void) {
    AssertEq(pm_frame(3), 89);
}

static int pm_cmp(const void *a, const void *b) {
    int s = 0;
    for (int i = 0; i < 3; i++)
        s += i;
    if (*(const int *)a == 3)
        longjmp(pm_jb, s + 100);
    return *(const int *)a - *(const int *)b;
}

static int pm_sort_guard(int *v, int n) {
    int hit = setjmp(pm_jb);
    if (hit)
        return hit;
    qsort(v, (size_t)n, sizeof *v, pm_cmp);
    return 0;
}

[[cccc::test]]
void test_longjmp_out_of_callback(void) {
    int total = 0;
    int v[4]  = {5, 3, 9, 1};
    for (int i = 0; i < 4; i++) {
        total += i;
        AssertEq(pm_sort_guard(v, 4), 103);
    }
    AssertEq(total, 6);
}

[[cccc::test]]
void test_signal_handler_preserves_registers(void) {
    signal(SIGUSR1, pm_on_sig);
    pm_sig    = 0;
    long sum  = 0;
    int  last = 0;
    for (int i = 0; i < 5; i++) {
        sum += i;
        raise(SIGUSR1);
        last = i;
    }
    AssertEq(sum, 10);
    AssertEq(last, 4);
    AssertEq(pm_sig, 5);
}

[[cccc::test]]
void test_nested_function_capture(void) {
    int count = 0;
    void bump(void) {
        count++;
    }
    for (int i = 0; i < 5; i++)
        bump();
    AssertEq(count, 5);
}

[[cccc::test]]
void test_checked_arithmetic_on_local(void) {
    int  r        = 0;
    bool overflow = false;
    for (int i = 0; i < 5; i++)
        overflow |= ckd_add(&r, r, 1);
    AssertEq(r, 5);
    AssertEq(overflow, false);
}

static int pm_rec(int n) {
    int s = 0;
    for (int i = 0; i < n; i++)
        s += i;
    if (n > 0)
        s += pm_rec(n - 1);
    return s;
}

[[cccc::test]]
void test_recursion_with_loop_locals(void) {
    int total = 0;
    for (int i = 0; i < 3; i++)
        total += pm_rec(5);
    AssertEq(total, 3 * (10 + 6 + 3 + 1 + 0 + 0));
}

static int pm_tail(int n, int acc) {
    for (int i = 0; i < 2; i++)
        acc += i + n;
    if (n == 0)
        return acc;
    return pm_tail(n - 1, acc);
}

[[cccc::test]]
void test_tail_call_restores_caller_registers(void) {
    int keep = 0;
    for (int i = 0; i < 3; i++) {
        keep += i;
        AssertEq(pm_tail(4, 0), 25);
    }
    AssertEq(keep, 3);
}

static int pm_narrow_param(signed char c, int n) {
    for (int i = 0; i < n; i++)
        c++;
    return c;
}

[[cccc::test]]
void test_narrow_params(void) {
    AssertEq(pm_narrow_param(125, 5), -126);
    AssertEq(pm_narrow_param(-3, 2), -1);
}

static int pm_many(int n) {
    int a = 0, b = 1, c = 2, d = 3, e = 4, f = 5, g = 6;
    for (int i = 0; i < n; i++) {
        a += i;
        b += a;
        c += b;
        d += c;
        e += d;
        f += e;
        g += f;
    }
    return a + b + c + d + e + f + g;
}

[[cccc::test]]
void test_more_loop_locals_than_registers(void) {
    AssertEq(pm_many(4), 623);
    int total = 0;
    for (int i = 0; i < 4; i++)
        total += pm_many(i);
    AssertEq(total, pm_many(0) + pm_many(1) + pm_many(2) + pm_many(3));
}

[[cccc::test]]
void test_pointer_walk_and_switch_in_loop(void) {
    int  arr[5] = {1, 2, 3, 4, 5};
    int *p      = arr;
    int  s      = 0;
    for (int i = 0; i < 5; i++) {
        switch (i % 2) {
            case 0:
                s += *p;
                break;
            default:
                s -= *p;
        }
        p++;
    }
    AssertEq(s, 1 - 2 + 3 - 4 + 5);
    AssertEq(p - arr, 5);
}

[[cccc::test]]
void test_do_while_and_goto_loops(void) {
    int n = 0, m = 0;
    do {
        n += 2;
    } while (n < 10);
    int k = 0;
again:
    m += k;
    if (++k < 5)
        goto again;
    AssertEq(n, 10);
    AssertEq(m, 10);
}

static long pm_mix(long v) {
    long a = v, b = 7;
    for (int i = 0; i < 3; i++) {
        a  = a * 5 + b;
        b ^= a;
    }
    return a & 0xff;
}

static long pm_goto_calls(long n) {
    long sum = 0, i = 0;
again:
    sum += pm_mix(i);
    if (++i < n)
        goto again;
    return sum;
}

[[cccc::test]]
void test_goto_loop_survives_calls(void) {
    long expect = 0;
    for (long i = 0; i < 20; i++)
        expect += pm_mix(i);
    AssertEq(pm_goto_calls(20), expect);
}

static int pm_goto_in_for(int rows) {
    int total = 0;
    for (int r = 0; r < rows; r++) {
        int c = 0;
    col:
        total += r * 10 + c;
        if (++c < 3)
            goto col;
    }
    return total;
}

[[cccc::test]]
void test_goto_loop_nested_in_for(void) {
    AssertEq(pm_goto_in_for(4),
             0 + 1 + 2 + 10 + 11 + 12 + 20 + 21 + 22 + 30 + 31 + 32);
}

static long pm_dispatch(const unsigned char *code) {
    static void *labels[] = {&&op_inc, &&op_dbl, &&op_neg, &&op_end};
    long         acc      = 1;
    int          steps    = 0;
    goto        *labels[*code++];
op_inc:
    acc += 3;
    steps++;
    goto *labels[*code++];
op_dbl:
    acc *= 2;
    steps++;
    goto *labels[*code++];
op_neg:
    acc = -acc;
    steps++;
    goto *labels[*code++];
op_end:
    return acc * 100 + steps;
}

[[cccc::test]]
void test_computed_goto_dispatch_keeps_state(void) {
    static const unsigned char prog[] = {0, 1, 2, 0, 3};
    AssertEq(pm_dispatch(prog), (-((1 + 3) * 2) + 3) * 100 + 4);
}

static int pm_forward_goto(int n) {
    int acc = 0;
    for (int i = 0; i < n; i++) {
        if (i % 2)
            goto skip;
        acc += i;
    skip:
        acc++;
    }
    return acc;
}

[[cccc::test]]
void test_forward_goto_unchanged(void) {
    AssertEq(pm_forward_goto(6), (0 + 2 + 4) + 6);
}
