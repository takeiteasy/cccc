// CCCC_FLAGS: --testing
// Consolidated suite: Apple Blocks extension
// Source tests: test_blocks_basic, test_blocks_capture, test_blocks_copy, test_blocks_mutable, test_blocks_nested, test_blocks_release_no_stdlib

#include <stdlib.h>

// [from test_blocks_basic]
/*
 * Test: Block definition, type syntax, and actual invocation
 * Tests the Apple Blocks extension for CCCC
 */

// [from test_blocks_capture]
/*
 * Test: Block variable capture (capture-by-copy)
 * Tests that blocks capture outer variable values at creation time
 */

// [from test_blocks_copy]
/*
 * Test: Block_copy and Block_release (#424)
 *
 * Block_copy heap-duplicates a block descriptor so the block can outlive the
 * stack frame it was created in; Block_release frees that heap copy.  A copied
 * block must NOT be used after it is released (that is use-after-free), so each
 * case here releases only once it is done calling the copy.
 */

typedef int (^IntBlock)(void);

/* Returns a heap block that escapes this frame -- the core #424 scenario. */

static IntBlock make_adder(int base) {
    IntBlock b = ^{ return base + 1; };
    return Block_copy(b);
}

// [from test_blocks_mutable]
/*
 * Test: __block storage qualifier for mutable captures
 * Tests that blocks can mutate captured variables with __block
 */

// [from test_blocks_nested]
/*
 * Regression test: nested block literals (#423)
 *
 * A block literal defined inside another block literal must be able to capture
 * variables from both the enclosing block's frame and the enclosing function's
 * frame.  Captures are snapshotted by value into each block's descriptor; an
 * inner block reaches a grandparent-scope variable through the enclosing
 * block's descriptor (transitive capture).
 *
 * The offset-collision case (Test 3) is deliberate: outer captures [p] so p
 * sits at slot 2 of outer's descriptor; inner captures [q, r, p] so p is at a
 * different slot of inner's descriptor.  A regression would cause inner to read
 * p from the wrong descriptor slot.
 *
 * Tests 6 and 7 cover three- and four-block-deep nesting (#427): each level
 * re-snapshots a grandparent-scope variable through the enclosing block's
 * descriptor (transitive capture), so a deep chain must thread the value down
 * one descriptor at a time.
 */

// [from test_blocks_release_no_stdlib]
/*
 * Regression test: Block_release works without <stdlib.h> (#458)
 *
 * Block_release previously searched compiler.globals for a "free" prototype.
 * If the TU did not include <stdlib.h>, the search failed and Block_release
 * degraded to a no-op (ND_NULL_EXPR), silently leaking the Block_copy'd heap
 * allocation.
 *
 * Fix: declare_builtin_functions() now injects a builtin "free" prototype so
 * Block_release always resolves regardless of which headers are included.
 *
 * This test deliberately omits <stdlib.h> to exercise the fixed path.
 */

typedef int (^IB)(void);

static IB _blocks_release_no_stdlib_make_adder(int base) {
    IB b = ^{ return base + 1; };
    return Block_copy(b);
}

#pragma cccc suite begin "blocks"

