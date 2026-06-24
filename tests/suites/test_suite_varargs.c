// CCCC_FLAGS: --testing
// Consolidated suite: variadic functions, va_list, nested varargs
// Source tests: test_nested_vararg, test_varargs_builtin_va, test_varargs_comprehensive_v2, test_varargs_double_medium, test_varargs_double_simple, test_varargs_float, test_varargs_int, test_varargs_nested_double, test_varargs_simple, test_varargs_struct, test_varargs_struct_double, test_varargs_struct_simple, test_varargs_struct_simple2, test_varargs_struct_va, test_varargs_vacopy_double

#include "stdarg.h"
#include <stdarg.h>

// [from test_nested_vararg]
// Nested variadic test with val*2

static int inner(int n, ...) {
    va_list args;
    va_start(args, n);
    int a = va_arg(args, int);
    int b = va_arg(args, int);
    va_end(args);
    return a + b;
}

static int outer(int count, ...) {
    va_list args;
    va_start(args, count);
    
    int total = 0;
    for (int i = 0; i < count; i++) {
        int val = va_arg(args, int);
        total += inner(2, val, val * 2);  // <-- val*2 instead of val
    }
    
    va_end(args);
    return total;
}

// [from test_varargs_builtin_va]
/*
 * Test __builtin_va_* macros (ticket #513)
 *
 * These are aliases defined in <stdarg.h> that forward to the va_* macros.
 */

static int sum(int n, ...) {
    va_list ap;
    __builtin_va_start(ap, n);
    int total = 0;
    for (int i = 0; i < n; i++)
        total += __builtin_va_arg(ap, int);
    __builtin_va_end(ap);
    return total;
}

static int copy_sum(int n, ...) {
    va_list ap, ap2;
    __builtin_va_start(ap, n);
    __builtin_va_copy(ap2, ap);
    // consume from copy
    int total = 0;
    for (int i = 0; i < n; i++)
        total += __builtin_va_arg(ap2, int);
    __builtin_va_end(ap2);
    __builtin_va_end(ap);
    return total;
}

// [from test_varargs_comprehensive_v2]
/*
 * Test: Variable Argument Lists - Comprehensive (Simplified)
 * 
 * This test validates that varargs work correctly with doubles and mixed types.
 */

// Helper: Compare doubles with epsilon

static int double_equal(double a, double b, double epsilon) {
    double diff = a - b;
    if (diff < 0.0) diff = -diff;
    return diff < epsilon;
}

// Test 1: Simple integer sum (baseline)

static int sum_ints(int count, ...) {
    va_list args;
    va_start(args, count);
    
    int sum = 0;
    for (int i = 0; i < count; i++) {
        sum += va_arg(args, int);
    }
    
    va_end(args);
    return sum;
}

// Test 2: Simple double sum

static double sum_doubles(int count, ...) {
    va_list args;
    va_start(args, count);
    
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += va_arg(args, double);
    }
    
    va_end(args);
    return sum;
}

// Test 3: Mixed int and double arguments

static double mixed_sum(int count, ...) {
    va_list args;
    va_start(args, count);
    
    int int_sum = 0;
    double double_sum = 0.0;
    
    for (int i = 0; i < count; i++) {
        if (i % 2 == 0) {
            int_sum += va_arg(args, int);
        } else {
            double_sum += va_arg(args, double);
        }
    }
    
    va_end(args);
    return (double)int_sum + double_sum;
}

// Test 4: va_copy with doubles

static double test_va_copy_double(int count, ...) {
    va_list args1, args2;
    va_start(args1, count);
    va_copy(args2, args1);
    
    double sum1 = 0.0;
    for (int i = 0; i < count; i++) {
        sum1 += va_arg(args1, double);
    }
    
    double sum2 = 0.0;
    for (int i = 0; i < count; i++) {
        sum2 += va_arg(args2, double);
    }
    
    va_end(args1);
    va_end(args2);
    
    double diff = sum1 - sum2;
    if (diff < 0.0) diff = -diff;
    return (diff < 0.0001) ? sum1 : -1.0;
}

// Test 5: Nested variadic calls with doubles

static double inner_double_sum(int n, ...) {
    va_list args;
    va_start(args, n);
    
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        sum += va_arg(args, double);
    }
    
    va_end(args);
    return sum;
}

