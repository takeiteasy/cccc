// CCCC_FLAGS: --testing
// Consolidated suite: variable-length arrays
// Source tests: test_vla_basic, test_vla_cleanup, test_vla_comprehensive, test_vla_enhancements, test_vla_expr, test_vla_nested, test_vla_param, test_vla_ultra_minimal,
//   test_vla_simple, test_vla_minimal, test_vla_init, test_vla_cleanup_simple, test_vla_with_constructor

// [from test_vla_basic]
// Test: VLA basic allocation
// Expected return: 42

// [from test_vla_cleanup]
// Test VLA memory cleanup on function exit
// Expected return: 42
// This test verifies that VLAs are properly freed when functions return

static int test_vla_in_function() {
    int n = 10;
    int arr[n];
    
    // Use the array
    arr[0] = 42;
    int result = arr[0];
    
    // VLA should be freed automatically when function returns
    return result;
}

static int test_multiple_vlas() {
    int n1 = 5;
    int n2 = 10;
    
    int arr1[n1];
    int arr2[n2];
    
    arr1[0] = 20;
    arr2[0] = 22;
    
    int result = arr1[0] + arr2[0];  // 20 + 22 = 42
    
    // Both VLAs should be freed automatically
    return result;
}

static int test_vla_in_nested_scopes() {
    int result = 0;
    
    {
        int n = 5;
        int arr[n];
        arr[0] = 10;
        result += arr[0];
    }
    
    {
        int n = 8;
        int arr[n];
        arr[0] = 32;
        result += arr[0];
    }
    
    return result;  // 10 + 32 = 42
}

// [from test_vla_comprehensive]
// Test: Comprehensive VLA test with multiple scenarios
// Expected return: 100

static int sum_array(int *arr, int size) {
    int sum = 0;
    for (int i = 0; i < size; i = i + 1) {
        sum = sum + arr[i];
    }
    return sum;
}

// [from test_vla_enhancements]
// Comprehensive test of VLA enhancements
// Expected return: 42

static int _vla_enhancements_test_vla_cleanup() {
    // Test automatic cleanup on return
    int n = 10;
    int arr[n];
    arr[0] = 42;
    return arr[0];  // VLA cleaned up automatically
}

static int test_multiple_returns() {
    int n = 5;
    int arr[n];
    
    arr[0] = 10;
    
    if (arr[0] == 10) {
        return 42;  // VLA cleaned up here
    }
    
    return 0;  // VLA would be cleaned up here too
}

// [from test_vla_expr]
// Test: VLA with expression for size
// Expected return: 84

// [from test_vla_nested]
// Test: VLA in nested scope
// Expected return: 30

// [from test_vla_param]
// Test: VLA array parameters decay to pointer (C99 §6.7.6.3p7)
// Ticket: #413 — void f(int n, int a[n]) previously errored "undefined variable 'n'"
// Expected return: 42

static int sum(int n, int a[n]) {
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i];
    return s;
}

// Forward declaration exercises the is_function lookahead path
static int dot(int n, int a[n], int b[n]);

static int dot(int n, int a[n], int b[n]) {
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}

// Qualifiers on bracketed dimension transfer to the decayed pointer

static int sum_const(int n, const int a[n]) {
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i];
    return s;
}

// [from test_vla_ultra_minimal]
// Test: Ultra minimal - just create VLA, don't use it
// Expected return: 1

#pragma cccc suite begin "vla"

// test_vla_basic
[[cccc::test(return = 42)]]
int test_vla_basic(void) {
    int n = 3;
    int arr[n];
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 12;
    if (arr[0] + arr[1] + arr[2] != 42) return 1;
    return 42;
}

// test_vla_cleanup
[[cccc::test(return = 42)]]
int test_vla_cleanup(void) {
    int r1 = test_vla_in_function();
    if (r1 != 42) return 1;
    
    int r2 = test_multiple_vlas();
    if (r2 != 42) return 2;
    
    int r3 = test_vla_in_nested_scopes();
    if (r3 != 42) return 3;
    
    return 42;  // Success
}

