/*
 * Test: Block definition, type syntax, and actual invocation
 * Tests the Apple Blocks extension for CCCC
 */

int main() {
    // Simple block with no parameters - test invocation
    int (^simple)(void) = ^{ return 42; };
    int result1 = simple();
    if (result1 != 42) return 1;
    
    // Block with parameters - test passing args
    int (^add)(int, int) = ^(int a, int b) { return a + b; };
    int result2 = add(10, 20);
    if (result2 != 30) return 2;
    
    // Block with single parameter
    int (^double_it)(int) = ^(int x) { return x * 2; };
    int result3 = double_it(5);
    if (result3 != 10) return 3;
    
    // Inline block invocation
    int result4 = (^(int x) { return x * x; })(7);
    if (result4 != 49) return 4;
    
    return 42;
}
