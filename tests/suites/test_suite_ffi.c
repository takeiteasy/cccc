// CCCC_FLAGS: --testing
// Consolidated suite: FFI, dlfcn, float FFI
// Source tests: test_dlfcn, test_dlfcn_call, test_dlfcn_close_no_symbols, test_dlfcn_missing, test_ffi_puts, test_ffi_simple, test_ffi_strlen, test_float_ffi, test_float_funcall

#include <dlfcn.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// [from test_ffi_puts]
// Test just puts
// Expected return: 42

// [from test_ffi_simple]
// Simple FFI test
// Expected return: 42

// [from test_ffi_strlen]
// Test just strlen
// Expected return: 42

// [from test_float_ffi]
// Tests that float-returning math functions (registered with returns_double=2)
// produce correct float results via FFI (#406).

static int feq(float a, float b) {
    float diff = a - b;
    if (diff < 0.0f) diff = -diff;
    float mag = b < 0.0f ? -b : b;
    if (mag < 1e-6f) mag = 1e-6f;
    return diff / mag < 1e-5f;
}

// Wrappers for the CALLN (indirect/function-pointer) path

static float wrap_sqrtf(float x) { return sqrtf(x); }

static float wrap_hypotf(float x, float y) { return hypotf(x, y); }

// [from test_float_funcall]
// Test floating-point function parameters and return values
// Expected return: 42

static double add(double a, double b) {
    return a + b;
}

static double multiply(double x, double y) {
    return x * y;
}

static double subtract(double a, double b) {
    return a - b;
}

#pragma cccc suite begin "ffi"

// test_dlfcn
[[cccc::test(return = 42)]]
int test_dlfcn(void) {
    void *handle = dlopen(0, RTLD_LAZY);
    if (!handle) return 1;

    void *sym = dlsym(handle, "printf");
    if (!sym) return 2;

    if (dlclose(handle) == 0) return 3;
    return 42;
}

// test_dlfcn_call
[[cccc::test(return = 42)]]
int test_dlfcn_call(void) {
    void *handle = dlopen(0, RTLD_NOW);
    if (!handle) return 1;

    unsigned long (*fn)(const char *) = dlsym(handle, "strlen");
    if (!fn) return 2;

    if (fn("dynamic") != 7) return 3;
    if (fn("") != 0) return 4;
    return 42;
}

// test_dlfcn_close_no_symbols
[[cccc::test(return = 42)]]
int test_dlfcn_close_no_symbols(void) {
    void *handle = dlopen(0, RTLD_NOW);
    if (!handle) return 1;
    if (dlclose(handle) != 0) return 2;
    return 42;
}

// test_dlfcn_missing
[[cccc::test(return = 42)]]
int test_dlfcn_missing(void) {
    void *handle = dlopen(0, RTLD_NOW);
    if (!handle) return 1;

    void *sym = dlsym(handle, "__builtin_symbol_that_should_not_exist__");
    if (sym) return 2;
    if (!dlerror()) return 3;
    if (dlclose(handle) != 0) return 4;
    return 42;
}

// test_ffi_puts
[[cccc::test(return = 42)]]
int test_ffi_puts(void) {
    puts("Hello from FFI!");
    return 42;
}

// test_ffi_simple
[[cccc::test(return = 42)]]
int test_ffi_simple(void) {
    // Test strlen
    char *str = "hello";
    puts(str);  // Should print "hello"
    
    int len = strlen(str);
    putchar('0' + len);  // Should print length as digit
    putchar('\n');
    
    if (len != 5) {
        puts("strlen failed");
        return 1;
    }
    
    // Test strcmp
    char *s1 = "hello";
    char *s2 = "hello";
    int cmp = strcmp(s1, s2);
    
    if (cmp != 0) {
        puts("strcmp failed");
        return 2;
    }
    
    puts("All tests passed!");
    return 42;
}

// test_ffi_strlen
[[cccc::test(return = 42)]]
int test_ffi_strlen(void) {
    char *str = "hello world";
    puts(str);  // This works - should print "hello world"
    
    long len;
    len = strlen(str);  // Assign return value
    
    puts("After strlen");
    
    // Try to use len - don't do any arithmetic yet
    if (len) {
        puts("len is nonzero");
    }
    
    return 42;
}

// test_float_ffi
[[cccc::test(return = 42)]]
int test_float_ffi(void) {
    // Basic float FFI via CALLF (direct, statically registered)
    if (!feq(sqrtf(16.0f), 4.0f)) return 1;
    if (!feq(fabsf(-3.5f), 3.5f)) return 2;
    if (!feq(fmodf(5.5f, 2.0f), 1.5f)) return 3;

    // Transcendental functions
    if (!feq(expf(0.0f), 1.0f)) return 4;
    if (!feq(logf(1.0f), 0.0f)) return 5;
    if (!feq(powf(2.0f, 10.0f), 1024.0f)) return 6;

    // Trig
    if (!feq(sinf(0.0f), 0.0f)) return 7;
    if (!feq(cosf(0.0f), 1.0f)) return 8;

    // Multi-arg float FFI
    if (!feq(hypotf(3.0f, 4.0f), 5.0f)) return 9;
    if (!feq(atan2f(0.0f, 1.0f), 0.0f)) return 10;

    // CALLN path: call float-returning function via function pointer
    float (*fn1)(float) = wrap_sqrtf;
    if (!feq(fn1(25.0f), 5.0f)) return 11;

    float (*fn2)(float, float) = wrap_hypotf;
    if (!feq(fn2(5.0f, 12.0f), 13.0f)) return 12;

    return 42;
}

// test_float_funcall
[[cccc::test(return = 42)]]
int test_float_funcall(void) {
    // Test 1: Simple function call with float params
    double result = add(20.0, 22.0);
    if (result != 42.0) return 1;
    
    // Test 2: Multiplication
    double prod = multiply(6.0, 7.0);
    if (prod != 42.0) return 2;
    
    // Test 3: Subtraction
    double diff = subtract(50.0, 8.0);
    if (diff != 42.0) return 3;
    
    // Test 4: Nested function calls
    double nested = add(multiply(2.0, 20.0), 2.0);  // (2*20) + 2 = 42
    if (nested != 42.0) return 4;
    
    // Test 5: Variable arguments
    double x = 10.0;
    double y = 32.0;
    double sum = add(x, y);
    if (sum != 42.0) return 5;
    
    return 42;
}

// [from test_ffi_allow_zero] -- --ffi-allow restricts which FFI calls succeed
[[cccc::test(return = 42, flags = "--ffi-allow=strlen")]]
int test_ffi_allow_zero(void) {
    if (strlen("allowed") != 7)
        return 1;
    // puts is not in the allow list; returns 0 (blocked) when ffi_errors_fatal is off
    if (puts("blocked") != 0)
        return 2;
    return 42;
}

#pragma cccc suite end
