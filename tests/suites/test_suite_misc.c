// CCCC_FLAGS: --testing
// Consolidated suite: miscellaneous: volatile, builtins, compound literals,
//   comma operator, block scope, token paste, digraphs, edge cases, regression tests
// Source tests: test_builtins, test_volatile, test_cast_const, test_comma,
//   test_block_scope, test_compound_simple, test_compound_struct_access,
//   test_define_only, test_simple_paste, test_vm_profile_smoke,
//   test_deep_initializer_576, test_memzero_init, test_cond_pointer_type,
//   test_coalesce, test_block_partial_init,
//   test_edge_digraph_braces, test_edge_digraph_directive,
//   test_edge_digraph_paste, test_edge_digraph_subscript,
//   test_edge_empty_union_varargs, test_edge_worm_emoji_macros

#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

// [from test_builtins]
// Test GNU-style builtins implemented in the parser (ticket #220, #212, #213, #513)

// [from test_volatile]
// Test volatile keyword parsing and semantics.
// Verifies: correct values observed through volatile accesses, no caching,
// correct behaviour through pointer aliases and signal-like patterns.

volatile int global_volatile = 42;

static volatile sig_atomic_t flag = 0;

#pragma cccc suite begin "misc"

// test_builtins
[[cccc::test(return = 42)]]
int test_builtins(void) {
    // Math constants
    double h = HUGE_VAL;
    float hf = HUGE_VALF;
    double inf = INFINITY;
    float inff = __builtin_inff();
    double nan = NAN;
    float nanf = __builtin_nanf("");

    // Verify HUGE_VAL is positive infinity
    if (!(h > 0)) return 1;
    if (!(inf > 0)) return 2;
    if (!(hf > 0)) return 3;
    if (!(inff > 0)) return 4;

    // Verify NaN
    if (nan == nan) return 5;     // NaN != NaN
    if (nanf == nanf) return 6;

    // Math predicates
    if (!__builtin_isnan(nan)) return 10;
    if (!__builtin_isnan(nanf)) return 11;
    if (__builtin_isnan(1.0)) return 12;

    if (!__builtin_isinf(h)) return 13;
    if (!__builtin_isinf(inf)) return 14;
    if (__builtin_isinf(1.0)) return 15;

    if (!__builtin_isfinite(1.0)) return 16;
    if (__builtin_isfinite(h)) return 17;
    if (__builtin_isfinite(nan)) return 18;

    if (!__builtin_signbit(-1.0)) return 19;
    if (__builtin_signbit(1.0)) return 20;

    // __builtin_expect (ignored hint)
    if (__builtin_expect(42, 0) != 42) return 21;
    if (__builtin_expect(0, 1) != 0) return 22;

    // __builtin_constant_p
    if (!__builtin_constant_p(1 + 2)) return 23;
    int x = 5;
    if (__builtin_constant_p(x)) return 24;

    // __builtin_alloca
    void *p = __builtin_alloca(64);
    if (!p) return 25;

    // ==== Bit manipulation (#212) ====

    // clz / clzll
    if (__builtin_clz(1u) != 31) return 30;
    if (__builtin_clz(0x80000000u) != 0) return 31;
    if (__builtin_clzll(1ull) != 63) return 32;
    if (__builtin_clzll(0x8000000000000000ull) != 0) return 33;

    // ctz / ctzll
    if (__builtin_ctz(8u) != 3) return 34;
    if (__builtin_ctz(1u) != 0) return 35;
    if (__builtin_ctzll(8ull) != 3) return 36;
    if (__builtin_ctzll(1ull) != 0) return 37;

    // popcount / popcountll
    if (__builtin_popcount(0u) != 0) return 38;
    if (__builtin_popcount(0xFFu) != 8) return 39;
    if (__builtin_popcount(0xFFFFFFFFu) != 32) return 40;
    if (__builtin_popcountll(0xFFFFFFFFFFFFFFFFull) != 64) return 41;

    // parity / parityll  (0xB = 0b1011, 0x6 = 0b0110)
    if (__builtin_parity(0xBu) != 1) return 42;
    if (__builtin_parity(0x6u) != 0) return 43;
    if (__builtin_parityll(0xBull) != 1) return 44;

    // ffs / ffsll: 1-based index of lowest set bit, 0 for 0
    if (__builtin_ffs(0) != 0) return 45;
    if (__builtin_ffs(8) != 4) return 46;
    if (__builtin_ffs(1) != 1) return 47;
    if (__builtin_ffsll(0ll) != 0) return 48;
    if (__builtin_ffsll(8ll) != 4) return 49;

    // bswap16 / bswap32 / bswap64
    if (__builtin_bswap16(0x0102u) != 0x0201u) return 50;
    if (__builtin_bswap32(0x01020304u) != 0x04030201u) return 51;
    if (__builtin_bswap64(0x0102030405060708ull) != 0x0807060504030201ull) return 52;

    // ==== Overflow arithmetic (#213) ====

    // add_overflow
    int r_add;
    if (__builtin_add_overflow(1, 2, &r_add)) return 60;
    if (r_add != 3) return 61;
    if (!__builtin_add_overflow(INT_MAX, 1, &r_add)) return 62;

    // sub_overflow
    int r_sub;
    if (__builtin_sub_overflow(5, 3, &r_sub)) return 63;
    if (r_sub != 2) return 64;
    if (!__builtin_sub_overflow(INT_MIN, 1, &r_sub)) return 65;

    // mul_overflow (int variant — matches type.c usage)
    int r_mul;
    if (__builtin_mul_overflow(3, 4, &r_mul)) return 66;
    if (r_mul != 12) return 67;
    if (!__builtin_mul_overflow(INT_MAX, 2, &r_mul)) return 68;

    // mul_overflow (long long variant — matches optimize.c usage)
    long long r_mull;
    if (__builtin_mul_overflow(3ll, 4ll, &r_mull)) return 69;
    if (r_mull != 12ll) return 70;
    if (!__builtin_mul_overflow((long long)INT64_MAX, 2ll, &r_mull)) return 71;

    // add_overflow no-overflow with long long result
    long long r_addl;
    if (__builtin_add_overflow(1ll, 2ll, &r_addl)) return 72;
    if (r_addl != 3ll) return 73;

    // ==== Ticket #513: long-double constant builtins ====

    // __builtin_huge_vall() -> long double positive infinity
    long double hvl = __builtin_huge_vall();
    if (!(hvl > 0)) return 80;
    if (sizeof(hvl) != sizeof(long double)) return 81;

    // __builtin_infl() -> long double infinity
    long double il = __builtin_infl();
    if (!(il > 0)) return 82;

    // __builtin_nanl("") -> long double NaN
    long double nl = __builtin_nanl("");
    if (nl == nl) return 83;  // NaN != NaN

    // ==== __builtin_alloca_with_align ====
    // align arg is in bits; 128 = 16 bytes (VM minimum)
    char *awa = (char *)__builtin_alloca_with_align(16, 128);
    if (!awa) return 84;
    awa[0] = 'X';
    if (awa[0] != 'X') return 85;

    // ==== __builtin_strlen / __builtin_strcmp (forwarded to libc) ====
    const char *s = "hello";
    if (__builtin_strlen(s) != strlen(s)) return 86;
    if (__builtin_strlen("") != 0) return 87;
    if (__builtin_strcmp("abc", "abc") != 0) return 88;
    if (__builtin_strcmp("abc", "abd") >= 0) return 89;
    if (__builtin_strcmp("abd", "abc") <= 0) return 90;

    // Verify __builtin_strlen agrees with <string.h> strlen on same symbol
    if (__builtin_strlen("world") != 5) return 91;

    return 42;
}

