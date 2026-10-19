// Expected return: 42
// A gnu_inline definition whose address is taken still has a symbol.
extern inline __attribute__((gnu_inline)) int add_two(int x) {
    return x + 2;
}

int main(void) {
    int (*volatile fp)(int) = add_two;
    return fp(40);
}
