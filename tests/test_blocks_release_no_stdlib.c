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

static IB make_adder(int base) {
    IB b = ^{ return base + 1; };
    return Block_copy(b);
}

int main(void) {
    /* Basic copy + release without <stdlib.h> */
    IB a = make_adder(41);
    IB b = make_adder(99);
    int ra = a();
    int rb = b();
    if (ra != 42) return 1;
    if (rb != 100) return 2;
    Block_release(a);
    Block_release(b);

    return 42;
}