// test_volatile
[[cccc::test(return = 42)]]
int test_volatile(void) {
    // Basic volatile local
    volatile int x = 10;

    // Volatile pointer
    volatile int *vp = &x;

    // Pointer to volatile (unused but must parse)
    int * volatile pv;
    (void)pv;

    // Const volatile
    const volatile int cv = 100;

    // Read volatile variable
    int a = x;

    // Write volatile variable
    x = 20;

    // Read through volatile pointer (must re-read, not cache)
    int b = *vp;

    if (a != 10) return 1;
    if (b != 20) return 2;
    if (cv != 100) return 3;
    if (global_volatile != 42) return 4;

    // Alias test: write through non-volatile alias, read back via volatile
    // pointer — the volatile read must see the updated value.
    volatile int aliased = 0;
    int *alias = (int *)&aliased;
    *alias = 99;
    if (aliased != 99) return 5;

    // Loop test: volatile counter modified through alias inside loop.
    // If the volatile read were hoisted out of the loop this would fail.
    volatile int counter = 0;
    int *cp = (int *)&counter;
    for (int i = 0; i < 5; i++) {
        *cp = i;
        if (counter != i) return 6;
    }

    // Signal-flag pattern: write to volatile sig_atomic_t, read back.
    flag = 1;
    if (flag != 1) return 7;
    flag = 0;
    if (flag != 0) return 8;

    // Volatile struct member access
    struct { volatile int v; int n; } s;
    s.v = 77;
    s.n = 3;
    if (s.v != 77) return 9;

    // Volatile global write and re-read
    global_volatile = 55;
    if (global_volatile != 55) return 10;

    return 42;
}

// [from test_cast_const]
[[cccc::test(return = 42)]]
int test_cast_const(void) {
    return ((char)1000) == -24 ? 42 : 1;
}

