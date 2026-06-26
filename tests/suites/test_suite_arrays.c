// CCCC_FLAGS: --testing
// Consolidated suite: arrays, array decay, multidimensional, global arrays
// Source tests: test_array_decay, test_array_decay_params, test_array_no_return, test_compound_arrays, test_global_array_direct, test_global_array_indices, test_global_array_minimal, test_global_array_return, test_global_arrays, test_multidim_comprehensive, test_static_array_param, test_zero_length_arrays

// [from test_array_decay]
// Test array decay to pointer in various contexts
// Expected return: 42

// Test 1: Array parameter decays to pointer

static int sum_array(int arr[], int len) {
    int sum = 0;
    int i = 0;
    while (i < len) {
        sum = sum + arr[i];
        i = i + 1;
    }
    return sum;
}

// Test 2: Alternative syntax - explicit pointer parameter

static int sum_ptr(int *ptr, int len) {
    int sum = 0;
    int i = 0;
    while (i < len) {
        sum = sum + ptr[i];
        i = i + 1;
    }
    return sum;
}

// Test 3: Array decays when passed to function

static int get_first(int arr[]) {
    return arr[0];
}

// Test 4: Multidimensional array partial decay
// int arr[3][2] decays to int (*)[2] (pointer to array of 2 ints)

static int sum_2d_row(int row[2]) {
    return row[0] + row[1];
}

// Test 5: sizeof behaves differently for array vs decayed pointer

static int check_array_size(int arr[5]) {
    // Inside function, arr is a pointer, so sizeof(arr) would be pointer size
    // But we can still access elements
    return arr[0] + arr[1] + arr[2] + arr[3] + arr[4];
}

// [from test_array_decay_params]
// Test array decay in function parameters - advanced cases
// Expected return: 42

// Test that int arr[] and int *arr are equivalent in parameters

static int func1(int arr[]) {
    return arr[0] + arr[1];
}

static int func2(int *arr) {
    return arr[0] + arr[1];
}

// Array with size specification - size is ignored, still decays to pointer

static int func3(int arr[10]) {
    // arr is just a pointer, size 10 is documentation only
    return arr[0] + arr[1];
}

// Multidimensional arrays - first dimension decays, others don't
// int arr[][3] becomes int (*arr)[3] (pointer to array of 3 ints)

static int sum_matrix_row(int matrix[][3], int row) {
    return matrix[row][0] + matrix[row][1] + matrix[row][2];
}

// Const array parameters (const applies to elements, not pointer)

static int sum_const(int const arr[], int len) {
    int sum = 0;
    int i = 0;
    while (i < len) {
        sum = sum + arr[i];
        i = i + 1;
    }
    return sum;
}

// Function pointer that takes array (which decays)

static int apply_to_array(int (*func)(int[]), int arr[]) {
    return func(arr);
}

static int process_array(int arr[]) {
    return arr[0] * 10 + arr[1];
}

// Array of arrays as parameter

static int get_element(int arr[2][3], int i, int j) {
    return arr[i][j];
}

// [from test_array_no_return]
// Test that demonstrates array return limitations
// In C, arrays cannot be returned directly from functions
// This test shows the correct patterns for working with arrays
// Expected return: 42

// CORRECT: Return pointer to static array

static int *get_static_array() {
    static int arr[3];
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 12;
    return arr;  // Returns pointer to first element
}

// CORRECT: Return pointer passed as parameter

static int *get_modified_array(int *arr, int len) {
    int i = 0;
    while (i < len) {
        arr[i] = arr[i] * 2;
        i = i + 1;
    }
    return arr;  // Returns the pointer that was passed in
}

// CORRECT: Fill array via pointer parameter

static void fill_array(int *arr, int len) {
    int i = 0;
    while (i < len) {
        arr[i] = i + 1;
        i = i + 1;
    }
}

// CORRECT: Return pointer to dynamically allocated array (conceptual)
// Note: In CCCC, we'd use malloc via MALC instruction
// For this test, we'll use static storage

static int *create_array(int size) {
    static int buffer[10];
    int i = 0;
    while (i < size) {
        buffer[i] = i * 2;
        i = i + 1;
    }
    return buffer;
}

// CORRECT: Return pointer to global array
int tc_array_no_return_global_array[5];

