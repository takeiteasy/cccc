// CCCC_NATIVE_SKIP: gnu_inline bodies are not emitted (#1710)
// Expected return: 42
// A called gnu_inline definition compiles its own body, including the
// inline-only helpers it calls.
extern inline __attribute__((gnu_inline)) int helper(int x) {
    return x + 2;
}

extern inline __attribute__((gnu_inline)) int caller(int x) {
    return helper(x) * 2;
}

int main(void) {
    return caller(19) == 42 ? 42 : 1;
}
