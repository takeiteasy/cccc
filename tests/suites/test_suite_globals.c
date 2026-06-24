// CCCC_FLAGS: --testing
// Consolidated suite: globals, static storage, extern declarations
// Source tests: test_extern_comprehensive1, test_extern_file2, test_extern_simple1, test_extern_works, test_global_pointer_init, test_globals, test_globals_comprehensive, test_globals_simple, test_static, test_static_comprehensive

// [from test_extern_comprehensive1]
// Comprehensive extern test (merged with test_extern_comprehensive2.c)
// This file demonstrates all extern use cases

// Definitions
int global_counter = 0;
int array[5] = {1, 2, 3, 4, 5};

static void increment_counter() {
    global_counter = global_counter + 1;
}

static int get_array_sum() {
    int sum = 0;
    for (int i = 0; i < 5; i = i + 1) {
        sum = sum + array[i];
    }
    return sum;
}

// [from test_extern_file2]
// Merged extern test (combined file1 and file2)

// Define the shared variable
int shared_var = 42;

// Define the helper function

static int helper_func(int x) {
    return x + shared_var;
}

static int use_shared() {
    return shared_var * 2;
}

static int call_helper(int val) {
    return helper_func(val);
}

// [from test_extern_simple1]
// Super simple test - minimal extern (merged with test_extern_simple2.c)
int tc_extern_simple1_x = 42;

// [from test_extern_works]
// Comprehensive test demonstrating extern keyword functionality
// Merged into single file

// Define tc_extern_works_x
int tc_extern_works_x = 42;

// [from test_global_pointer_init]
int global_value = 40;
int *global_ptr = &global_value;

static int read_global(void) {
    return *global_ptr + 1;
}

int (*global_fn_ptr)(void) = read_global;

// [from test_globals]
// Test global variables

int tc_globals_global_var = 42;
int uninitialized_global;

static int get_global() {
    return tc_globals_global_var;
}

static int set_global(int val) {
    tc_globals_global_var = val;
    return tc_globals_global_var;
}

static int test_uninitialized() {
    uninitialized_global = 100;
    return uninitialized_global;
}

// [from test_globals_comprehensive]
// Comprehensive test for global variables

// Initialized globals
int global_int = 100;
char global_char = 'X';

// Uninitialized globals
int tc_globals_comprehensive_uninit_global;
char uninit_char;

// Array globals
int global_array[5] = {1, 2, 3, 4, 5};

// Test reading initialized global

static int read_global_int() {
    return global_int;
}

// Test writing to global

static void write_global_int(int val) {
    global_int = val;
}

// Test reading and writing char global

static char get_and_set_char(char new_val) {
    char old = global_char;
    global_char = new_val;
    return old;
}

// Test uninitialized global (should be zero-initialized)

static int test_uninit() {
    // Uninitialized globals should be zero
    if (tc_globals_comprehensive_uninit_global != 0) return 1;
    if (uninit_char != 0) return 2;
    
    // Write to them
    tc_globals_comprehensive_uninit_global = 50;
    uninit_char = 'Y';
    
    // Read back
    if (tc_globals_comprehensive_uninit_global != 50) return 3;
    if (uninit_char != 'Y') return 4;
    
    return 0;  // Success
}

// Test global array access

static int test_global_array() {
    // Read values
    if (global_array[0] != 1) return 1;
    if (global_array[4] != 5) return 2;
    
    // Modify array
    global_array[2] = 99;
    
    // Read back
    if (global_array[2] != 99) return 3;
    
    return 0;  // Success
}

// Test pointer to global

static int test_global_pointer() {
    int *ptr = &global_int;
    *ptr = 200;
    
    if (global_int != 200) return 1;
    
    return 0;  // Success
}

// [from test_globals_simple]
// Simple test for global variables (without arrays)

int tc_globals_simple_global_var = 42;
int tc_globals_simple_uninit_global;

static int _globals_simple_get_global() {
    return tc_globals_simple_global_var;
}

// [from test_static]
// Test static keyword functionality
// Tests:
// 1. Static local variables (retain value between function calls)
// 2. Static global variables (file scope only - not testable in single file)
// 3. Static functions (file scope only - not testable in single file)

// Test 1: Static local variable

static int counter() {
    static int count = 0;  // Initialized once, persists between calls
    count = count + 1;
    return count;
}