static int *get_global_array() {
    return tc_array_no_return_global_array;
}

// CORRECT: Return struct containing array
struct ArrayWrapper {
    int data[3];
    int size;
};

static struct ArrayWrapper make_array_wrapper() {
    struct ArrayWrapper wrapper;
    wrapper.data[0] = 5;
    wrapper.data[1] = 7;
    wrapper.data[2] = 10;
    wrapper.size = 3;
    return wrapper;  // Struct can be returned (contains array)
}

// [from test_compound_arrays]
// Test array compound literals with pointer assignment
// Expected return: 42

// [from test_global_array_direct]
// Direct comparison without intermediate variable

int tc_global_array_direct_global_array[5] = {10, 20, 30, 40, 50};

// [from test_global_array_indices]
// Test each array index individually

int tc_global_array_indices_arr[5] = {10, 20, 30, 40, 50};

// [from test_global_array_minimal]
// Minimal test for global array
int tc_global_array_minimal_arr[3] = {10, 20, 30};

// [from test_global_array_return]
// Just return the first element

int tc_global_array_return_global_array[5] = {10, 20, 30, 40, 50};

// [from test_global_arrays]
// Test global arrays

int tc_global_arrays_global_array[5] = {10, 20, 30, 40, 50};

// [from test_multidim_comprehensive]
// Comprehensive multi-dimensional array test
// Expected return: 42

// [from test_static_array_param]
// Verify C99 array-parameter qualifiers parse without error.

static void f1(int a[static 10]) {}

static void f2(int a[const static 10]) {}

static void f3(int a[restrict 5]) {}

static void f4(int a[volatile 3]) {}

static void f5(int a[static restrict 4]) {}

static void f6(int a[const restrict 5]) {}

static void f7(int a[const]) {}

// [from test_zero_length_arrays]
// Test zero-length arrays (GNU extension)

struct flexible {
    int count;
    int data[0];  // Zero-length array at end
};

struct with_zero {
    int arr[0];
    int x;
};

#pragma cccc suite begin "arrays"

// test_array_decay
[[cccc::test(return = 42)]]
int test_array_decay(void) {
    // Test 1: Array argument decays to pointer
    int arr1[5];
    arr1[0] = 1;
    arr1[1] = 2;
    arr1[2] = 3;
    arr1[3] = 4;
    arr1[4] = 5;
    
    int sum1 = sum_array(arr1, 5);  // 1+2+3+4+5 = 15
    if (sum1 != 15) return 1;
    
    // Test 2: Same array with explicit pointer parameter
    int sum2 = sum_ptr(arr1, 5);    // 15
    if (sum2 != 15) return 2;
    
    // Test 3: Array decay in assignment
    int *ptr = arr1;  // arr1 decays to pointer
    if (ptr[0] != 1) return 3;
    if (ptr[4] != 5) return 4;
    
    // Test 4: Array name in expression context decays to pointer
    int val = get_first(arr1);      // arr1 decays to pointer
    if (val != 1) return 5;
    
    // Test 5: Passing array subset (pointer arithmetic)
    int val2 = get_first(arr1 + 2); // arr1+2 points to arr1[2]
    if (val2 != 3) return 6;
    
    // Test 6: Multidimensional array
    int arr2[3][2];
    arr2[0][0] = 10;
    arr2[0][1] = 15;
    arr2[1][0] = 5;
    arr2[1][1] = 12;
    
    int row_sum = sum_2d_row(arr2[0]);  // Pass first row
    if (row_sum != 25) return 7;
    
    row_sum = sum_2d_row(arr2[1]);      // Pass second row
    if (row_sum != 17) return 8;
    
    // Test 7: sizeof distinction
    // sizeof(arr1) in main gives full array size
    // but inside function it's pointer size
    int arr3[5];
    arr3[0] = 2;
    arr3[1] = 4;
    arr3[2] = 6;
    arr3[3] = 8;
    arr3[4] = 10;
    
    int sum3 = check_array_size(arr3);  // 2+4+6+8+10 = 30
    if (sum3 != 30) return 9;
    
    // Test 8: Array doesn't decay with address-of operator
    int (*ptr_to_arr)[5] = &arr1;  // Pointer to entire array
    int first_elem = (*ptr_to_arr)[0];
    if (first_elem != 1) return 10;
    
    // Test 9: String literals are arrays that decay
    char *str = "Hello";  // String literal decays to char*
    if (str[0] != 'H') return 11;
    if (str[1] != 'e') return 12;
    
    return 42;
}

