// CCCC_FLAGS: --testing
// Consolidated suite: functions: args, nested, funcall, K&R, static_assert, misc
// Source tests: test_assign_lhs_call, test_basic_args, test_comprehensive, test_fortytwo, test_knr_funcdef, test_local_in_func, test_long_chain_add, test_main_ok, test_many_fixed_params, test_negative, test_nested_func_basic, test_nested_func_double, test_nested_func_outer_vars, test_nested_func_params, test_param_order, test_simple, test_simple_funcall, test_static_assert

#include <string.h>
#include <stdarg.h>

// [from test_assign_lhs_call]
// Regression (#581): when the LHS of an assignment has a function call in its
// address/index expression, codegen evaluates the RHS first into a caller-saved
// temp, then computes the LHS address. The call inside that address computation
// clobbers every caller-saved temp at runtime, destroying the RHS value (and,
// for struct assignment, the RHS source address) before the store. The fix
// spills the held value across the address computation.

typedef struct { int a, b, c; } S;

S items[4];

static int two(void) { return 2; }

// [from test_basic_args]
// Test basic function with multiple arguments

static int add_three(int a, int b, int c) {
    return a + b + c;
}

// [from test_comprehensive]
// Comprehensive arithmetic and operator test for CCCC

// [from test_knr_funcdef]
// K&R-style function definitions
int add(a, b)
int a;
int b;
{ return a + b; }

double scale(x, factor)
double x;
double factor;
{ return x * factor; }

static int id(n)
{ return n; }

// [from test_local_in_func]
// Test local variables in function with params

static int test(int a) {
    int x = 10;
    return x;
}

// [from test_long_chain_add]
// Regression test for ticket #295: long left-associative binary chains
// exhausted the fixed temp-register pool (11 regs, T0-T10).

static int one(void) { return 1; }

// 20-operand int addition chain — well past the old limit of 12.

static int long_int_add(int a, int b, int c, int d, int e, int f, int g,
                        int h, int i, int j, int k, int l, int m, int n,
                        int o, int p, int q, int r, int s, int t) {
    return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p + q + r + s + t;
}

// Subtraction chain — verifies non-commutative ops stay correct.

static int long_int_sub(int a, int b, int c, int d, int e, int f) {
    return a - b - c - d - e - f;
}

// Chain with a function call as one operand — exercises the rhs_has_call
// push/pop path together with a long LHS chain.

static int chain_with_call(int a, int b, int c, int d, int e, int f,
                           int g, int h, int i, int j, int k, int l) {
    return a + b + c + d + e + f + g + h + i + j + k + l + one();
}

// 20-operand float addition chain — exercises the float branch.

static float long_float_add(float a, float b, float c, float d, float e,
                            float f, float g, float h, float i, float j,
                            float k, float l, float m, float n, float o,
                            float p, float q, float r, float s, float t) {
    return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p + q + r + s + t;
}

// [from test_main_ok]
// Main file without errors

// [from test_many_fixed_params]
// Ticket #287: fixed parameters beyond the first 8 are stack-passed and
// copied into callee local slots.

static int sum9(int a, int b, int c, int d, int e, int f, int g, int h, int i) {
    int total = 0;
    total = total + a;
    total = total + b;
    total = total + c;
    total = total + d;
    total = total + e;
    total = total + f;
    total = total + g;
    total = total + h;
    total = total + i;
    return total;
}

static int sum12(int a, int b, int c, int d, int e, int f, int g, int h, int i,
          int j, int k, int l) {
    int total = sum9(a, b, c, d, e, f, g, h, i);
    total = total + j;
    total = total + k;
    total = total + l;
    return total;
}

static int sum16(int a, int b, int c, int d, int e, int f, int g, int h, int i,
          int j, int k, int l, int m, int n, int o, int p) {
    int total = sum12(a, b, c, d, e, f, g, h, i, j, k, l);
    total = total + m;
    total = total + n;
    total = total + o;
    total = total + p;
    return total;
}

static int mixed_stack_float(int a, int b, int c, int d, int e, int f, int g, int h,
                      double i, float j, int k) {
    int total = sum9(a, b, c, d, e, f, g, h, (int)i);
    total = total + (int)j;
    total = total + k;
    return total;
}

