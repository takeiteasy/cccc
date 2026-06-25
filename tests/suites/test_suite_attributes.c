// CCCC_FLAGS: --testing
// Consolidated suite: attributes: [[nodiscard]], [[noreturn]], fallthrough, format, pragma
// Source tests: test_alignof_alignas, test_attribute_simple, test_fallthrough_attribute, test_format_attribute_custom, test_noreturn, test_pragma_link

#include "stdio.h"

// [from test_alignof_alignas]
// Test _Alignof and _Alignas alignment control
// Tests compile-time alignment queries and alignment specifications

// [from test_attribute_simple]
// Simple attribute test

int __attribute__((unused)) x = 5;

// [from test_fallthrough_attribute]
// Test [[fallthrough]] attribute
// Expected return: 42

static int test_fallthrough(int x) {
    int result = 0;
    switch (x) {
        case 1:
            result = 10;
            [[fallthrough]];
        case 2:
            result = result + 20;
            break;
        default:
            result = 99;
    }
    return result;
}

// [from test_format_attribute_custom]
/*
 * Test __attribute__((format(printf, ...))) on custom functions
 */

static int my_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2))) {
    return 42;
}

static int my_fancy_printf(const char *label, const char *fmt, ...)
    __attribute__((format(printf, 2, 3))) {
    return 42;
}

// [from test_noreturn]
// Test _Noreturn function specifier and [[noreturn]] / __attribute__((noreturn))

// Test 1: _Noreturn keyword (C11)
_Noreturn void test_noreturn_kw(void) {
    for (;;) {}
}

// Test 2: __attribute__((noreturn)) (GNU) after return type
void __attribute__((noreturn)) test_noreturn_gnu(void) {
    for (;;) {}
}

// Test 3: __attribute__((__noreturn__)) (GNU with underscores)
void __attribute__((__noreturn__)) test_noreturn_gnu_us(void) {
    for (;;) {}
}

// Test 4: Check that noreturn is propagated to function objects

static int check_noreturn(void) {
    return 42;
}

// [from test_pragma_link]
// #pragma cccc link("name") queues a library for FFI resolution, the same
// as -l/--library (#357).

#pragma cccc link("m")

extern double sqrt(double x);

#pragma cccc suite begin "attributes"

// test_alignof_alignas
[[cccc::test(return = 42)]]
int test_alignof_alignas(void) {
    // Test 1: _Alignof with basic types
    if (_Alignof(char) != 1) return 1;
    if (_Alignof(short) != 2) return 2;
    if (_Alignof(int) < 4) return 3;
    if (_Alignof(long) < 8) return 4;
    if (_Alignof(double) < 8) return 5;
    
    // Test 2: _Alignof with pointer
    if (_Alignof(int*) < 8) return 6;
    if (_Alignof(void*) < 8) return 7;
    
    // Test 3: _Alignof with struct
    struct S1 {
        char c;
        int i;
    };
    if (_Alignof(struct S1) < 4) return 8;
    
    // Test 4: _Alignof with array
    int arr[10];
    if (_Alignof(arr) < 4) return 9;
    
    // Test 5: _Alignof with variable
    int x = 42;
    if (_Alignof(x) < 4) return 10;
    
    // Test 6: _Alignas with integer (2-byte alignment)
    _Alignas(2) char c1 = 'A';
    if (c1 != 'A') return 11;
    
    // Test 7: _Alignas with type
    _Alignas(int) char c2 = 'B';
    if (c2 != 'B') return 12;
    
    // Test 8: _Alignas with larger alignment
    _Alignas(16) int aligned_int = 100;
    if (aligned_int != 100) return 13;
    
    // Test 9: _Alignas in struct
    struct AlignedStruct {
        _Alignas(8) char c;
        int i;
    };
    struct AlignedStruct as = {'X', 200};
    if (as.c != 'X' || as.i != 200) return 14;
    
    // Test 10: Multiple aligned variables
    _Alignas(8) char a1 = '1';
    _Alignas(8) char a2 = '2';
    _Alignas(8) char a3 = '3';
    if (a1 != '1' || a2 != '2' || a3 != '3') return 15;
    
    return 42;  // Success
}

// test_attribute_simple
[[cccc::test(return = 42)]]
int test_attribute_simple(void) {
    if (x != 5) return 1;  // Assert x == 5
    return 42;
}

