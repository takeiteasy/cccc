// CCCC_FLAGS: -O0
// CCCC_MATRIX_SKIP: address-identity assertion, sensitive to bytecode
// reordering under an individual -f pass #981: alloca/VLA storage is reclaimed
// at frame exit when it is provably safe to do so (no address-keyed safety
// feature enabled, single-threaded). A VLA declared inside a non-recursive
// function called repeatedly must get the exact same address back every time --
// the VM heap's bump pointer should rewind to the same watermark at each LEV3,
// not grow unboundedly.
static long long addr_of_vla(int k) {
    int u[k];
    u[0] = 42;
    return (long long)&u[0];
}

int main(void) {
    long long first = addr_of_vla(4);
    for (int i = 0; i < 20; i++) {
        long long addr = addr_of_vla(4);
        if (addr != first)
            return 1;
    }
    return 42;
}
