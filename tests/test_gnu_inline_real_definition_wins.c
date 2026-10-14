// CCCC_FLAGS: tests/fixtures/gnu_inline_real_def.c
// Expected return: 42
// A real definition in another translation unit takes precedence over a
// gnu_inline body, whichever file names it first.
extern inline __attribute__((gnu_inline)) int overridden(int x) {
    return x;
}

int main(void) {
    return overridden(2);
}
