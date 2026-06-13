/*
 * Test that volatile, restrict, register, and inline keywords
 * are accepted by the compiler (even though they may be ignored)
 */

// Test inline functions
inline int inline_add(int a, int b) {
    return a + b;
}

// Test static inline (common pattern)
static inline int static_inline_multiply(int a, int b) {
    return a * b;
}

// Test volatile (commonly used for hardware registers, signal handlers)
int test_volatile() {
    volatile int x = 10;
    x = 20;
    return x;
}

// Test register (optimization hint from old C code)
int test_register() {
    register int i;
    register int sum = 0;
    for (i = 0; i < 10; i++) {
        sum += i;
    }
    return sum;
}

// Test restrict (pointer aliasing hint)
void test_restrict(int *restrict a, int *restrict b, int n) {
    for (int i = 0; i < n; i++) {
        a[i] = b[i] + 1;
    }
}

// Test __restrict and __restrict__ variants
void test_restrict_variants(int *__restrict a, int *__restrict__ b) {
    *a = *b + 1;
}

// Test volatile in function parameters
int test_volatile_param(volatile int *ptr) {
    return *ptr;
}

// Test const volatile (common for memory-mapped I/O)
int test_const_volatile() {
    const volatile int x = 42;
    return x;
}

// Test inline with extern (external inline function)
extern inline int extern_inline_subtract(int a, int b) {
    return a - b;
}

// Test multiple qualifiers on pointers
void test_pointer_qualifiers(
    int *const p1,                    // const pointer
    const int *p2,                    // pointer to const
    volatile int *p3,                 // pointer to volatile
    int *restrict p4,                 // restrict pointer
    const volatile int *restrict p5   // all qualifiers
) {
    // Just test that declaration is accepted (use proper (void) casts)
    (void)p1;
    (void)p2;
    (void)p3;
    (void)p4;
    (void)p5;
}

// Test in array dimensions
void test_array_restrict(int arr[restrict static 10]) {
    arr[0] = 1;
}

// Test auto (C23 type inference)
int test_auto() {
    auto x = 5;
    return x;
}

int main() {
    int result = 0;
    
    // Test inline functions
    result += inline_add(5, 3);  // 8
    result += static_inline_multiply(2, 4);  // 8, total: 16
    
    // Test volatile
    result += test_volatile();  // 20, total: 36
    
    // Test register
    result += test_register();  // 45 (0+1+...+9), total: 81
    
    // Test restrict with arrays
    int a[5];
    int b[5] = {1, 2, 3, 4, 5};
    test_restrict(a, b, 5);
    result += a[0];  // 2, total: 83
    
    // Test restrict variants
    int x = 10, y = 20;
    test_restrict_variants(&x, &y);
    result += x;  // 21, total: 104
    
    // Test volatile parameter
    volatile int v = 15;
    result += test_volatile_param(&v);  // 15, total: 119
    
    // Test const volatile
    result += test_const_volatile();  // 42, total: 161
    
    // Test extern inline
    result += extern_inline_subtract(10, 5);  // 5, total: 166
    
    // Test auto
    result += test_auto();  // 5, total: 171

    if (result != 171) return 1;  // Assert result == 171
    return 42;
}
