// Test that demonstrates array return limitations
// In C, arrays cannot be returned directly from functions
// This test shows the correct patterns for working with arrays
// Expected return: 42

// CORRECT: Return pointer to static array
int *get_static_array() {
    static int arr[3];
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 12;
    return arr;  // Returns pointer to first element
}

// CORRECT: Return pointer passed as parameter
int *get_modified_array(int *arr, int len) {
    int i = 0;
    while (i < len) {
        arr[i] = arr[i] * 2;
        i = i + 1;
    }
    return arr;  // Returns the pointer that was passed in
}

// CORRECT: Fill array via pointer parameter
void fill_array(int *arr, int len) {
    int i = 0;
    while (i < len) {
        arr[i] = i + 1;
        i = i + 1;
    }
}

// CORRECT: Return pointer to dynamically allocated array (conceptual)
// Note: In CCCC, we'd use malloc via MALC instruction
// For this test, we'll use static storage
int *create_array(int size) {
    static int buffer[10];
    int i = 0;
    while (i < size) {
        buffer[i] = i * 2;
        i = i + 1;
    }
    return buffer;
}

// CORRECT: Return pointer to global array
int global_array[5];

int *get_global_array() {
    return global_array;
}

// CORRECT: Return struct containing array
struct ArrayWrapper {
    int data[3];
    int size;
};

struct ArrayWrapper make_array_wrapper() {
    struct ArrayWrapper wrapper;
    wrapper.data[0] = 5;
    wrapper.data[1] = 7;
    wrapper.data[2] = 10;
    wrapper.size = 3;
    return wrapper;  // Struct can be returned (contains array)
}

int main() {
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
    if (global_array[0] != 10) return 17;
    if (global_array[1] != 32) return 18;
    
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
