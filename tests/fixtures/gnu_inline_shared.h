extern inline __attribute__((gnu_inline)) int shared_twice(int x) {
    return x * 2;
}
extern inline __attribute__((gnu_inline)) int shared_unused(int x) {
    return undefined_shared_callee(x);
}