static double outer_double_call(int count, ...) {
    va_list args;
    va_start(args, count);
    
    double total = 0.0;
    for (int i = 0; i < count; i++) {
        double val = va_arg(args, double);
        total += inner_double_sum(2, val, val * 2.0);
    }
    
    va_end(args);
    return total;
}

// Test 6: Printf-style formatting

static int simple_format(char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    int a = va_arg(args, int);
    double b = va_arg(args, double);
    int c = va_arg(args, int);
    
    va_end(args);
    return a + (int)b + c;
}

// Test 7: Float arguments

static double sum_floats(int count, ...) {
    va_list args;
    va_start(args, count);
    
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += va_arg(args, double);
    }
    
    va_end(args);
    return sum;
}

// Test 8: Pointer arguments

static int sum_via_pointers(int count, ...) {
    va_list args;
    va_start(args, count);
    
    int sum = 0;
    for (int i = 0; i < count; i++) {
        int *ptr = va_arg(args, int*);
        if (ptr) sum += *ptr;
    }
    
    va_end(args);
    return sum;
}

// Test 9: Many double arguments

static double sum_many_doubles(int count, ...) {
    va_list args;
    va_start(args, count);
    
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += va_arg(args, double);
    }
    
    va_end(args);
    return sum;
}

// [from test_varargs_double_medium]
/*
 * Test: Incremental varargs double tests
 */

// Helper: Compare doubles with epsilon

static int _varargs_double_medium_double_equal(double a, double b, double epsilon) {
    double diff = a - b;
    if (diff < 0.0) diff = -diff;
    return diff < epsilon;
}

// Test 1: Simple double sum

static double _varargs_double_medium_sum_doubles(int count, ...) {
    va_list args;
    va_start(args, count);
    
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += va_arg(args, double);
    }
    
    va_end(args);
    return sum;
}

// Test 2: Mixed int and double

static double _varargs_double_medium_mixed_sum(int count, ...) {
    va_list args;
    va_start(args, count);
    
    int int_sum = 0;
    double double_sum = 0.0;
    
    for (int i = 0; i < count; i++) {
        if (i % 2 == 0) {
            int_sum += va_arg(args, int);
        } else {
            double_sum += va_arg(args, double);
        }
    }
    
    va_end(args);
    return (double)int_sum + double_sum;
}

// [from test_varargs_double_simple]
/*
 * Simple test for double varargs
 */

static double _varargs_double_simple_sum_doubles(int count, ...) {
    va_list args;
    va_start(args, count);
    
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += va_arg(args, double);
    }
    
    va_end(args);
    return sum;
}

// [from test_varargs_float]
/*
 * Test: Float arguments (promoted to double)
 */

static double _varargs_float_sum_floats(int count, ...) {
    va_list args;
    va_start(args, count);
    
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        // Note: float arguments are promoted to double in varargs
        sum += va_arg(args, double);
    }
    
    va_end(args);
    return sum;
}

// [from test_varargs_int]
/*
 * Test: Variable Argument Lists - Integer-only tests
 */

// Test 1: Simple variadic function - sum integers

static int _varargs_int_sum_ints(int count, ...) {
    va_list args;
    va_start(args, count);
    
    int sum = 0;
    for (int i = 0; i < count; i++) {
        sum += va_arg(args, int);
    }
    
    va_end(args);
    return sum;
}

// Test 2: va_copy - copy va_list

static int test_va_copy(int count, ...) {
    va_list args1, args2;
    va_start(args1, count);
    va_copy(args2, args1);
    
    // Process with first list
    int sum1 = 0;
    for (int i = 0; i < count; i++) {
        sum1 += va_arg(args1, int);
    }
    
    // Process with copied list (should get same values)
    int sum2 = 0;
    for (int i = 0; i < count; i++) {
        sum2 += va_arg(args2, int);
    }
    
    va_end(args1);
    va_end(args2);
    
    // Both sums should be equal
    return (sum1 == sum2) ? sum1 : -1;
}

// Test 3: Nested variadic calls

static int inner_vararg(int n, ...) {
    va_list args;
    va_start(args, n);
    
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += va_arg(args, int);
    }
    
    va_end(args);
    return sum;
}