// test_vla_comprehensive
[[cccc::test(return = 42)]]
int test_vla_comprehensive(void) {
    int n = 5;
    int arr[n];
    
    // Initialize array
    for (int i = 0; i < n; i = i + 1) {
        arr[i] = (i + 1) * 10;  // 10, 20, 30, 40, 50
    }
    
    // Test reading values
    int test1 = arr[0];  // 10
    int test2 = arr[4];  // 50
    
    // Test pointer arithmetic
    int *ptr = arr;
    int test3 = ptr[2];  // 30
    
    // Test passing VLA to function
    int total = sum_array(arr, n);  // 10+20+30+40+50 = 150
    
    // Verify results
    if (test1 != 10) return 1;
    if (test2 != 50) return 2;
    if (test3 != 30) return 3;
    if (total != 150) return 4;

    return 42;  // Success
}

// test_vla_enhancements
[[cccc::test(return = 42)]]
int test_vla_enhancements(void) {
    int r1 = _vla_enhancements_test_vla_cleanup();
    if (r1 != 42) return 1;
    
    int r2 = test_multiple_returns();
    if (r2 != 42) return 2;
    
    return 42;  // Success
}

// test_vla_expr
[[cccc::test(return = 42)]]
int test_vla_expr(void) {
    int x = 3;
    int y = 4;
    int arr[x + y];  // 7 elements
    
    for (int i = 0; i < 7; i = i + 1) {
        arr[i] = i * 10;
    }
    
    int sum = 0;
    for (int i = 0; i < 7; i = i + 1) {
        sum = sum + arr[i];
    }

    // sum = 0+10+20+30+40+50+60 = 210
    int result = sum / 2 - 21;  // 210/2 - 21 = 105 - 21 = 84
    if (result != 84) return 1;  // Assert result == 84
    return 42;
}

// test_vla_nested
[[cccc::test(return = 42)]]
int test_vla_nested(void) {
    int result = 0;
    
    {
        int n = 3;
        int arr1[n];
        arr1[0] = 10;
        arr1[1] = 20;
        arr1[2] = 30;
        result = arr1[2];
    }
    
    {
        int m = 2;
        int arr2[m];
        arr2[0] = 100;
        arr2[1] = 200;
        // result should still be 30 from above
    }

    if (result != 30) return 1;  // Assert result == 30
    return 42;
}

// test_vla_param
[[cccc::test(return = 42)]]
int test_vla_param(void) {
    int a[4] = {1, 2, 3, 4};
    int b[4] = {4, 3, 2, 1};

    if (sum(4, a) != 10) return 1;
    if (dot(4, a, b) != 20) return 2;
    if (sum_const(4, a) != 10) return 3;

    return 42;
}

// test_vla_ultra_minimal
[[cccc::test(return = 42)]]
int test_vla_ultra_minimal(void) {
    int n = 1;
    int arr[n];
    if (n != 1) return 0;  // Assert n == 1
    return 42;
}

// [from test_vla_simple]
[[cccc::test(return = 42)]]
int test_vla_simple(void) {
    int n = 5;
    int arr[n];
    arr[0] = 10; arr[1] = 20; arr[2] = 30; arr[3] = 40; arr[4] = 50;
    int sum = arr[0] + arr[1] + arr[2] + arr[3] + arr[4];
    return sum - 108; // 150 - 108 = 42
}

// [from test_vla_minimal]
[[cccc::test(return = 42)]]
int test_vla_minimal(void) {
    int n = 1;
    int arr[n];
    arr[0] = 42;
    return arr[0];
}

// [from test_vla_init]
// VLA with brace initializer.
[[cccc::test(return = 42)]]
int test_vla_init(void) {
    int n = 5;
    int arr[n] = {10, 20, 30, 40, 50};
    int sum = arr[0] + arr[1] + arr[2] + arr[3] + arr[4];
    return sum - 108; // 150 - 108 = 42
}

// [from test_vla_cleanup_simple]
[[cccc::test(return = 42)]]
int test_vla_cleanup_simple(void) {
    int n = 5;
    int arr[n];
    arr[0] = 42;
    return arr[0];
}

// [from test_vla_with_constructor]
// Regression #588: VLA + __attribute__((constructor)) must not SIGSEGV.
[[cccc::test(return = 42)]]
int test_vla_with_constructor(void) {
    __attribute__((constructor)) static void init_thing588(void) {}
    static int vla_sink(char *p, int n) { p[0] = (char)n; return p[0]; }
    int n = 8;
    char buf[n + 4];
    if ((int)sizeof(buf) != 12) return 1;
    return vla_sink(buf, 42);
}

#pragma cccc suite end