// [from test_comma]
// Comprehensive comma operator test.
[[cccc::test(return = 42)]]
int test_comma(void) {
    int x = 0, y = 0, result = 0;
    result = (x = 5, y = 10, x + y);        // 15
    int a = 0;
    int b = (a = 3, a * 2);                  // b=6, a=3
    if ((x = 7, y = 8, x < y)) result += 10; // 25
    int i = 0, sum = 0;
    for (i = 0, sum = 0; i < 3; i = i + 1) sum += 1;
    result += sum;                            // 28
    int c = (a = 1, (b = 2, a + b));         // c=3 (unused)
    result += (a = 5, b = 6, a + b);         // 39
    int final = (x = 1, y = 2, x + y);      // 3
    return result + final;                    // 42
}

// [from test_block_scope]
// Nested block scope and variable shadowing.
[[cccc::test(return = 42)]]
int test_block_scope(void) {
    int x = 10, result = 0;
    { int x = 20; result = x; }
    if (x != 10) return 1;
    if (result != 20) return 2;
    { int x = 30; { int x = 40; result = x; } if (x != 30) return 3; }
    if (result != 40) return 4;
    if (x != 10) return 5;
    { int y = 5; result = x + y; }
    if (result != 15) return 6;
    result = 0;
    { int x = 2; { int x = 3; result = x * 10; } result += x; }
    result += x; // 32 + 10 = 42
    return result;
}

// [from test_compound_simple]
// Simple compound literal returning a scalar.
[[cccc::test(return = 42)]]
int test_compound_simple(void) {
    int x = (int){42};
    return x;
}

// [from test_compound_struct_access]
// Struct compound literal with pointer member access.
[[cccc::test(return = 42)]]
int test_compound_struct_access(void) {
    struct CmpPoint { int x; int y; };
    struct CmpPoint *p1 = &(struct CmpPoint){30, 12};
    return p1->x + p1->y;
}

// [from test_define_only]
// Defining a macro but never calling it must not error.
[[cccc::test(return = 42)]]
int test_define_only(void) {
#define JUST_ARGS(...) __VA_OPT__(__VA_ARGS__)
    return 42;
#undef JUST_ARGS
}

// [from test_simple_paste]
// ## token paste outside __VA_OPT__.
[[cccc::test(return = 42)]]
int test_simple_paste(void) {
#define SIMPLE_PASTE(a, b) a ## b
    int var123 = 42;
    int v = SIMPLE_PASTE(var, 123);
    return v == 42 ? 42 : 1;
#undef SIMPLE_PASTE
}

// [from test_vm_profile_smoke]
// Basic loop: vm profile smoke test.
[[cccc::test(return = 42)]]
int test_vm_profile_smoke(void) {
    int acc = 0;
    for (int i = 0; i < 6; i++) acc += i;
    return acc == 15 ? 42 : 1;
}

// [from test_deep_initializer_576]
// Regression #576: large brace-initialiser must not overflow the host C stack.
[[cccc::test(return = 42)]]
int test_deep_initializer_576(void) {
    int a[1000] = {0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11,12,0,1,2,3,4,5,6,7,8,9,10,11};
    long s = 0;
    for (int i = 0; i < 1000; i++) s += a[i];
    if (s != 5994) return 1;
    int b[4096] = {7};
    if (b[0] != 7) return 2;
    long s2 = 0;
    for (int i = 1; i < 4096; i++) s2 += b[i];
    if (s2 != 0) return 3;
    return 42;
}

// [from test_memzero_init]
// Regression #464: partial aggregate initialiser must zero unspecified elements.
[[cccc::test(return = 42, flags = "-O0")]]
int test_memzero_init(void) {
    static void dirty_stack(void) {
        volatile unsigned char buf[256];
        for (int i = 0; i < 256; i++) buf[i] = 0xAB;
        (void)buf[0];
    }
    dirty_stack();
    unsigned char seen[256] = {0};
    for (int i = 0; i < 256; i++) if (seen[i]) return 1;
    dirty_stack();
    struct MzS { int a; char b; long c; char d[8]; };
    struct MzS s = {.a = 1};
    if (s.b != 0) return 2;
    if (s.c != 0) return 3;
    for (int i = 0; i < 8; i++) if (s.d[i] != 0) return 4;
    return 42;
}

// [from test_cond_pointer_type]
// Regression #591: conditional operator must yield pointer type (not int).
[[cccc::test(return = 42)]]
int test_cond_pointer_type(void) {
    static void *xmalloc(int n) { return n <= 0 ? 0 : malloc((size_t)n); }
    int *a = (int *)xmalloc((int)sizeof(int) * 4);
    if (!a) return 1;
    a[0] = 10; a[1] = 20; a[2] = 12;
    int sum = a[0] + a[1] + a[2];
    free(a);
    if (sum != 42) return 2;
    int x = 7;
    int *p = (1 ? &x : 0);
    if (*p != 7) return 3;
    *p = 35;
    if (x != 35) return 4;
    int y = 100, z = 200;
    int *q = (0 ? &y : &z);
    if (*q != 200) return 5;
    char buf[8] = "ok";
    void *vp = (1 ? (void *)buf : 0);
    if (__builtin_strcmp((char *)vp, "ok") != 0) return 6;
    return 42;
}