// Test 2: Static local with explicit initialization

static int sum_values(int x) {
    static int total = 100;  // Initialized to 100 on first call
    total = total + x;
    return total;
}

// Test 3: Static local without initialization (should be zero-initialized)

static int zero_init() {
    static int value;  // Should be 0
    value = value + 10;
    return value;
}

// [from test_static_comprehensive]
// Comprehensive test for static keyword functionality
// Tests static local variables, static functions, and static globals

// Static global variable (file scope)
static int static_global = 42;

// Static function (file scope)

static int static_helper(int x) {
    return x * 2;
}

// Test 1: Static local variable with initialization

static int _static_comprehensive_counter() {
    static int count = 0;
    count = count + 1;
    return count;
}

// Test 2: Multiple static locals in same function

static int multi_static() {
    static int a = 10;
    static int b = 20;
    a = a + 1;
    b = b + 2;
    return a + b;  // First call: 11+22=33, Second: 12+24=36, etc.
}

// Test 3: Static local in different functions (separate storage)

static int counter_a() {
    static int count = 0;
    count = count + 1;
    return count;
}

static int counter_b() {
    static int count = 100;
    count = count + 10;
    return count;
}

// Test 4: Static local with complex initialization

static int complex_init() {
    static int value = 5 + 3;  // Should be evaluated once to 8
    value = value + 1;
    return value;
}

// Test 5: Static local without explicit initialization (zero-init)

static int zero_init_test() {
    static int uninitialized;
    uninitialized = uninitialized + 1;
    return uninitialized;
}

// Test 6: Static local with array

static int array_static() {
    static int arr[3];  // Zero-initialized
    arr[0] = arr[0] + 1;
    arr[1] = arr[1] + 2;
    arr[2] = arr[2] + 3;
    return arr[0] + arr[1] + arr[2];  // First: 1+2+3=6, Second: 2+4+6=12
}

// Test 7: Nested function calls with static locals

static int outer() {
    static int x = 0;
    x = x + 1;
    return x + _static_comprehensive_counter();  // _static_comprehensive_counter has its own static
}

#pragma cccc suite begin "globals"

// test_extern_comprehensive1
[[cccc::test(return = 42)]]
int test_extern_comprehensive1(void) {
    // global_counter starts at 0
    increment_counter();
    increment_counter();

    // Should be 2 now
    if (global_counter != 2) return 1;

    return 42;  // Success
}

// test_extern_file2
[[cccc::test(return = 42)]]
int test_extern_file2(void) {
    // shared_var is 42
    int result1 = use_shared();  // Should return 84

    // helper_func(10) = 10 + 42 = 52
    int result2 = call_helper(10);  // Should return 52

    // Verify results
    if (result1 != 84) return 1;  // Failure
    if (result2 != 52) return 2;  // Failure

    return 42;  // Success
}

// test_extern_simple1
[[cccc::test(return = 42)]]
int test_extern_simple1(void) {
    if (tc_extern_simple1_x != 42) return 1;
    return 42;
}

// test_extern_works
[[cccc::test(return = 42)]]
int test_extern_works(void) {
    // Test extern variable
    if (tc_extern_works_x != 42) return 1;

    return 42;  // Success
}

// test_global_pointer_init
[[cccc::test(return = 42)]]
int test_global_pointer_init(void) {
    if (*global_ptr != 40)
        return 1;
    global_value = 41;
    if (global_fn_ptr() != 42)
        return 2;
    return 42;
}

// test_globals
[[cccc::test(return = 42)]]
int test_globals(void) {
    // Test initialized global
    int result = get_global();  // Should return 42
    
    // Test global assignment
    set_global(10);
    result = get_global();  // Should return 10
    
    // Test uninitialized global
    int uninit_result = test_uninitialized();  // Should return 100
    
    // Verify final state
    if (get_global() == 10 && uninit_result == 100) {
        return 42;  // Success
    }
    
    return 1;  // Failure
}

