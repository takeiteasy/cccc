// CCCC_FLAGS: -O2
// Test CSE for [[gnu::const]] functions.
// When the same [[gnu::const]] function is called twice with the same
// argument value numbers (constants or unchanged locals), the second call
// is replaced by a register move at -O2+.

[[gnu::const]] static int square(int x) { return x * x; }
[[gnu::const]] static int add2(int x, int y) { return x + y; }

int main(void) {
    // --- constant-arg CSE ---
    int a = square(7);
    int b = square(7);  // same constant arg -> CSE
    if (a != 49) return 1;
    if (b != 49) return 2;
    if (a != b) return 3;

    // --- local-variable CSE: p not modified between calls ---
    int p = 5;
    int c = square(p);
    int d = square(p);  // same local slot, unchanged -> CSE
    if (c != 25) return 4;
    if (d != 25) return 5;
    if (c != d) return 6;

    // --- no CSE: p modified between calls ---
    int e = square(p);   // p==5, result 25
    p = 6;
    int f = square(p);   // p==6, result 36
    if (e != 25) return 7;
    if (f != 36) return 8;
    if (e == f) return 9;  // must differ

    // --- multi-arg CSE ---
    int u = add2(3, 4);
    int v = add2(3, 4);  // same two constant args -> CSE
    if (u != 7) return 10;
    if (v != 7) return 11;
    if (u != v) return 12;

    return 42;
}
