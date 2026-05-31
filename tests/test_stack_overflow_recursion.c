// Test: deep recursion now grows the stack on demand (ticket #75)
// Previously this would trigger a stack overflow at 256KB; now it should
// complete successfully because the stack segment grows via reserve-and-commit.

int recurse(int n) {
    if (n <= 0) return 0;
    return recurse(n - 1) + 1;
}

int main() {
    int result = recurse(100000);
    return result == 100000 ? 42 : 1;
}
