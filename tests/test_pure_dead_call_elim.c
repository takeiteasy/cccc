// CCCC_FLAGS: -O1
// Test dead-call elimination for pure/const functions.
// When a pure/const call's result is unused, the call is skipped at -O1+.
// Argument side effects must still execute.

__attribute__((pure)) int square(int x) { return x * x; }
__attribute__((const)) int cube(int x) { return x * x * x; }

int main(void) {
    int n = 3;

    // Result unused: call should be eliminated, but ++n must still run
    square(++n);
    if (n != 4) return 1;

    // Result unused with const function: ++n must still run
    cube(++n);
    if (n != 5) return 2;

    // Result used: call must still execute normally
    if (square(4) != 16) return 3;
    if (cube(3) != 27) return 4;

    // Multiple discarded calls in sequence
    int k = 0;
    square(++k);
    square(++k);
    if (k != 2) return 5;

    return 42;
}
