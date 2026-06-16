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

int main() {
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