// test_blocks_basic
[[cccc::test(return = 42)]]
int test_blocks_basic(void) {
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

// test_blocks_capture
[[cccc::test(return = 42)]]
int test_blocks_capture(void) {
    // Test 1: Simple capture - block should capture value of x at creation time
    int x = 10;
    int (^get_x)(void) = ^{ return x; };
    
    // Modify x after block creation
    x = 20;
    
    // Block should still return 10 (captured at creation time)
    int result1 = get_x();
    if (result1 != 10) return 1;  // Expected: 10, not 20
    
    // Test 2: Capture multiple variables
    int a = 5;
    int b = 7;
    int (^add_captured)(void) = ^{ return a + b; };
    
    // Modify after capture
    a = 100;
    b = 200;
    
    // Should still return 5 + 7 = 12
    int result2 = add_captured();
    if (result2 != 12) return 2;
    
    // Test 3: Captured variable used with block parameter
    int multiplier = 3;
    int (^multiply)(int) = ^(int n) { return n * multiplier; };
    
    multiplier = 100;  // Won't affect the block
    
    int result3 = multiply(4);  // 4 * 3 = 12
    if (result3 != 12) return 3;
    
    return 42;
}

// test_blocks_copy
[[cccc::test(return = 42)]]
int test_blocks_copy(void) {
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

// test_blocks_mutable
[[cccc::test(return = 42)]]
int test_blocks_mutable(void) {
    // Test 1: __block variable mutation through block
    __block int x = 10;
    void (^mutate)(int) = ^(int v) { x = v; };
    mutate(42);
    if (x != 42) return 1;
    
    // Test 2: Multiple blocks sharing __block variable
    __block int shared = 0;
    void (^inc)(void) = ^{ shared++; };
    void (^dec)(void) = ^{ shared--; };
    inc(); inc(); dec();
    if (shared != 1) return 2;
    
    // Test 3: __block with block parameters
    __block int counter = 0;
    int (^add_and_get)(int) = ^(int n) { counter += n; return counter; };
    if (add_and_get(5) != 5) return 3;
    if (add_and_get(3) != 8) return 4;
    
    // Test 4: Mix of __block and regular captures
    int const_val = 100;
    __block int mut_val = 0;
    int (^mixed)(void) = ^{ mut_val = const_val; return mut_val; };
    if (mixed() != 100) return 5;
    if (mut_val != 100) return 6;
    
    return 42;
}

// test_blocks_nested
[[cccc::test(return = 42)]]
int test_blocks_nested(void) {
    /* Test 1: inner captures grandparent-function local (x) and parent-block
     * local (y) */
    int x = 10;
    int (^outer)(void) = ^{
        int y = 20;
        int (^inner)(void) = ^{ return x + y; };
        return inner();
    };
    if (outer() != 30) return 1;

    /* Test 2: outer block's snapshot of the grandparent var is frozen at
     * creation time */
    int a = 5;
    int (^o2)(void) = ^{
        int b = 7;
        int (^i2)(void) = ^{ return a + b; };
        return i2();
    };
    a = 99; /* changed after o2 was created -- o2's descriptor still holds 5 */
    if (o2() != 12) return 2;

    /* Test 3: offset-collision -- outer captures [p] at slot 1, inner captures
     * [q, r, p] so p lands at slot 3. */
    int p = 3;
    int (^o3)(void) = ^{
        int q = 4;
        int r = 5;
        int (^i3)(void) = ^{ return q + r + p; };
        return i3();
    };
    if (o3() != 12) return 3;

    /* Test 4: nested block with parameters */
    int base = 10;
    int (^o5)(void) = ^{
        int mul = 3;
        int (^i5)(int n) = ^(int n) { return base + mul * n; };
        return i5(4);
    };
    if (o5() != 22) return 4;

    /* Test 5: __block mutation through a nested block */
    __block int counter = 0;
    void (^o6)(void) = ^{
        void (^i6)(void) = ^{ counter += 10; };
        i6();
    };
    o6();
    if (counter != 10) return 5;

    /* Test 6: three-block-deep nesting (#427) -- lv3 reaches v (main) and w
     * (lv1) transitively through lv2's descriptor. */
    int v = 100;
    int (^lv1)(void) = ^{
        int w = 200;
        int (^lv2)(void) = ^{
            int z = 300;
            int (^lv3)(void) = ^{ return v + w + z; };
            return lv3();
        };
        return lv2();
    };
    if (lv1() != 600) return 6;

    /* Test 7: four-block-deep nesting -- d1's local threads all the way down to
     * d4 through three intermediate descriptors. */
    int e1 = 1;
    int (^d1)(void) = ^{
        int e2 = 20;
        int (^d2)(void) = ^{
            int e3 = 300;
            int (^d3)(void) = ^{
                int e4 = 4000;
                int (^d4)(void) = ^{ return e1 + e2 + e3 + e4; };
                return d4();
            };
            return d3();
        };
        return d2();
    };
    if (d1() != 4321) return 7;

    return 42;
}

// test_blocks_release_no_stdlib
[[cccc::test(return = 42)]]
int test_blocks_release_no_stdlib(void) {
    /* Basic copy + release without <stdlib.h> */
    IB a = _blocks_release_no_stdlib_make_adder(41);
    IB b = _blocks_release_no_stdlib_make_adder(99);
    int ra = a();
    int rb = b();
    if (ra != 42) return 1;
    if (rb != 100) return 2;
    Block_release(a);
    Block_release(b);

    return 42;
}

#pragma cccc suite end
