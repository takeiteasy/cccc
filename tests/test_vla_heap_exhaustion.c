// #981: a VLA declared inside a loop must not grow the VM heap without
// bound. The VM heap's default reservation is 64 MiB (poolsize_max); this
// program allocates a 4 MiB VLA on every one of 64 iterations (256 MiB
// cumulative, 4x the reservation) all within one loop in one frame. Without
// reclamation this exhausts the heap and the program dies; with it, the
// bump pointer rewinds every iteration and the cumulative allocation never
// exceeds one VLA's worth. Deliberately no per-test flags override (and no
// matrix-skip annotation) -- unlike the address-identity tests, this is robust
// to any amount of instruction reordering under any optimization level: either
// the heap is reclaimed or the program dies, nothing in between.
int main(void) {
    long long k = 1024 * 1024; // 4 MiB of int
    for (int i = 0; i < 64; i++) {
        int u[k];
        u[0]     = i;
        u[k - 1] = i;
        if (u[0] != u[k - 1])
            return 1;
    }
    return 42;
}
