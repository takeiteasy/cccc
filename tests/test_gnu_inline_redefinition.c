// Expected return: 42
// A non-inline definition replaces an earlier gnu_inline one, for calls made
// before it too. The replaced body's callees are no longer needed.
extern int undefined_everywhere(int);

extern inline __attribute__((gnu_inline)) int inline_only(int x) {
    return undefined_everywhere(x);
}

extern inline __attribute__((gnu_inline)) int sq(int x) {
    return inline_only(x);
}

extern inline __attribute__((gnu_inline)) int twice(int x) {
    return inline_only(x);
}

int use(void) {
    return sq(5) + twice(1);
}

int sq(int x) {
    return x * x + 15;
}

static int twice(int x) {
    return x * 2;
}

int main(void) {
    return use();
}
