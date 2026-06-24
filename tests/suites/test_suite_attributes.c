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