// test_fallthrough_attribute
[[cccc::test(return = 42)]]
int test_fallthrough_attribute(void) {
    if (test_fallthrough(1) != 30) return 1;
    if (test_fallthrough(2) != 20) return 2;
    if (test_fallthrough(5) != 99) return 3;
    return 42;
}

// test_format_attribute_custom
[[cccc::test(return = 42, flags = "--format-string-checks")]]
int test_format_attribute_custom(void) {
    // Valid calls — should pass without warning
    my_printf("Hello\n");
    my_printf("Number: %d\n", 42);
    my_printf("Two: %d %d\n", 10, 20);
    my_printf("String: %s\n", "test");
    my_printf("Mixed: %d %s\n", 42, "test");

    // Test with sprintf-style: label + format
    my_fancy_printf("INFO", "value = %d\n", 99);

    // Test %% — shouldn't consume args
    my_printf("100%% complete\n");

    return 42;
}

// test_noreturn
[[cccc::test(return = 42)]]
int test_noreturn(void) {
    return 42;
}

// test_pragma_link
[[cccc::test(return = 42)]]
int test_pragma_link(void) {
    double r = sqrt(16.0);
    if ((int)r != 4)
        return 1;
    return 42;
}

#pragma cccc suite end

// [from test_at_attribute.c]
// Tests for @identifier attribute syntax (ticket #234).
#pragma cccc suite begin "attributes/at_syntax"

@comptime
int at_double_val(int x) { return x * 2; }

@comptime
Node *at_make_doubled_42(void) {
    return MakeIntLiteral(at_double_val(21));
}

@comptime
Node *at_add_one(Node *x) {
    return MakeBinary(NK_ADD, x, MakeIntLiteral(1));
}

[[cccc::test]]
void test_at_comptime_inline(void) {
    int v = at_make_doubled_42();
    AssertEq(v, 42);
}

[[cccc::test]]
void test_at_macro_inline(void) {
    int v = at_add_one(10);
    AssertEq(v, 11);
}

@test
void test_at_test_bare(void) {
    AssertEq(1 + 1, 2);
}

@test(suite="at_suite")
void test_at_test_with_suite(void) {
    AssertEq(6 * 7, 42);
}

#pragma cccc suite end

// [from test_attr_cleanup.c]
// Tests for __attribute__((cleanup(fn))) scope-exit callbacks (tickets #218, #480).
#include <stdbool.h>

#pragma cccc suite begin "attributes/cleanup"

static int g_cleanup_log[64];
static int g_cleanup_log_n = 0;

static void cleanup_log_reset(void) { g_cleanup_log_n = 0; }
static void cleanup_log_val(int v)  { if (g_cleanup_log_n < 64) g_cleanup_log[g_cleanup_log_n++] = v; }
static void cleanup_int(int *p) { cleanup_log_val(*p); }
static void cleanup_set_true(bool *p) { *p = true; }

[[cccc::test]]
static void test_basic_block_exit(void) {
    cleanup_log_reset();
    {
        int x __attribute__((cleanup(cleanup_int))) = 42;
        (void)x;
    }
    AssertEq(g_cleanup_log_n, 1);
    AssertEq(g_cleanup_log[0], 42);
}

static void void_with_cleanup_return(bool cond, bool *cleaned) {
    bool flag __attribute__((cleanup(cleanup_set_true))) = false;
    (void)flag;
    *cleaned = false;
    if (cond) {
        *cleaned = true;
        return;
    }
}

[[cccc::test]]
static void test_cleanup_on_void_return(void) {
    bool cleaned = false;
    bool took_early = false;
    void_with_cleanup_return(true, &took_early);
    AssertEq(took_early, true);
}

static int int_return_with_cleanup(void) {
    int sentinel __attribute__((cleanup(cleanup_int))) = 99;
    (void)sentinel;
    return 42;
}

[[cccc::test]]
static void test_nonvoid_return_value_preserved(void) {
    cleanup_log_reset();
    int result = int_return_with_cleanup();
    AssertEq(result, 42);
    AssertEq(g_cleanup_log_n, 1);
    AssertEq(g_cleanup_log[0], 99);
}

static void cleanup_float_marker(int *p) { *p = 1; }

