// CCCC_MATRIX_SKIP: address-identity assertion, sensitive to bytecode
// reordering under an individual -f pass #981: a VLA declared inside a loop
// body (all within ONE frame) must be reclaimed at each iteration's *block*
// exit, not just at frame exit -- this is the ticket's headline symptom ("a VLA
// declared inside a loop... grows the VM heap without bound"). Proves the
// HMRK/HREL block-exit path, which a frame-exit-only fix
// (test_vla_frame_heap_reclaim.c) cannot cover.
int main(void) {
    int       k     = 4;
    long long first = 0;
    for (int i = 0; i < 20; i++) {
        int u[k];
        u[0]           = i;
        long long addr = (long long)&u[0];
        if (i == 0)
            first = addr;
        else if (addr != first)
            return 1;
    }
    return 42;
}
