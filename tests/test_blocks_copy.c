/*
 * Test: Block_copy and Block_release (#424)
 *
 * Block_copy heap-duplicates a block descriptor so the block can outlive the
 * stack frame it was created in; Block_release frees that heap copy.  A copied
 * block must NOT be used after it is released (that is use-after-free), so each
 * case here releases only once it is done calling the copy.
 */

#include <stdlib.h>

typedef int (^IntBlock)(void);

/* Returns a heap block that escapes this frame -- the core #424 scenario. */
IntBlock make_adder(int base) {
    IntBlock b = ^{ return base + 1; };
    return Block_copy(b);
}

int main() {
    // Test 1: Block_copy returns a working block
    int (^simple)(void) = ^{ return 42; };
    int (^copy)(void) = Block_copy(simple);
    if (copy() != 42) return 1;
    Block_release(copy);

    // Test 2: Block_copy snapshots captured variables
    int x = 100;
    int (^capture)(void) = ^{ return x; };
    int (^capture_copy)(void) = Block_copy(capture);
    x = 999; // copy holds the value captured at creation time
    if (capture_copy() != 100) return 2;
    Block_release(capture_copy);

    // Test 3: Block_copy with __block variables shares the heap cell
    __block int counter = 0;
    void (^inc)(void) = ^{ counter++; };
    void (^inc_copy)(void) = Block_copy(inc);
    inc_copy();
    inc_copy();
    if (counter != 2) return 3;
    Block_release(inc_copy);

    // Test 4: Block_copy with parameters
    int (^add)(int, int) = ^(int a, int b) { return a + b; };
    int (^add_copy)(int, int) = Block_copy(add);
    if (add_copy(5, 7) != 12) return 4;
    Block_release(add_copy);

    // Test 5: escaped blocks -- each Block_copy is an independent heap copy
    IntBlock a = make_adder(41);
    IntBlock b = make_adder(99);
    int ra = a();
    int rb = b();
    if (ra != 42) return 5;
    if (rb != 100) return 6;
    Block_release(a);
    Block_release(b);

    return 42;
}