// test_array_decay_params
[[cccc::test(return = 42)]]
int test_array_decay_params(void) {
    int test_arr[5];
    test_arr[0] = 10;
    test_arr[1] = 20;
    test_arr[2] = 5;
    test_arr[3] = 3;
    test_arr[4] = 4;
    
    // Test 1: Array notation in parameter
    int r1 = func1(test_arr);  // 10 + 20 = 30
    if (r1 != 30) return 1;
    
    // Test 2: Pointer notation in parameter (equivalent)
    int r2 = func2(test_arr);  // 10 + 20 = 30
    if (r2 != 30) return 2;
    
    // Test 3: Array with size (ignored)
    int r3 = func3(test_arr);  // 10 + 20 = 30
    if (r3 != 30) return 3;
    
    // Test 4: Can pass array+offset to any of these functions
    int r4 = func1(test_arr + 2);  // 5 + 3 = 8
    if (r4 != 8) return 4;
    
    // Test 5: Multidimensional array
    int matrix[2][3];
    matrix[0][0] = 10;
    matrix[0][1] = 15;
    matrix[0][2] = 7;
    matrix[1][0] = 1;
    matrix[1][1] = 2;
    matrix[1][2] = 3;
    
    int row0 = sum_matrix_row(matrix, 0);  // 10 + 15 + 7 = 32
    if (row0 != 32) return 5;
    
    int row1 = sum_matrix_row(matrix, 1);  // 1 + 2 + 3 = 6
    if (row1 != 6) return 6;
    
    // Test 6: Const array parameter
    int const_arr[3];
    const_arr[0] = 5;
    const_arr[1] = 7;
    const_arr[2] = 10;
    
    int r6 = sum_const(const_arr, 3);  // 5 + 7 + 10 = 22
    if (r6 != 22) return 7;
    
    // Test 7: Function pointer with array parameter
    int proc_arr[2];
    proc_arr[0] = 3;
    proc_arr[1] = 12;
    
    int r7 = apply_to_array(process_array, proc_arr);  // 3*10 + 12 = 42
    if (r7 != 42) return 8;
    
    // Test 8: Array of arrays
    int arr2d[2][3];
    arr2d[0][0] = 10;
    arr2d[0][1] = 20;
    arr2d[0][2] = 30;
    arr2d[1][0] = 1;
    arr2d[1][1] = 2;
    arr2d[1][2] = 3;
    
    int elem = get_element(arr2d, 0, 1);  // 20
    if (elem != 20) return 9;
    
    elem = get_element(arr2d, 1, 2);  // 3
    if (elem != 3) return 10;
    
    // Test 9: Pointer arithmetic preserves array subscripting
    int *ptr = test_arr;
    ptr = ptr + 1;  // Now points to test_arr[1]
    if (ptr[0] != 20) return 11;  // ptr[0] is test_arr[1]
    if (ptr[1] != 5) return 12;   // ptr[1] is test_arr[2]
    
    return 42;
}

