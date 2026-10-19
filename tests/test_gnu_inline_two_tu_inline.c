// CCCC_FLAGS: tests/fixtures/gnu_inline_two_tu_inline_other.c
// Expected return: 42
// The same gnu_inline body written in two translation units and called from
// both yields one definition.
extern inline __attribute__((gnu_inline)) int twice(int x) {
    return x * 2;
}
extern int other_twice(void);

int main(void) {
    return twice(1) + other_twice();
}
