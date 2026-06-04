// JCC_FLAGS: -std=c99
int f(void) {
    int x = 1;
    x = 2;
    int y = 3;
    return x + y;
}
int main(void) { return f() == 5 ? 42 : 1; }