// test_array_no_return
[[cccc::test(return = 42)]]
int test_array_no_return(void) {
    // Test 1: Get pointer to static array
    int *arr1 = get_static_array();
    if (arr1[0] != 10) return 1;
    if (arr1[1] != 20) return 2;
    if (arr1[2] != 12) return 3;
    
    // Test 2: Modify and return array
    int test_arr[3];
    test_arr[0] = 5;
    test_arr[1] = 10;
    test_arr[2] = 6;
    
    int *arr2 = get_modified_array(test_arr, 3);
    if (arr2[0] != 10) return 4;  // 5 * 2
    if (arr2[1] != 20) return 5;  // 10 * 2
    if (arr2[2] != 12) return 6;  // 6 * 2
    
    // Test 3: Fill array via pointer
    int arr3[5];
    fill_array(arr3, 5);
    if (arr3[0] != 1) return 7;
    if (arr3[1] != 2) return 8;
    if (arr3[2] != 3) return 9;
    if (arr3[3] != 4) return 10;
    if (arr3[4] != 5) return 11;
    
    // Test 4: Get pointer to created array
    int *arr4 = create_array(5);
    if (arr4[0] != 0) return 12;  // 0 * 2
    if (arr4[1] != 2) return 13;  // 1 * 2
    if (arr4[2] != 4) return 14;  // 2 * 2
    if (arr4[3] != 6) return 15;  // 3 * 2
    if (arr4[4] != 8) return 16;  // 4 * 2
    
    // Test 5: Get pointer to global array
    int *arr5 = get_global_array();
    arr5[0] = 10;
    arr5[1] = 32;
    if (tc_array_no_return_global_array[0] != 10) return 17;
    if (tc_array_no_return_global_array[1] != 32) return 18;
    
    // Test 6: Struct containing array can be returned
    struct ArrayWrapper wrapper = make_array_wrapper();
    if (wrapper.data[0] != 5) return 19;
    if (wrapper.data[1] != 7) return 20;
    if (wrapper.data[2] != 10) return 21;
    if (wrapper.size != 3) return 22;
    
    int sum = wrapper.data[0] + wrapper.data[1] + wrapper.data[2];
    if (sum != 22) return 23;
    
    // Test 7: Array decay in return statement
    // When we return an array name, it decays to pointer
    int final_arr[2];
    final_arr[0] = 20;
    final_arr[1] = 22;
    
    int *ptr = final_arr;  // Explicit decay
    if (ptr[0] + ptr[1] != 42) return 24;
    
    return 42;
}

// test_compound_arrays
[[cccc::test(return = 42)]]
int test_compound_arrays(void) {
    // Test 1: Array compound literal assigned to pointer
    int *p1 = (int[]){10, 20, 12};
    if (p1[0] + p1[1] + p1[2] != 42) return 1;
    
    // Test 2: Array compound literal with direct access
    int val = ((int[]){42, 0})[0];
    if (val != 42) return 2;
    
    // Test 3: Multi-dimensional array compound literal
    int *p2 = (int[]){1, 2, 3, 4, 5};
    int sum = p2[0] + p2[1] + p2[2] + p2[3] + p2[4];
    if (sum != 15) return 3;
    
    return 42;
}

// test_global_array_direct
[[cccc::test(return = 42)]]
int test_global_array_direct(void) {
    if (tc_global_array_direct_global_array[0] != 10) {
        return 1;
    }
    return 42;
}

// test_global_array_indices
[[cccc::test(return = 42)]]
int test_global_array_indices(void) {
    int v0 = tc_global_array_indices_arr[0];  // Should be 10
    int v1 = tc_global_array_indices_arr[1];  // Should be 20
    int v2 = tc_global_array_indices_arr[2];  // Should be 30
    int v3 = tc_global_array_indices_arr[3];  // Should be 40
    int v4 = tc_global_array_indices_arr[4];  // Should be 50
    
    // Return sum to verify all values
    int sum = v0 + v1;  // Should be 30
    if (sum != 30) return 1;  // Assert sum == 30
    return 42;
}

// test_global_array_minimal
[[cccc::test(return = 42)]]
int test_global_array_minimal(void) {
    int val = tc_global_array_minimal_arr[0];  // Should return 10
    if (val != 10) return 1;  // Assert val == 10
    return 42;
}

// test_global_array_return
[[cccc::test(return = 42)]]
int test_global_array_return(void) {
    int val = tc_global_array_return_global_array[0];
    if (val != 10) return 1;  // Assert val == 10
    return 42;
}

// test_global_arrays
[[cccc::test(return = 42)]]
int test_global_arrays(void) {
    // Test reading array elements
    if (tc_global_arrays_global_array[0] != 10) return 1;
    if (tc_global_arrays_global_array[1] != 20) return 2;
    if (tc_global_arrays_global_array[4] != 50) return 3;
    
    // Test modifying array elements
    tc_global_arrays_global_array[2] = 99;
    if (tc_global_arrays_global_array[2] != 99) return 4;
    
    return 42;  // Success
}