static int forward12(int a, int b, int c, int d, int e, int f, int g, int h, int i,
              int j, int k, int l) {
    return sum12(a, b, c, d, e, f, g, h, i, j, k, l);
}

static int fixed9_then_varargs(int a, int b, int c, int d, int e, int f, int g,
                        int h, int i, ...) {
    va_list ap;
    va_start(ap, i);
    int j = va_arg(ap, int);
    int k = va_arg(ap, int);
    va_end(ap);
    int total = sum9(a, b, c, d, e, f, g, h, i);
    total = total + j;
    total = total + k;
    return total;
}

// [from test_negative]
// Test negative numbers

// [from test_nested_func_basic]
/*
 * Test: Basic nested function definition and call
 * Tests simple nested function with no outer variable access
 */

// [from test_nested_func_double]
/*
 * Test: Nested functions with double arguments and return types
 */

// [from test_nested_func_outer_vars]
/*
 * Test: Nested function accessing outer function's local variables
 * Tests static chain access for outer scope variables
 */

int outer_result = 0;

// [from test_nested_func_params]
/*
 * Test: Nested function accessing outer function's parameters
 * Tests static chain access for parameters (which are local to the function)
 */

static int test_params(int a, int b) {
    int sum() {
        return a + b;  // Access outer's parameters
    }
    
    int diff() {
        return a - b;  // Access outer's parameters
    }
    
    if (sum() != a + b) return 1;
    if (diff() != a - b) return 2;
    
    return 0;
}

// [from test_param_order]
// Debug _param_order_test - checking parameter order
// Expected: first param should be "a", second should be "b"

static int _param_order_test(int a, int b) {
    // If a=10, b=20, return should be 30
    int sum = a + b;
    return sum;
}

// [from test_simple_funcall]
// Simplest possible function call test
// Expected return: 42

static int foo() {
    return 42;
}

// [from test_static_assert]
// Test _Static_assert compile-time assertions

// Global scope assertions
_Static_assert(1, "This should pass");
_Static_assert(sizeof(int) >= 4, "int must be at least 4 bytes");
_Static_assert(sizeof(char) == 1, "char must be 1 byte");

// Note: To test failure cases, uncomment these (they should cause compile errors):
// _Static_assert(0, "This should fail at compile time");
// _Static_assert(1 == 2, "This should also fail");
// _Static_assert(sizeof(int) < 4, "This will fail on most platforms");

#pragma cccc suite begin "functions"

// test_assign_lhs_call
[[cccc::test(return = 42)]]
int test_assign_lhs_call(void) {
    // Scalar: RHS value 's' held across the strlen() call in the LHS index.
    char form[8];
    strcpy(form, "%f");
    form[strlen(form) - 1] = 's';
    if (strcmp(form, "%s") != 0) return 1;

    // Struct: RHS source address held across the call in the LHS index.
    S v = {10, 20, 30};
    items[two()] = v;
    if (items[2].a != 10 || items[2].b != 20 || items[2].c != 30) return 2;

    return 42;
}

// test_basic_args
[[cccc::test(return = 42)]]
int test_basic_args(void) {
    int result = add_three(10, 20, 30);
    if (result != 60) return 1;  // Assert result == 60
    return 42;
}

