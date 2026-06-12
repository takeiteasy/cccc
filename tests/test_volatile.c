// Test volatile keyword parsing and semantics.
// Verifies: correct values observed through volatile accesses, no caching,
// correct behaviour through pointer aliases and signal-like patterns.

#include <signal.h>

volatile int global_volatile = 42;

static volatile sig_atomic_t flag = 0;

int main() {
    // Basic volatile local
    volatile int x = 10;

    // Volatile pointer
    volatile int *vp = &x;

    // Pointer to volatile (unused but must parse)
    int * volatile pv;
    (void)pv;

    // Const volatile
    const volatile int cv = 100;

    // Read volatile variable
    int a = x;

    // Write volatile variable
    x = 20;

    // Read through volatile pointer (must re-read, not cache)
    int b = *vp;

    if (a != 10) return 1;
    if (b != 20) return 2;
    if (cv != 100) return 3;
    if (global_volatile != 42) return 4;

    // Alias test: write through non-volatile alias, read back via volatile
    // pointer — the volatile read must see the updated value.
    volatile int aliased = 0;
    int *alias = (int *)&aliased;
    *alias = 99;
    if (aliased != 99) return 5;

    // Loop test: volatile counter modified through alias inside loop.
    // If the volatile read were hoisted out of the loop this would fail.
    volatile int counter = 0;
    int *cp = (int *)&counter;
    for (int i = 0; i < 5; i++) {
        *cp = i;
        if (counter != i) return 6;
    }

    // Signal-flag pattern: write to volatile sig_atomic_t, read back.
    flag = 1;
    if (flag != 1) return 7;
    flag = 0;
    if (flag != 0) return 8;

    // Volatile struct member access
    struct { volatile int v; int n; } s;
    s.v = 77;
    s.n = 3;
    if (s.v != 77) return 9;

    // Volatile global write and re-read
    global_volatile = 55;
    if (global_volatile != 55) return 10;

    return 42;
}
