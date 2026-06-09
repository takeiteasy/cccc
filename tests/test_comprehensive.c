// Comprehensive arithmetic and operator test for CCCC
int main() {
    int passed = 0;
    int failed = 0;
    
    // Test 1: Basic arithmetic
    if ((5 + 3) == 8) passed = passed + 1; else failed = failed + 1;
    if ((10 - 4) == 6) passed = passed + 1; else failed = failed + 1;
    if ((7 * 2) == 14) passed = passed + 1; else failed = failed + 1;
    if ((20 / 4) == 5) passed = passed + 1; else failed = failed + 1;
    if ((17 % 5) == 2) passed = passed + 1; else failed = failed + 1;
    
    // Test 2: Comparisons
    if (10 == 10) passed = passed + 1; else failed = failed + 1;
    if (10 != 5) passed = passed + 1; else failed = failed + 1;
    if (5 < 10) passed = passed + 1; else failed = failed + 1;
    if (10 <= 10) passed = passed + 1; else failed = failed + 1;
    if (10 > 5) passed = passed + 1; else failed = failed + 1;
    
    // Test 3: Bitwise operations
    if ((12 | 10) == 14) passed = passed + 1; else failed = failed + 1;
    if ((12 ^ 10) == 6) passed = passed + 1; else failed = failed + 1;
    if ((12 & 10) == 8) passed = passed + 1; else failed = failed + 1;
    if ((1 << 3) == 8) passed = passed + 1; else failed = failed + 1;
    if ((16 >> 2) == 4) passed = passed + 1; else failed = failed + 1;
    
    // Test 4: Unary operators
    if ((-5) == -5) passed = passed + 1; else failed = failed + 1;
    if (!0) passed = passed + 1; else failed = failed + 1;
    if (!(!1)) passed = passed + 1; else failed = failed + 1;
    if ((~0) == -1) passed = passed + 1; else failed = failed + 1;
    
    // Test 5: Logical operators
    if (1 && 1) passed = passed + 1; else failed = failed + 1;
    if (!(0 && 1)) passed = passed + 1; else failed = failed + 1;
    if (1 || 0) passed = passed + 1; else failed = failed + 1;
    if (!(0 || 0)) passed = passed + 1; else failed = failed + 1;
    
    // Test 6: Control flow
    int sum = 0;
    int i;
    for (i = 1; i <= 10; i = i + 1) {
        sum = sum + i;
    }
    if (sum == 55) passed = passed + 1; else failed = failed + 1;
    
    // Test 7: Pointers
    int val = 99;
    int *ptr = &val;
    if (*ptr == 99) passed = passed + 1; else failed = failed + 1;
    *ptr = 88;
    if (val == 88) passed = passed + 1; else failed = failed + 1;
    
    // Return passed tests (should be 26 if all work)
    if (passed != 26) return 1;  // Assert all 26 tests passed
    return 42;
}
