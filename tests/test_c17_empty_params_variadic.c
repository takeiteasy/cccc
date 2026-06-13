// CCCC_FLAGS: --std=c17
// In C17, int foo() is K&R style — no prototype, accepts any arguments.
// Calling it with arguments must not be a compile error.
int add();

int add(int a, int b) {
    return a + b;
}

int main(void) {
    return add(19, 23) == 42 ? 42 : 1;
}
