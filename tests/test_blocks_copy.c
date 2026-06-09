/*
 * Test: Block_copy and Block_release functions
 * Tests block lifecycle management (simplified for CCCC VM)
 */

int main() {
    // Test 1: Block_copy returns the block
    int (^simple)(void) = ^{ return 42; };
    int (^copy)(void) = Block_copy(simple);
    if (copy() != 42) return 1;
    
    // Test 2: Block_release is a no-op (doesn't crash)
    Block_release(copy);
    // Block still works after release (VM manages lifetime)
    if (copy() != 42) return 2;
    
    // Test 3: Block_copy with captured variables
    int x = 100;
    int (^capture)(void) = ^{ return x; };
    int (^capture_copy)(void) = Block_copy(capture);
    if (capture_copy() != 100) return 3;
    Block_release(capture_copy);
    
    // Test 4: Block_copy with __block variables
    __block int counter = 0;
    void (^inc)(void) = ^{ counter++; };
    void (^inc_copy)(void) = Block_copy(inc);
    inc_copy();
    inc_copy();
    if (counter != 2) return 4;
    Block_release(inc_copy);
    
    // Test 5: Block_copy with parameters
    int (^add)(int, int) = ^(int a, int b) { return a + b; };
    int (^add_copy)(int, int) = Block_copy(add);
    if (add_copy(5, 7) != 12) return 5;
    Block_release(add_copy);
    
    return 42;
}