static int outer_vararg(int count, ...) {
    va_list args;
    va_start(args, count);
    
    int total = 0;
    for (int i = 0; i < count; i++) {
        int val = va_arg(args, int);
        // Call another variadic function
        total += inner_vararg(2, val, val * 2);
    }
    
    va_end(args);
    return total;
}

// Test 4: Many arguments (stress test)

static int sum_many(int count, ...) {
    va_list args;
    va_start(args, count);
    
    int sum = 0;
    for (int i = 0; i < count; i++) {
        sum += va_arg(args, int);
    }
    
    va_end(args);
    return sum;
}

// Test 5: Zero variadic arguments (edge case)

static int optional_args(int base, ...) {
    return base;  // Don't use varargs at all
}

// Test 6: Pointer arguments

static int _varargs_int_sum_via_pointers(int count, ...) {
    va_list args;
    va_start(args, count);
    
    int sum = 0;
    for (int i = 0; i < count; i++) {
        int *ptr = va_arg(args, int*);
        if (ptr) sum += *ptr;
    }
    
    va_end(args);
    return sum;
}

// [from test_varargs_nested_double]
/*
 * Test: Nested variadic calls with doubles
 */

static double _varargs_nested_double_inner_double_sum(int n, ...) {
    va_list args;
    va_start(args, n);
    
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        sum += va_arg(args, double);
    }
    
    va_end(args);
    return sum;
}

static double _varargs_nested_double_outer_double_call(int count, ...) {
    va_list args;
    va_start(args, count);
    
    double total = 0.0;
    for (int i = 0; i < count; i++) {
        double val = va_arg(args, double);
        // Call another variadic function with double
        total += _varargs_nested_double_inner_double_sum(2, val, val * 2.0);
    }
    
    va_end(args);
    return total;
}

// [from test_varargs_simple]
/*
 * Simple varargs test - minimal case
 */

static int sum_two(int count, ...) {
    va_list args;
    va_start(args, count);
    
    int a = va_arg(args, int);
    int b = va_arg(args, int);
    
    va_end(args);
    return a + b;
}

// [from test_varargs_struct]
/*
 * Test: Varargs with struct return
 */

typedef struct {
    int int_sum;
    long long_sum;
    double double_sum;
    int ptr_sum;
} AllTypesSums;

static AllTypesSums sum_all_types(int count, ...) {
    va_list args;
    va_start(args, count);
    
    AllTypesSums result = {0, 0, 0.0, 0};
    
    for (int i = 0; i < count; i++) {
        int type = i % 4;
        if (type == 0) {
            result.int_sum += va_arg(args, int);
        } else if (type == 1) {
            result.long_sum += va_arg(args, long);
        } else if (type == 2) {
            result.double_sum += va_arg(args, double);
        } else {
            int *ptr = va_arg(args, int*);
            if (ptr) result.ptr_sum += *ptr;
        }
    }
    
    va_end(args);
    return result;
}

// Helper: Compare doubles with epsilon

static int _varargs_struct_double_equal(double a, double b, double epsilon) {
    double diff = a - b;
    if (diff < 0.0) diff = -diff;
    return diff < epsilon;
}

// [from test_varargs_struct_double]
/*
 * Test: Struct return with double va_arg
 */

typedef struct {
    int a;
    double b;
} Mixed;

static Mixed use_va_arg_double(int x, ...) {
    va_list args;
    va_start(args, x);
    
    double y = va_arg(args, double);
    double z = va_arg(args, double);
    
    va_end(args);
    
    Mixed result = {x, y + z};
    return result;
}

// [from test_varargs_struct_simple]
/*
 * Test: Simple struct with varargs
 */

typedef struct {
    int a;
    int b;
} Simple;

static Simple make_simple(int x, ...) {
    Simple result = {x, x * 2};
    return result;
}

// [from test_varargs_struct_simple2]
/*
 * Test: Varargs with multiple types (simplified struct)
 */

typedef struct {
    int int_sum;
    double double_sum;
} SimpleSums;

static SimpleSums sum_simple_types(int count, ...) {
    va_list args;
    va_start(args, count);
    
    SimpleSums result;
    result.int_sum = 0;
    result.double_sum = 0.0;
    
    for (int i = 0; i < count; i++) {
        int type = i % 2;
        if (type == 0) {
            result.int_sum += va_arg(args, int);
        } else {
            result.double_sum += va_arg(args, double);
        }
    }
    
    va_end(args);
    return result;
}

