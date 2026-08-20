// CCCC_FLAGS: -O0
// CCCC_MATRIX_SKIP: address-identity assertion, sensitive to bytecode
// reordering under an individual -f pass #981: a bare __builtin_alloca in the
// SAME block as a VLA declaration must NOT be reclaimed at that block's exit --
// only the VLA's own storage (ALLOC_KIND_FRAME) dies at block exit; a bare
// alloca (ALLOC_KIND_ALLOCA) lives until the *frame* returns, matching every
// mainstream compiler's alloca semantics. This is the load-bearing regression
// test for that split: reusing one AllocKind for both (as ALCA alone did before
// this ticket) would have HREL sweep the still-live alloca'd block too,
// silently corrupting it the moment the loop's next iteration reused that
// address -- a real miscompile of working code, not a missed optimization.
// The alloca pointer is stashed into an array that outlives the block it
// was allocated in, then everything is read back and verified only after
// the whole loop (and all the VLA-triggered HREL calls) has finished.
int main(void) {
    int   k = 4;
    char *ptrs[10];
    for (int i = 0; i < 10; i++) {
        int u[k]; // triggers this block's HMRK/HREL
        u[0]    = i;
        char *p = __builtin_alloca(
            8);   // ALLOC_KIND_ALLOCA -- must survive this block's HREL
        p[0]    = (char)(i + 1);
        ptrs[i] = p;
    }

    // Addresses must be distinct: if HREL had wrongly swept the alloca'd
    // blocks, the bump pointer would have rewound to the same watermark
    // every iteration and every ptrs[i] would coincide.
    for (int i = 0; i < 10; i++)
        for (int j = i + 1; j < 10; j++)
            if (ptrs[i] == ptrs[j])
                return 1;

    // Values must still be intact -- if the storage had been reclaimed and
    // reused (e.g. by a later VLA), this would read back corrupted data.
    for (int i = 0; i < 10; i++)
        if (ptrs[i][0] != (char)(i + 1))
            return 2;

    return 42;
}
