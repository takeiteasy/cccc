// CCCC_FLAGS: --stack-canaries
// #445 — functions with parameters must work under stack canaries.
// The canary lives at bp[-1]; params/locals must read from bp[-2] downward.

int add(int a, int b) {
    return a + b;
}

// Multiple int params plus a local variable.
int weighted(int a, int b, int c) {
    int local = a * 2;
    return local + b - c;
}

int main(void) {
    if (add(20, 22) != 42)
        return 1;
    if (weighted(10, 5, 7) != 18) // 20 + 5 - 7
        return 2;
    return 42;
}
