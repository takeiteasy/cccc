// Test: VLA array parameters decay to pointer (C99 §6.7.6.3p7)
// Ticket: #413 — void f(int n, int a[n]) previously errored "undefined variable 'n'"
// Expected return: 42

int sum(int n, int a[n]) {
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i];
    return s;
}

// Forward declaration exercises the is_function lookahead path
int dot(int n, int a[n], int b[n]);

int dot(int n, int a[n], int b[n]) {
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}

// Qualifiers on bracketed dimension transfer to the decayed pointer
int sum_const(int n, const int a[n]) {
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i];
    return s;
}

int main() {
    int a[4] = {1, 2, 3, 4};
    int b[4] = {4, 3, 2, 1};

    if (sum(4, a) != 10) return 1;
    if (dot(4, a, b) != 20) return 2;
    if (sum_const(4, a) != 10) return 3;

    return 42;
}
