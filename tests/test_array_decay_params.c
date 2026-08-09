// Test array decay in function parameters - advanced cases
// Expected return: 42

// Test that int arr[] and int *arr are equivalent in parameters
int func1(int arr[]) {
    return arr[0] + arr[1];
}

int func2(int *arr) {
    return arr[0] + arr[1];
}

// Array with size specification - size is ignored, still decays to pointer
int func3(int arr[10]) {
    // arr is just a pointer, size 10 is documentation only
    return arr[0] + arr[1];
}

// Multidimensional arrays - first dimension decays, others don't
// int arr[][3] becomes int (*arr)[3] (pointer to array of 3 ints)
int sum_matrix_row(int matrix[][3], int row) {
    return matrix[row][0] + matrix[row][1] + matrix[row][2];
}

// Const array parameters (const applies to elements, not pointer)
int sum_const(int const arr[], int len) {
    int sum = 0;
    int i = 0;
    while (i < len) {
        sum = sum + arr[i];
        i = i + 1;
    }
    return sum;
}

// Function pointer that takes array (which decays)
int apply_to_array(int (*func)(int[]), int arr[]) {
    return func(arr);
}

int process_array(int arr[]) {
    return arr[0] * 10 + arr[1];
}

// Array of arrays as parameter
int get_element(int arr[2][3], int i, int j) {
    return arr[i][j];
}

int main() {
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
