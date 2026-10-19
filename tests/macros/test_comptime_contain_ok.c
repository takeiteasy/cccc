// CCCC_NATIVE_SKIP: -c=native rejects FFI policy options
// CCCC_FLAGS: --disable-ffi
// Comptime reads and writes of VM memory and builtin results stay allowed.

struct Pair { int a, b; };

[[cccc::comptime]] static int table[4] = {10, 11, 12, 9};

[[cccc::comptime]]
Node *sum(void) {
    struct Pair local = {20, 0};
    struct Pair copy  = local;
    char buf[8]       = "ab";
    buf[2]            = 'c';
    const char *s     = "xyz";
    int total         = copy.a + table[3] + (buf[2] - 'c') + (s[1] - 'y');
    int *heap_int     = __builtin_alloca(sizeof(int));
    *heap_int         = 13;
    return __builtin_ast_int_literal(total + *heap_int);
}

int main(void) {
    return sum();
}
