// CCCC_FLAGS: --std=c17
int g();

int inside(void) {
    int g(int, int);
    return g(2.9, 40);
}

int outside(void) {
    return g(2, 40);
}

int g(int a, int b) {
    return a + b;
}

int main(void) {
    if (inside() != 42)
        return 1;
    if (outside() != 42)
        return 2;
    return 42;
}