static float float_return_with_cleanup(void) {
    int marker __attribute__((cleanup(cleanup_float_marker))) = 0;
    (void)marker;
    return 3.14f;
}

[[cccc::test]]
static void test_float_return_preserved(void) {
    float r = float_return_with_cleanup();
    AssertEq(r > 3.13f && r < 3.15f, true);
}

[[cccc::test]]
static void test_lifo_order(void) {
    cleanup_log_reset();
    {
        int a __attribute__((cleanup(cleanup_int))) = 1;
        int b __attribute__((cleanup(cleanup_int))) = 2;
        int c __attribute__((cleanup(cleanup_int))) = 3;
        (void)a; (void)b; (void)c;
    }
    AssertEq(g_cleanup_log_n, 3);
    AssertEq(g_cleanup_log[0], 3);
    AssertEq(g_cleanup_log[1], 2);
    AssertEq(g_cleanup_log[2], 1);
}

[[cccc::test]]
static void test_nested_scope_order(void) {
    cleanup_log_reset();
    {
        int outer __attribute__((cleanup(cleanup_int))) = 10;
        (void)outer;
        {
            int inner __attribute__((cleanup(cleanup_int))) = 20;
            (void)inner;
        }
    }
    AssertEq(g_cleanup_log_n, 2);
    AssertEq(g_cleanup_log[0], 20);
    AssertEq(g_cleanup_log[1], 10);
}

[[cccc::test]]
static void test_cleanup_on_break(void) {
    cleanup_log_reset();
    for (int i = 0; i < 3; i++) {
        int x __attribute__((cleanup(cleanup_int))) = i + 1;
        (void)x;
        if (i == 1)
            break;
    }
    AssertEq(g_cleanup_log_n, 2);
    AssertEq(g_cleanup_log[0], 1);
    AssertEq(g_cleanup_log[1], 2);
}

[[cccc::test]]
static void test_cleanup_on_continue(void) {
    cleanup_log_reset();
    for (int i = 0; i < 3; i++) {
        int x __attribute__((cleanup(cleanup_int))) = i + 1;
        (void)x;
        if (i == 1)
            continue;
    }
    AssertEq(g_cleanup_log_n, 3);
    AssertEq(g_cleanup_log[0], 1);
    AssertEq(g_cleanup_log[1], 2);
    AssertEq(g_cleanup_log[2], 3);
}

static int g_inline_cleanup_ran = 0;

static inline void inline_cleanup(int *p) {
    (void)p;
    g_inline_cleanup_ran = 1;
}

[[cccc::test]]
static void test_static_inline_cleanup_not_stripped(void) {
    g_inline_cleanup_ran = 0;
    {
        int x __attribute__((cleanup(inline_cleanup))) = 5;
        (void)x;
    }
    AssertEq(g_inline_cleanup_ran, 1);
}

static int early_return_from_nested(bool flag) {
    int outer __attribute__((cleanup(cleanup_int))) = 100;
    (void)outer;
    {
        int inner __attribute__((cleanup(cleanup_int))) = 200;
        (void)inner;
        if (flag)
            return 99;
    }
    return 0;
}

[[cccc::test]]
static void test_early_return_from_nested_block(void) {
    cleanup_log_reset();
    int r = early_return_from_nested(true);
    AssertEq(r, 99);
    AssertEq(g_cleanup_log_n, 2);
    AssertEq(g_cleanup_log[0], 200);
    AssertEq(g_cleanup_log[1], 100);
}

static int goto_out_of_inner(void) {
    int result = 0;
    {
        int x __attribute__((cleanup(cleanup_int))) = 55;
        (void)x;
        goto done;
        result = 99;
    }
done:
    return result;
}

[[cccc::test]]
static void test_goto_out_of_inner_block(void) {
    cleanup_log_reset();
    int r = goto_out_of_inner();
    AssertEq(r, 0);
    AssertEq(g_cleanup_log_n, 1);
    AssertEq(g_cleanup_log[0], 55);
}

static void same_scope_forward_goto(void) {
    int x __attribute__((cleanup(cleanup_int))) = 77;
    (void)x;
    goto done;
done:;
}

[[cccc::test]]
static void test_same_scope_forward_goto_no_double_cleanup(void) {
    cleanup_log_reset();
    same_scope_forward_goto();
    AssertEq(g_cleanup_log_n, 1);
    AssertEq(g_cleanup_log[0], 77);
}

