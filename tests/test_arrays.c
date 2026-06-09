// Test array operations in CCCC
// Expected return: 42

int main() {
    // Test 1: Simple array declaration and access
    int arr[5];
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 12;
    
    // Test 2: Array indexing with variables
    int idx = 2;
    int val = arr[idx];  // Should be 12
    
    // Test 3: Array arithmetic
    int result = arr[0] + arr[1] + val;  // 10 + 20 + 12 = 42
    
    return result;
}
