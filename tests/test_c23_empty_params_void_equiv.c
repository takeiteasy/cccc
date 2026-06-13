// CCCC_FLAGS: --std=c23
// In C23, int foo() is equivalent to int foo(void).
// These two declarations must be compatible — no redeclaration error.
int add();
int add(void);

int add(void) {
    return 42;
}

int main(void) {
    return add();
}
