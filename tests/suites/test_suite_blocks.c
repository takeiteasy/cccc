// CCCC_FLAGS: --testing
// Consolidated suite: Apple Blocks extension
// Source tests: test_blocks_basic, test_blocks_capture, test_blocks_copy, test_blocks_mutable, test_blocks_nested, test_blocks_release_no_stdlib, test_blocks_large_capture

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

// [from test_blocks_large_capture]
/*
 * Regression test: block capture of a by-value aggregate larger than 8
 * bytes (#994).
 *
 * The block descriptor used to be a flat one-8-byte-word-per-capture
 * array, and the capture-copy loop always did exactly one 8-byte
 * load+store regardless of the capture's real size -- so a struct/union/
 * array capture wider than one word was silently truncated to its first
 * word (and a wide _BitInt/_Decimal capture, which is address-based,
 * stored a dangling pointer into the enclosing frame instead of a value).
 * Fixed by sizing each descriptor slot from the capture's own type
 * (cc_block_capture_offset/cc_block_desc_size, shared between parse.c and
 * codegen.c) and copying a wide slot via MCPY instead of a truncating
 * LDR_D/STR_D pair.
 */

struct BigS { long a; long b; };
struct FiveInts { int a, b, c, d, e; };

typedef int (^IntBlock2)(void);
typedef struct BigS (^BigSBlock)(void);

// Escapes its frame via Block_copy with a struct capture larger than 8
// bytes -- the primary "return Block_copy(^{...})" idiom (mirrors
// make_adder above), which evaluates the block literal inside an MCPY's
// own argument-list position.
static IntBlock2 _large_capture_make_reader(struct BigS s) {
    IntBlock2 b = ^{ return (int)(s.a + s.b); };
    return Block_copy(b);
}

static int _large_capture_use_bigs(struct BigS s) {
    return (int)(s.a + s.b);
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

// test_blocks_large_capture
[[cccc::test(return = 42)]]
int test_blocks_large_capture(void) {
    // Test 1: two-long struct capture -- exactly the #994 ticket repro,
    // one word over the old flat 8-byte-per-capture slot.
    struct BigS t1;
    t1.a = 1;
    t1.b = 41;
    int (^r1)(void) = ^{ return (int)(t1.a + t1.b); };
    if (r1() != 42) return 1;

    // Test 2: five-int struct (20 bytes, not a multiple of 8) capture.
    struct FiveInts t2 = {1, 2, 3, 4, 32};
    int (^r2)(void) = ^{ return t2.a + t2.b + t2.c + t2.d + t2.e; };
    if (r2() != 42) return 2;

    // Test 3: two large captures in sequence, pins the slot-offset
    // arithmetic (not just "bigger than one slot").
    struct BigS t3a;
    t3a.a = 1;
    t3a.b = 2;
    struct BigS t3b;
    t3b.a = 3;
    t3b.b = 4;
    int (^r3)(void) = ^{ return (int)(t3a.a + t3a.b + t3b.a + t3b.b + 32); };
    if (r3() != 42) return 3;

    // Test 4: return the whole captured struct by value from inside the
    // block -- exercises the read side (gen_addr's capture-address path)
    // rather than a scalar member read.
    struct BigS t4;
    t4.a = 20;
    t4.b = 22;
    BigSBlock r4 = ^{ return t4; };
    struct BigS out4 = r4();
    if (out4.a + out4.b != 42) return 4;

    // Test 5: pass the captured struct by value to a function from inside
    // the block -- another read-side shape (the whole aggregate, not just
    // one member).
    struct BigS t5;
    t5.a = 30;
    t5.b = 12;
    int (^r5)(void) = ^{ return _large_capture_use_bigs(t5); };
    if (r5() != 42) return 5;

    // Test 6: escape via Block_copy with a large struct *parameter*
    // capture -- params passed by value larger than one register are
    // themselves passed by pointer (a second, independent ABI gap #994
    // uncovered: the capture-copy loop must dereference that pointer
    // before it becomes the MCPY source, not treat the frame slot's own
    // address as the data).
    struct BigS t6;
    t6.a = 40;
    t6.b = 2;
    IntBlock2 r6 = _large_capture_make_reader(t6);
    int result6 = r6();
    Block_release(r6);
    if (result6 != 42) return 6;

    // Test 7: transitive (nested) capture of a large struct through a
    // parent block's own descriptor.
    struct BigS t7;
    t7.a = 15;
    t7.b = 15;
    int (^outer7)(void) = ^{
        int (^inner7)(void) = ^{ return (int)(t7.a + t7.b + 12); };
        return inner7();
    };
    if (outer7() != 42) return 7;

    // Test 8: a wide _BitInt(128) capture (address-based storage, the
    // other #994 kind that used to store a dangling pointer instead of a
    // value) escaping its frame via Block_copy.
    _BitInt(128) t8 = 42;
    IntBlock2 b8 = ^{ return (int)t8; };
    IntBlock2 r8 = Block_copy(b8);
    int result8 = r8();
    Block_release(r8);
    if (result8 != 42) return 8;

    // Test 9: a bare array capture (accepted by this compiler, unlike
    // clang) is subject to the exact same truncation bug and must copy
    // in full, not just its first 8 bytes (two ints).
    int t9[3] = {1, 2, 39};
    int (^r9)(void) = ^{ return t9[0] + t9[1] + t9[2]; };
    if (r9() != 42) return 9;

    return 42;
}

// #994/#982 pattern: a `-2`-flagged duplicate of the large-capture MCPY
// path (test 1 above) as a CHKD false-positive canary -- the new MCPY
// site introduced by #994's fix is exactly the kind of "did the bounds
// check move to the correct side" question #982/#983 raised for the
// struct-assignment MCPY site.
[[cccc::test(return = 42, flags = "-2")]]
int test_blocks_large_capture_bounds_checked(void) {
    struct BigS t;
    t.a = 1;
    t.b = 41;
    int (^r)(void) = ^{ return (int)(t.a + t.b); };
    return r() == 42 ? 42 : 1;
}

#pragma cccc suite end