// [from test_coalesce]
// VM heap coalesces adjacent free blocks for large re-allocations.
[[cccc::test(return = 42)]]
int test_coalesce(void) {
    void *p1 = malloc(100), *p2 = malloc(100), *p3 = malloc(100);
    void *p4 = malloc(100), *p5 = malloc(100);
    free(p1); free(p3); free(p5);
    free(p2); free(p4);
    void *large = malloc(400);
    if (!large) return 1;
    free(large);
    return 42;
}

// [from test_block_partial_init]
// Regression #473: __block aggregate partial-init must zero unspecified elements.
[[cccc::test(return = 42, flags = "-O0 --memory-poisoning")]]
int test_block_partial_init(void) {
    struct BpS { int a; char b; long c; char d[8]; };
    __block int x[4] = {1};
    if (x[0] != 1) return 1;
    if (x[1] != 0 || x[2] != 0 || x[3] != 0) return 2;
    __block struct BpS s = {.a = 42};
    if (s.a != 42 || s.b != 0 || s.c != 0) return 3;
    for (int i = 0; i < 8; i++) if (s.d[i] != 0) return 4;
    __block int arr[4] = {99};
    __block int result = 0;
    void (^check)(void) = ^{
        if (arr[0] != 99) result = 5;
        if (arr[1] != 0 || arr[2] != 0 || arr[3] != 0) result = 6;
    };
    check();
    if (result) return result;
    return 42;
}

// [from test_edge_digraph_braces]
// <% and %> digraphs as { and } block delimiters (C23 §6.4.6).
[[cccc::test(return = 42)]]
int test_edge_digraph_braces(void) <%
    static int sq(int x) <% return x * x; %>
    int r = sq(7);
    return r == 49 ? 42 : 1;
%>

// [from test_edge_digraph_directive]
// %: digraph as # in preprocessor directive lines (C23 §6.4.6).
[[cccc::test(return = 42)]]
int test_edge_digraph_directive(void) <%
%:define DGRAPH_ANSWER 42
    return DGRAPH_ANSWER == 42 ? 42 : 1;
%:undef DGRAPH_ANSWER
%>

// [from test_edge_digraph_paste]
// %:%: digraph as ## token-paste operator (C23 §6.4.6).
[[cccc::test(return = 42)]]
int test_edge_digraph_paste(void) {
#define DG_PASTE(a, b) a %:%: b
    int dgfoobar = 42;
    int result = DG_PASTE(dgfoo, bar);
    return result == 42 ? 42 : 1;
#undef DG_PASTE
}

// [from test_edge_digraph_subscript]
// <: and :> digraphs as [ and ] (C23 §6.4.6).
[[cccc::test(return = 42)]]
int test_edge_digraph_subscript(void) {
    int a<:3:> = {10, 20, 30};
    int sum = a<:0:> + a<:1:> + a<:2:>;
    return sum == 60 ? 42 : 1;
}

// [from test_edge_empty_union_varargs]
// Empty union variables (global, local, array) compile and have size 0.
union {} misc_empty_global = {};
union {} misc_empty_global_arr[3] = {};

[[cccc::test(return = 42, expect_stdout = "Let's count: 3 0 2 1")]]
int test_edge_empty_union_varargs(void) {
    union {} local_empty = {};
    union {} var[100] = {};
    (void)misc_empty_global;
    (void)misc_empty_global_arr[0];
    (void)local_empty;
    printf("Let's count: %d %d %d %d\n", 3, var[42], 2, 1);
    return 42;
}

// [from test_edge_worm_emoji_macros]
// -~ (right worm, +1) and ~- (left worm, -1) chains; emoji macro identifiers.
[[cccc::test(return = 42, expect_stdout = "-42 \\+ 5 = -37")]]
int test_edge_worm_emoji_macros(void) {
    if (-~42 != 43) return 1;
    if (~-42 != 41) return 2;
    if (-~-~-~42 != 45) return 3;
    if (~-~-~-~-~-42 != 37) return 4;

#define 🪱 -~
#define 🐍 ~-

    if ((🪱 🪱 🐍 🐍 🐍 42) != 41) return 5;  // 42 - 3 + 2
    int v = 🪱 🪱 🪱 🪱 🪱 -42;               // -42 + 5 = -37
    printf("-42 + 5 = %d\n", v);
    if (v != -37) return 6;

#undef 🪱
#undef 🐍

    return 42;
}

#pragma cccc suite end