// test_comprehensive
[[cccc::test(return = 42)]]
int test_comprehensive(void) {
    int passed = 0;
    int failed = 0;
    
    // Test 1: Basic arithmetic
    if ((5 + 3) == 8) passed = passed + 1; else failed = failed + 1;
    if ((10 - 4) == 6) passed = passed + 1; else failed = failed + 1;
    if ((7 * 2) == 14) passed = passed + 1; else failed = failed + 1;
    if ((20 / 4) == 5) passed = passed + 1; else failed = failed + 1;
    if ((17 % 5) == 2) passed = passed + 1; else failed = failed + 1;
    
    // Test 2: Comparisons
    if (10 == 10) passed = passed + 1; else failed = failed + 1;
    if (10 != 5) passed = passed + 1; else failed = failed + 1;
    if (5 < 10) passed = passed + 1; else failed = failed + 1;
    if (10 <= 10) passed = passed + 1; else failed = failed + 1;
    if (10 > 5) passed = passed + 1; else failed = failed + 1;
    
    // Test 3: Bitwise operations
    if ((12 | 10) == 14) passed = passed + 1; else failed = failed + 1;
    if ((12 ^ 10) == 6) passed = passed + 1; else failed = failed + 1;
    if ((12 & 10) == 8) passed = passed + 1; else failed = failed + 1;
    if ((1 << 3) == 8) passed = passed + 1; else failed = failed + 1;
    if ((16 >> 2) == 4) passed = passed + 1; else failed = failed + 1;
    
    // Test 4: Unary operators
    if ((-5) == -5) passed = passed + 1; else failed = failed + 1;
    if (!0) passed = passed + 1; else failed = failed + 1;
    if (!(!1)) passed = passed + 1; else failed = failed + 1;
    if ((~0) == -1) passed = passed + 1; else failed = failed + 1;
    
    // Test 5: Logical operators
    if (1 && 1) passed = passed + 1; else failed = failed + 1;
    if (!(0 && 1)) passed = passed + 1; else failed = failed + 1;
    if (1 || 0) passed = passed + 1; else failed = failed + 1;
    if (!(0 || 0)) passed = passed + 1; else failed = failed + 1;
    
    // Test 6: Control flow
    int sum = 0;
    int i;
    for (i = 1; i <= 10; i = i + 1) {
        sum = sum + i;
    }
    if (sum == 55) passed = passed + 1; else failed = failed + 1;
    
    // Test 7: Pointers
    int val = 99;
    int *ptr = &val;
    if (*ptr == 99) passed = passed + 1; else failed = failed + 1;
    *ptr = 88;
    if (val == 88) passed = passed + 1; else failed = failed + 1;
    
    // Return passed tests (should be 26 if all work)
    if (passed != 26) return 1;  // Assert all 26 tests passed
    return 42;
}

// test_fortytwo
[[cccc::test(return = 42)]]
int test_fortytwo(void) {
    return 42;
}

// test_knr_funcdef
[[cccc::test(return = 42)]]
int test_knr_funcdef(void) {
    if (add(19, 23) != 42) return 1;
    if ((int)scale(3.0, 14.0) != 42) return 2;
    if (id(42) != 42) return 3;
    return 42;
}

// test_local_in_func
[[cccc::test(return = 42)]]
int test_local_in_func(void) {
    int result = test(99);  // Should return 10, ignoring the param
    if (result != 10) return 1;  // Assert result == 10
    return 42;
}

// test_long_chain_add
[[cccc::test(return = 42)]]
int test_long_chain_add(void) {
    // 1+2+...+20 = 210
    int sum = long_int_add(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20);
    if (sum != 210) return 1;

    // 100 - 1 - 2 - 3 - 4 - 5 = 85
    int diff = long_int_sub(100, 1, 2, 3, 4, 5);
    if (diff != 85) return 2;

    // 1+2+...+12+1 = 79
    int mixed = chain_with_call(1,2,3,4,5,6,7,8,9,10,11,12);
    if (mixed != 79) return 3;

    // 1.0+2.0+...+20.0 = 210.0
    float fsum = long_float_add(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20);
    if ((int)fsum != 210) return 4;

    return 42;
}

// test_main_ok
[[cccc::test(return = 42)]]
int test_main_ok(void) {
    return 42;
}

// test_many_fixed_params
[[cccc::test(return = 42)]]
int test_many_fixed_params(void) {
    if (sum9(1, 2, 3, 4, 5, 6, 7, 8, 9) != 45)
        return 1;
    if (sum12(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12) != 78)
        return 2;
    if (sum16(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16) != 136)
        return 3;
    if (mixed_stack_float(1, 2, 3, 4, 5, 6, 7, 8, 9.0, 10.0f, 11) != 66)
        return 4;
    if (forward12(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12) != 78)
        return 5;
    if (fixed9_then_varargs(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11) != 66)
        return 6;
    return 42;
}

// test_negative
[[cccc::test(return = 42)]]
int test_negative(void) {
    int a = -5;
    int result = 0 - a;  // Should be 5
    if (result != 5) return 1;  // Assert result == 5
    return 42;
}

// test_nested_func_basic
[[cccc::test(return = 42)]]
int test_nested_func_basic(void) {
    int add(int a, int b) {
        return a + b;
    }
    
    int result = add(40, 2);
    if (result != 42) return 1;
    
    // Test calling nested function multiple times
    if (add(10, 20) != 30) return 2;
    if (add(0, 0) != 0) return 3;
    
    return 42;
}