static int goto_skip_two_scopes(void) {
    int outer __attribute__((cleanup(cleanup_int))) = 10;
    (void)outer;
    {
        int mid __attribute__((cleanup(cleanup_int))) = 20;
        (void)mid;
        {
            int inner __attribute__((cleanup(cleanup_int))) = 30;
            (void)inner;
            goto top;
        }
    }
top:
    return 0;
}

[[cccc::test]]
static void test_goto_skip_multiple_scopes(void) {
    cleanup_log_reset();
    int r = goto_skip_two_scopes();
    AssertEq(r, 0);
    AssertEq(g_cleanup_log_n, 3);
    AssertEq(g_cleanup_log[0], 30);
    AssertEq(g_cleanup_log[1], 20);
    AssertEq(g_cleanup_log[2], 10);
}

static void cleanup_tag_99(int *p) { (void)p; cleanup_log_val(99); }

static void cross_sibling_goto(void) {
    {
        int a __attribute__((cleanup(cleanup_int))) = 7;
        (void)a;
        goto L;
    }
    {
        int b __attribute__((cleanup(cleanup_tag_99))) = 0;
        (void)b;
    L:;
    }
}

[[cccc::test]]
static void test_cross_sibling_goto_cleans_source(void) {
    cleanup_log_reset();
    cross_sibling_goto();
    AssertEq(g_cleanup_log_n, 2);
    AssertEq(g_cleanup_log[0], 7);
    AssertEq(g_cleanup_log[1], 99);
}

static void cross_sibling_goto_label_first(void) {
    {
        int a __attribute__((cleanup(cleanup_int))) = 7;
        (void)a;
        goto M;
    }
    {
    M:;
        int b __attribute__((cleanup(cleanup_int))) = 8;
        (void)b;
    }
}

[[cccc::test]]
static void test_cross_sibling_goto_label_before_decl(void) {
    cleanup_log_reset();
    cross_sibling_goto_label_first();
    AssertEq(g_cleanup_log_n, 2);
    AssertEq(g_cleanup_log[0], 7);
    AssertEq(g_cleanup_log[1], 8);
}

#pragma cccc suite end

// [from test_attribute_test_gnu.c]
// Tests for bare __attribute__((test)) / __attribute__((test_setup)) /
// __attribute__((test_teardown)) forms (ticket #348).
#pragma cccc suite begin "attributes/gnu_test"

static int gnu_setup_count    = 0;
static int gnu_teardown_count = 0;

__attribute__((test_setup(suite = "attributes/gnu_test")))
void gnu_test_setup(void) { gnu_setup_count++; }

__attribute__((test_teardown(suite = "attributes/gnu_test")))
void gnu_test_teardown(void) { gnu_teardown_count++; }

__attribute__((test))
void test_gnu_bare(void) {
    AssertEq(1 + 1, 2);
}

__attribute__((test(name = "gnu with args")))
void test_gnu_with_args(void) {
    AssertEq(6 * 7, 42);
}

__attribute__((test))
void test_gnu_hooks_ran(void) {
    Assert(gnu_setup_count > 0);
}

static int gnu_once_count = 0;

__attribute__((test_setup(suite = "attributes/gnu_test/once", once)))
void gnu_once_setup(void) { gnu_once_count++; }

__attribute__((test(suite = "attributes/gnu_test/once")))
void test_gnu_once_setup_a(void) {
    AssertEq(gnu_once_count, 1);
}

__attribute__((test(suite = "attributes/gnu_test/once")))
void test_gnu_once_setup_b(void) {
    AssertEq(gnu_once_count, 1);
}

#if !__has_attribute(test)
#error "__has_attribute(test) should be 1"
#endif
#if !__has_attribute(test_setup)
#error "__has_attribute(test_setup) should be 1"
#endif
#if !__has_attribute(test_teardown)
#error "__has_attribute(test_teardown) should be 1"
#endif
#if !__has_c_attribute(cccc::test)
#error "__has_c_attribute(cccc::test) should be 1"
#endif
#if !__has_c_attribute(cccc::test_setup)
#error "__has_c_attribute(cccc::test_setup) should be 1"
#endif
#if !__has_c_attribute(cccc::test_teardown)
#error "__has_c_attribute(cccc::test_teardown) should be 1"
#endif

#pragma cccc suite end