static int _varargs_struct_simple2_double_equal(double a, double b, double epsilon) {
    double diff = a - b;
    if (diff < 0.0) diff = -diff;
    return diff < epsilon;
}

// [from test_varargs_struct_va]
/*
 * Test: Struct return with va_arg usage
 */

typedef struct {
    int a;
    int b;
} Simple;

static Simple use_va_arg(int x, ...) {
    va_list args;
    va_start(args, x);
    
    int y = va_arg(args, int);
    int z = va_arg(args, int);
    
    va_end(args);
    
    Simple result = {x + y, z};
    return result;
}

// [from test_varargs_vacopy_double]
/*
 * Test: va_copy with doubles
 */

static double _varargs_vacopy_double_test_va_copy_double(int count, ...) {
    va_list args1, args2;
    va_start(args1, count);
    va_copy(args2, args1);
    
    // Process with first list
    double sum1 = 0.0;
    for (int i = 0; i < count; i++) {
        sum1 += va_arg(args1, double);
    }
    
    // Process with copied list
    double sum2 = 0.0;
    for (int i = 0; i < count; i++) {
        sum2 += va_arg(args2, double);
    }
    
    va_end(args1);
    va_end(args2);
    
    // Check if equal
    double diff = sum1 - sum2;
    if (diff < 0.0) diff = -diff;
    if (diff < 0.0001) {
        return sum1;
    } else {
        return -1.0;
    }
}

#pragma cccc suite begin "varargs"

// test_nested_vararg
[[cccc::test(return = 42)]]
int test_nested_vararg(void) {
    // outer(2, 10, 20)
    // i=0: val=10, inner(2,10,20) = 30
    // i=1: val=20, inner(2,20,40) = 60
    // total = 90
    int result = outer(2, 10, 20);
    if (result != 90) return result;
    return 42;
}

// test_varargs_builtin_va
[[cccc::test(return = 42)]]
int test_varargs_builtin_va(void) {
    if (sum(3, 1, 2, 3) != 6) return 1;
    if (sum(0) != 0) return 2;
    if (copy_sum(4, 10, 20, 30, 40) != 100) return 3;
    return 42;
}

// test_varargs_comprehensive_v2
[[cccc::test(return = 42)]]
int test_varargs_comprehensive_v2(void) {
    int result;
    double dresult;
    
    // Test 1: Integer sum
    result = sum_ints(3, 10, 20, 30);
    if (result != 60) return 1;
    
    // Test 2: Simple double sum
    dresult = sum_doubles(3, 1.5, 2.5, 3.0);
    if (!double_equal(dresult, 7.0, 0.0001)) return 2;
    
    // Test 3: More doubles
    dresult = sum_doubles(4, 10.5, 20.25, 30.0, 5.25);
    if (!double_equal(dresult, 66.0, 0.0001)) return 3;
    
    // Test 4: Mixed int and double
    dresult = mixed_sum(4, 10, 1.5, 20, 2.5);
    if (!double_equal(dresult, 34.0, 0.0001)) return 4;
    
    // Test 5: va_copy with doubles
    dresult = test_va_copy_double(3, 5.5, 10.0, 15.5);
    if (!double_equal(dresult, 31.0, 0.0001)) return 5;
    
    // Test 6: Nested variadic calls
    dresult = outer_double_call(2, 1.0, 2.0);
    if (!double_equal(dresult, 9.0, 0.0001)) return 6;
    
    // Test 7: Printf-style
    result = simple_format("test", 10, 5.5, 20);
    if (result != 35) return 7;
    
    // Test 8: Float arguments
    float f1 = 1.5f, f2 = 2.5f, f3 = 3.0f;
    dresult = sum_floats(3, f1, f2, f3);
    if (!double_equal(dresult, 7.0, 0.0001)) return 8;
    
    // Test 9: Pointer arguments
    int a = 5, b = 10, c = 15;
    result = sum_via_pointers(3, &a, &b, &c);
    if (result != 30) return 9;
    
    // Test 10: Many doubles (max 8 args for register-based calling)
    dresult = sum_many_doubles(7, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0);
    if (!double_equal(dresult, 28.0, 0.0001)) return 10;
    
    // Test 11: Large doubles
    dresult = sum_doubles(3, 1000.5, 2000.25, 500.25);
    if (!double_equal(dresult, 3501.0, 0.0001)) return 11;
    
    // Test 12: Negative doubles
    dresult = sum_doubles(4, -10.5, 20.5, -5.0, 15.0);
    if (!double_equal(dresult, 20.0, 0.0001)) return 12;
    
    // Test 13: Small doubles
    dresult = sum_doubles(3, 0.1, 0.2, 0.3);
    if (!double_equal(dresult, 0.6, 0.0001)) return 13;
    
    // Test 14: Struct return with mixed types
    typedef struct {
        int int_sum;
        double double_sum;
    } TestSums;
    
    // Inline function to avoid forward declaration issues
    va_list test_args;
    int test_count = 4;
    TestSums test_result;
    test_result.int_sum = 0;
    test_result.double_sum = 0.0;
    
    // Manually test struct return by calling a function
    // We can't easily define new functions in main, so we skip this
    // The individual test_varargs_struct_simple2.c already tests this
    
    return 42;  // All tests passed!
}

