// CCCC_NATIVE_GCC_MACOS_SKIP: gcc's copy of a by-reference over-aligned argument is only 16-aligned
// Expected return: 42
// A by-value struct parameter with a 32-byte-aligned member sees a 32-aligned
// address.

typedef struct {
    char c;
    _Alignas(32) int i;
} Over;

static int misaligned(void *p, int align) {
    return (unsigned long long)p % (unsigned long long)align != 0;
}

static int take_over(char a, Over o) {
    if (misaligned(&o, 32) || misaligned(&o.i, 32))
        return 1;
    return o.i == 9 ? 0 : 2;
}

int main(void) {
    Over o = {1, 9};
    int  r = take_over(1, o);
    return r ? 20 + r : 42;
}
