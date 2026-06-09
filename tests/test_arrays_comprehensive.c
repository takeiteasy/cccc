// Comprehensive array test for CCCC
// Expected return: 42

int main() {
    // Test 1: Array declaration and basic indexing
    int arr[5];
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 12;
    arr[3] = 5;
    arr[4] = 7;
    
    // Test 2: Reading from array with constant index
    int a = arr[0];  // Should be 10
    int b = arr[1];  // Should be 20
    
    // Test 3: Array indexing with variable
    int idx = 2;
    int c = arr[idx];  // Should be 12
    
    // Test 4: Array element in expression
    int sum = arr[0] + arr[1];  // 10 + 20 = 30
    
    // Test 5: Modifying array element
    arr[3] = arr[3] + 2;  // 5 + 2 = 7
    int d = arr[3];  // Should be 7
    
    // Test 6: Using array in more complex expression
    idx = 4;
    int e = arr[idx] * 2;  // 7 * 2 = 14
    
    // Test 7: Multiple arrays
    int arr2[3];
    arr2[0] = 1;
    arr2[1] = 2;
    arr2[2] = 3;
    
    int f = arr2[0] + arr2[1] + arr2[2];  // 1 + 2 + 3 = 6
    
    // Final result: a + c + d + f - e
    // = 10 + 12 + 7 + 6 - 14 + 21 = 42
    int result = a + c + d + f - e + 21;
    
    return result;
}