// test_varargs_double_medium
[[cccc::test(return = 42)]]
int test_varargs_double_medium(void) {
    double dresult;
    
    // Test 1: Simple double sum
    dresult = _varargs_double_medium_sum_doubles(2, 1.5, 2.5);
    if (!_varargs_double_medium_double_equal(dresult, 4.0, 0.0001)) return 1;
    
    // Test 2: More doubles
    dresult = _varargs_double_medium_sum_doubles(3, 1.5, 2.5, 3.0);
    if (!_varargs_double_medium_double_equal(dresult, 7.0, 0.0001)) return 2;
    
    // Test 3: Mixed
    dresult = _varargs_double_medium_mixed_sum(4, 10, 1.5, 20, 2.5);
    if (!_varargs_double_medium_double_equal(dresult, 34.0, 0.0001)) return 3;
    
    return 42;
}

// test_varargs_double_simple
[[cccc::test(return = 42)]]
int test_varargs_double_simple(void) {
    // Test: _varargs_double_simple_sum_doubles(3, 1.5, 2.5, 3.0) = 7.0
    double result = _varargs_double_simple_sum_doubles(3, 1.5, 2.5, 3.0);
    
    // Check if result is close to 7.0
    double diff = result - 7.0;
    if (diff < 0.0) diff = -diff;
    if (diff < 0.0001) {
        return 42;  // Success
    }
    return 1;  // Failure
}

// test_varargs_float
[[cccc::test(return = 42)]]
int test_varargs_float(void) {
    // _varargs_float_sum_floats(3, 1.5f, 2.5f, 3.0f) = 7.0
    float f1 = 1.5f, f2 = 2.5f, f3 = 3.0f;
    double result = _varargs_float_sum_floats(3, f1, f2, f3);
    
    double diff = result - 7.0;
    if (diff < 0.0) diff = -diff;
    if (diff > 0.0001) return 1;
    
    return 42;
}

// test_varargs_int
[[cccc::test(return = 42)]]
int test_varargs_int(void) {
    int result = 0;
    
    // Test 1: Simple sum
    // _varargs_int_sum_ints(3, 10, 20, 30) = 60
    result = _varargs_int_sum_ints(3, 10, 20, 30);
    if (result != 60) return 1;
    
    // Test 2: Different counts
    // _varargs_int_sum_ints(5, 1, 2, 3, 4, 5) = 15
    result = _varargs_int_sum_ints(5, 1, 2, 3, 4, 5);
    if (result != 15) return 2;
    
    // Test 3: Single argument
    // _varargs_int_sum_ints(1, 100) = 100
    result = _varargs_int_sum_ints(1, 100);
    if (result != 100) return 3;
    
    // Test 4: va_copy
    // test_va_copy(4, 10, 20, 30, 40) = 100
    result = test_va_copy(4, 10, 20, 30, 40);
    if (result != 100) return 4;
    
    // Test 5: Nested variadic calls
    // outer_vararg(3, 1, 2, 3) = inner_vararg(2,1,2) + inner_vararg(2,2,4) + inner_vararg(2,3,6)
    //                          = 3 + 6 + 9 = 18
    result = outer_vararg(3, 1, 2, 3);
    if (result != 18) return 5;
    
    // Test 6: Many arguments (max 8 total args for register-based calling)
    // sum_many(7, 1, 2, 3, 4, 5, 6, 7) = 28
    result = sum_many(7, 1, 2, 3, 4, 5, 6, 7);
    if (result != 28) return 6;
    
    // Test 7: Zero varargs (edge case)
    // optional_args(42) = 42
    result = optional_args(42);
    if (result != 42) return 7;
    
    // Test 8: Pointer arguments
    int a = 5, b = 10, c = 15;
    // _varargs_int_sum_via_pointers(3, &a, &b, &c) = 30
    result = _varargs_int_sum_via_pointers(3, &a, &b, &c);
    if (result != 30) return 8;
    
    // Test 9: Large numbers
    // _varargs_int_sum_ints(2, 1000, 2000) = 3000
    result = _varargs_int_sum_ints(2, 1000, 2000);
    if (result != 3000) return 9;
    
    return 42;  // All tests passed!
}

