// Test compound literals in CCCC
// Expected return: 42

int main() {
    // Test 1: Scalar compound literal
    int x = (int){42};
    if (x != 42) return 1;
    
    // Test 2: Scalar compound literal in expression
    int y = (int){30} + (int){12};
    if (y != 42) return 2;
    
    // Test 3: Multiple scalar compound literals
    int a = (int){10};
    int b = (int){20};
    int c = (int){12};
    if (a + b + c != 42) return 3;
    
    // Test 4: Compound literal assigned multiple times
    int val1 = (int){100};
    val1 = (int){42};
    if (val1 != 42) return 4;
    
    return 42;  // Success
}