// test_nested_func_double
[[cccc::test(return = 42)]]
int test_nested_func_double(void) {
    double outer_d = 3.14;

    // Test 1: Nested function with double arguments
    double add_doubles(double a, double b) {
        return a + b;
    }

    if (add_doubles(1.5, 2.5) != 4.0) return 1;

    // Test 2: Nested function accessing outer double
    double add_outer(double v) {
        return v + outer_d;
    }

    if (add_outer(10.0) != 13.14) return 2;

    // Test 3: Mixed arguments (int, double) 
    // This tests if static link (A0) interferes with FP regs
    double mixed(int i, double d) {
        return i + d;
    }

    // i passed in A1 (A0 is s-link), d passed in FA0
    if (mixed(10, 5.5) != 15.5) return 3;

    // Test 4: Verify static link doesn't shift FP args incorrectly
    // mixed2(double a, int b) -> a in FA0, b in A1 (A0 is s-link)
    double mixed2(double a, int b) {
        return a + b;
    }
    
    if (mixed2(10.5, 20) != 30.5) return 4;
    
    // Test 5: Verify outer double variable update
    void update_outer(double val) {
        outer_d = val;
    }
    
    update_outer(99.9);
    if (outer_d != 99.9) return 5;

    return 42;
}

// test_nested_func_outer_vars
[[cccc::test(return = 42)]]
int test_nested_func_outer_vars(void) {
    int x = 10;
    int y = 20;
    
    int get_x() {
        return x;  // Access outer's x via static chain
    }
    
    int get_y() {
        return y;  // Access outer's y via static chain
    }
    
    int add_xy() {
        return x + y;  // Access both outer variables
    }
    
    // Test simple access
    if (get_x() != 10) return 1;
    if (get_y() != 20) return 2;
    if (add_xy() != 30) return 3;
    
    // Modify outer variables and verify nested function sees changes
    x = 100;
    if (get_x() != 100) return 4;
    
    y = 200;
    if (get_y() != 200) return 5;
    if (add_xy() != 300) return 6;
    
    return 42;
}

// test_nested_func_params
[[cccc::test(return = 42)]]
int test_nested_func_params(void) {
    if (test_params(30, 12) != 0) return 1;  // Tests sum=42, diff=18
    if (test_params(100, 50) != 0) return 2; // Tests sum=150, diff=50
    if (test_params(5, 5) != 0) return 3;    // Tests sum=10, diff=0
    
    return 42;
}

// test_param_order
[[cccc::test(return = 42)]]
int test_param_order(void) {
    int result = _param_order_test(10, 20);
    if (result != 30) return 1;  // Assert result == 30
    return 42;
}

// test_simple
[[cccc::test(return = 42)]]
int test_simple(void) {
    return 42;
}

// test_simple_funcall
[[cccc::test(return = 42)]]
int test_simple_funcall(void) {
    return foo();
}

// test_static_assert
[[cccc::test(return = 42)]]
int test_static_assert(void) {
    // Function scope assertions
    _Static_assert(1 + 1 == 2, "Math doesn't work!");
    _Static_assert(10 > 5, "Comparison doesn't work!");
    
    // Test with sizeof
    _Static_assert(sizeof(long) >= 8, "long must be at least 8 bytes");
    _Static_assert(sizeof(double) == 8, "double must be 8 bytes");
    
    // Test with expressions
    _Static_assert((3 * 4) == 12, "multiplication doesn't work");
    _Static_assert((20 / 4) == 5, "division doesn't work");
    
    // Test with bitwise operations
    _Static_assert((0xFF & 0x0F) == 0x0F, "bitwise AND doesn't work");
    _Static_assert((0xF0 | 0x0F) == 0xFF, "bitwise OR doesn't work");
    
    // Test in nested blocks
    {
        _Static_assert(1, "nested block assertion");
        {
            _Static_assert(1, "deeply nested assertion");
        }
    }
    
    // Test with pointer sizes
    _Static_assert(sizeof(void*) == 8, "pointers must be 8 bytes");
    _Static_assert(sizeof(int*) == 8, "int pointers must be 8 bytes");
    
    return 42;  // Success
}

#pragma cccc suite end