// test_globals_comprehensive
[[cccc::test(return = 42)]]
int test_globals_comprehensive(void) {
    int errors = 0;
    
    // Test 1: Read initialized global
    if (read_global_int() != 100) errors++;
    
    // Test 2: Write and read back
    write_global_int(150);
    if (read_global_int() != 150) errors++;
    
    // Test 3: Char global
    char old_char = get_and_set_char('Z');
    if (old_char != 'X') errors++;
    if (global_char != 'Z') errors++;
    
    // Test 4: Uninitialized globals
    if (test_uninit() != 0) errors++;
    
    // Test 5: Global arrays
    if (test_global_array() != 0) errors++;
    
    // Test 6: Pointers to globals
    if (test_global_pointer() != 0) errors++;
    
    // If no errors, return success code
    if (errors == 0) {
        return 42;
    }
    
    return errors;
}

// test_globals_simple
[[cccc::test(return = 42)]]
int test_globals_simple(void) {
    // Test initialized global
    if (_globals_simple_get_global() != 42) return 1;
    
    // Test uninitialized global (should be 0)
    if (tc_globals_simple_uninit_global != 0) return 2;
    
    // Modify uninitialized global
    tc_globals_simple_uninit_global = 100;
    if (tc_globals_simple_uninit_global != 100) return 3;
    
    // Modify initialized global
    tc_globals_simple_global_var = 10;
    if (tc_globals_simple_global_var != 10) return 4;
    
    return 42;  // Success
}

// test_static
[[cccc::test(return = 42)]]
int test_static(void) {
    int result = 0;
    
    // Test counter - should return 1, 2, 3
    int c1 = counter();  // Should be 1
    int c2 = counter();  // Should be 2
    int c3 = counter();  // Should be 3
    
    if (c1 != 1) return 1;
    if (c2 != 2) return 2;
    if (c3 != 3) return 3;
    
    // Test sum_values - should start at 100
    int s1 = sum_values(5);   // Should be 105
    int s2 = sum_values(10);  // Should be 115
    
    if (s1 != 105) return 4;
    if (s2 != 115) return 5;
    
    // Test zero initialization
    int z1 = zero_init();  // Should be 10 (0 + 10)
    int z2 = zero_init();  // Should be 20 (10 + 10)
    
    if (z1 != 10) return 6;
    if (z2 != 20) return 7;
    
    return 42;  // All tests passed
}

// test_static_comprehensive
[[cccc::test(return = 42)]]
int test_static_comprehensive(void) {
    // Test static global
    if (static_global != 42) return 1;
    static_global = 50;
    if (static_global != 50) return 2;
    
    // Test static function
    int result = static_helper(5);
    if (result != 10) return 3;
    
    // Test basic _static_comprehensive_counter
    if (_static_comprehensive_counter() != 1) return 4;
    if (_static_comprehensive_counter() != 2) return 5;
    if (_static_comprehensive_counter() != 3) return 6;
    
    // Test multiple statics in one function
    int m1 = multi_static();  // 11 + 22 = 33
    int m2 = multi_static();  // 12 + 24 = 36
    if (m1 != 33) return 7;
    if (m2 != 36) return 8;
    
    // Test separate counters
    if (counter_a() != 1) return 9;
    if (counter_b() != 110) return 10;
    if (counter_a() != 2) return 11;
    if (counter_b() != 120) return 12;
    
    // Test complex initialization
    if (complex_init() != 9) return 13;   // 8 + 1
    if (complex_init() != 10) return 14;  // 9 + 1
    
    // Test zero initialization
    if (zero_init_test() != 1) return 15;
    if (zero_init_test() != 2) return 16;
    
    // Test array static
    if (array_static() != 6) return 17;   // 1+2+3
    if (array_static() != 12) return 18;  // 2+4+6
    
    // Test nested calls
    int o1 = outer();  // x=1, _static_comprehensive_counter()=4, total=5
    int o2 = outer();  // x=2, _static_comprehensive_counter()=5, total=7
    if (o1 != 5) return 19;
    if (o2 != 7) return 20;
    
    // Test static local vs regular local
    int i;
    int regular_sum = 0;
    int static_sum = 0;
    for (i = 0; i < 3; i = i + 1) {
        int regular = 0;
        static int static_var = 0;
        regular = regular + 1;
        static_var = static_var + 1;
        regular_sum = regular_sum + regular;
        static_sum = static_sum + static_var;
    }
    // regular always resets to 0: 1+1+1=3
    // static_var persists: 1+2+3=6
    if (regular_sum != 3) return 21;
    if (static_sum != 6) return 22;
    
    return 42;  // All tests passed!
}

#pragma cccc suite end
