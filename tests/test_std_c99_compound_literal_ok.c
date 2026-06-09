// CCCC_FLAGS: --std=c99
int f(void) {
    int *p = (int []){10, 20, 30};
    return p[1];
}
int main(void) { return f() == 20 ? 42 : 1; }
