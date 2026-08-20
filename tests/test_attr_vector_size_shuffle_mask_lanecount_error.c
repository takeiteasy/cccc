// EXPECT_COMPILE_ERROR
// __builtin_shuffle (tracker #723): a runtime/named vector mask must have
// the SAME LANE COUNT as the vector being shuffled -- a mismatched lane
// count is rejected with a diagnostic even though the mask's element size
// matches.

typedef int v4si __attribute__((vector_size(16)));
typedef int v8si __attribute__((vector_size(32)));

int main(void) {
    v4si a       = {1, 2, 3, 4};
    v8si badmask = {0, 1, 2, 3, 0, 0, 0, 0};      // 8 lanes, not 4
    v4si r       = __builtin_shuffle(a, badmask); // error: lane count mismatch
    return r[0];
}
