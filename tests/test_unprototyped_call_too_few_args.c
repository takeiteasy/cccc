// CCCC_FLAGS: --std=c17
int g();

int main(void) {
    if (0)
        g(1);
    return g(40, 2);
}
int g(int a, int b) {
    return a + b;
}