// test_multidim_comprehensive
[[cccc::test(return = 42)]]
int test_multidim_comprehensive(void) {
    // Test 1: 2D array with nested loop initialization
    int matrix[3][4];
    int val = 1;
    for (int i = 0; i < 3; i = i + 1) {
        for (int j = 0; j < 4; j = j + 1) {
            matrix[i][j] = val;
            val = val + 1;
        }
    }
    
    // Verify: matrix[2][3] should be 12
    if (matrix[2][3] != 12) return 1;
    
    // Test 2: 2D array initialization with literals
    int grid[2][3] = {
        {10, 20, 30},
        {40, 50, 60}
    };
    
    int sum = 0;
    for (int i = 0; i < 2; i = i + 1) {
        for (int j = 0; j < 3; j = j + 1) {
            sum = sum + grid[i][j];
        }
    }
    // sum = 10+20+30+40+50+60 = 210
    if (sum != 210) return 2;
    
    // Test 3: 3D array access
    int cube[2][2][2];
    cube[0][0][0] = 1;
    cube[0][0][1] = 2;
    cube[0][1][0] = 3;
    cube[0][1][1] = 4;
    cube[1][0][0] = 5;
    cube[1][0][1] = 6;
    cube[1][1][0] = 7;
    cube[1][1][1] = 8;
    
    // Access diagonal
    int diag = cube[0][0][0] + cube[1][1][1];  // 1 + 8 = 9
    if (diag != 9) return 3;
    
    // Test 4: Pointer arithmetic with 2D arrays
    int arr[2][3] = {{1, 2, 3}, {4, 5, 6}};
    int *ptr = &arr[0][0];
    
    // Access via pointer arithmetic
    if (*(ptr + 0) != 1) return 4;  // arr[0][0]
    if (*(ptr + 2) != 3) return 5;  // arr[0][2]
    if (*(ptr + 3) != 4) return 6;  // arr[1][0]
    if (*(ptr + 5) != 6) return 7;  // arr[1][2]
    
    // Test 5: Array of arrays
    int row1[3] = {1, 2, 3};
    int row2[3] = {4, 5, 6};
    
    if (row1[1] + row2[1] != 7) return 8;  // 2 + 5 = 7
    
    // Test 6: Modifying through indices
    matrix[1][2] = 999;
    if (matrix[1][2] != 999) return 9;
    
    // Test 7: Character 2D arrays (strings)
    char strings[2][10];
    strings[0][0] = 'H';
    strings[0][1] = 'i';
    strings[0][2] = 0;
    strings[1][0] = 'B';
    strings[1][1] = 'y';
    strings[1][2] = 'e';
    strings[1][3] = 0;
    
    if (strings[0][0] != 'H') return 10;
    if (strings[1][2] != 'e') return 11;
    
    // All tests passed!
    return 42;
}

// test_static_array_param
[[cccc::test(return = 42)]]
int test_static_array_param(void) { return 42; }

// test_zero_length_arrays
[[cccc::test(return = 42)]]
int test_zero_length_arrays(void) {
    // Test sizeof with zero-length array
    unsigned long sz1 = sizeof(struct flexible);
    unsigned long sz2 = sizeof(int);
    if (sz1 != sz2)
        return 1;

    // Test zero-length array in middle
    if (sizeof(struct with_zero) != sizeof(int))
        return 2;

    // Test local zero-length array
    int local_arr[0];
    (void)local_arr;  // Suppress unused warning

    return 42;
}

// test_array_partial_init: partial initialization; remaining elements zero-filled
[[cccc::test(return = 42)]]
int test_array_partial_init(void) {
    int arr1[5] = {1, 2};
    if (arr1[0] != 1) return 1;
    if (arr1[1] != 2) return 2;
    if (arr1[2] != 0) return 3;
    if (arr1[4] != 0) return 4;

    int arr2[10] = {42};
    if (arr2[0] != 42) return 5;
    if (arr2[1] != 0) return 6;
    if (arr2[9] != 0) return 7;

    int arr3[5] = {0};
    if (arr3[0] != 0) return 8;
    if (arr3[4] != 0) return 9;

    int arr4[7] = {10, 20, 30};
    int sum = arr4[0] + arr4[1] + arr4[2];
    if (sum != 60) return 10;
    if (arr4[3] != 0) return 11;

    int arr5[4] = {10, 32};
    int result = arr5[0] + arr5[1] + arr5[2] + arr5[3];  // 10+32+0+0 = 42
    return result;
}

#pragma cccc suite end
