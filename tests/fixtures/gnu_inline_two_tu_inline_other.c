extern inline __attribute__((gnu_inline)) int twice(int x) {
    return x * 2;
}

int other_twice(void) {
    return twice(20);
}