// test_varargs_nested_double
[[cccc::test(return = 42)]]
int test_varargs_nested_double(void) {
    // _varargs_nested_double_outer_double_call(2, 1.0, 2.0) = inner(2, 1.0, 2.0) + inner(2, 2.0, 4.0)
    //                                 = 3.0 + 6.0 = 9.0
    double result = _varargs_nested_double_outer_double_call(2, 1.0, 2.0);
    
    double diff = result - 9.0;
    if (diff < 0.0) diff = -diff;
    if (diff > 0.0001) return 1;
    
    return 42;
}

// test_varargs_simple
[[cccc::test(return = 42)]]
int test_varargs_simple(void) {
    int result = sum_two(2, 10, 20);
    if (result != 30) return 1;
    return 42;
}

// test_varargs_struct
[[cccc::test(return = 42)]]
int test_varargs_struct(void) {
    int a = 5, b = 10;
    
    // sum_all_types(4, 10, 100, 5.5, &a) - max 8 args for register-based calling
    AllTypesSums sums = sum_all_types(4, 10, 100, 5.5, &a);
    
    if (sums.int_sum != 10) return 1;
    if (sums.long_sum != 100) return 2;
    if (!_varargs_struct_double_equal(sums.double_sum, 5.5, 0.0001)) return 3;
    if (sums.ptr_sum != 5) return 4;
    
    return 42;
}

// test_varargs_struct_double
[[cccc::test(return = 42)]]
int test_varargs_struct_double(void) {
    Mixed m = use_va_arg_double(5, 10.5, 20.5);
    if (m.a != 5) return 1;
    
    // Check if m.b is close to 31.0
    double diff = m.b - 31.0;
    if (diff < 0.0) diff = -diff;
    if (diff > 0.0001) return 2;
    
    return 42;
}

// test_varargs_struct_simple
[[cccc::test(return = 42)]]
int test_varargs_struct_simple(void) {
    Simple s = make_simple(5, 10, 20);
    if (s.a != 5) return 1;
    if (s.b != 10) return 2;
    return 42;
}

// test_varargs_struct_simple2
[[cccc::test(return = 42)]]
int test_varargs_struct_simple2(void) {
    // sum_simple_types(4, 10, 5.5, 20, 10.5)
    //   int: 10, 20 = 30
    //   double: 5.5, 10.5 = 16.0
    SimpleSums sums = sum_simple_types(4, 10, 5.5, 20, 10.5);
    
    if (sums.int_sum != 30) return 1;
    if (!_varargs_struct_simple2_double_equal(sums.double_sum, 16.0, 0.0001)) return 2;
    
    return 42;
}

// test_varargs_struct_va
[[cccc::test(return = 42)]]
int test_varargs_struct_va(void) {
    Simple s = use_va_arg(5, 10, 20);
    if (s.a != 15) return 1;  // 5 + 10
    if (s.b != 20) return 2;
    return 42;
}

// test_varargs_vacopy_double
[[cccc::test(return = 42)]]
int test_varargs_vacopy_double(void) {
    double result = _varargs_vacopy_double_test_va_copy_double(3, 5.5, 10.0, 15.5);
    
    // Should be 31.0
    double diff = result - 31.0;
    if (diff < 0.0) diff = -diff;
    if (diff > 0.0001) return 1;
    
    return 42;
}

#pragma cccc suite end
