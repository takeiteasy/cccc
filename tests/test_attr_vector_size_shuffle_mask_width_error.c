// EXPECT_COMPILE_ERROR
// __builtin_shuffle (tracker #723): a runtime/named vector mask must have
// the SAME ELEMENT BYTE WIDTH as the vector being shuffled -- a mask with a
// matching lane count but a different element size is rejected with a
// diagnostic.

typedef int v4si __attribute__((vector_size(16)));       // 4 lanes of 4-byte int
typedef long long v4di __attribute__((vector_size(32))); // 4 lanes of 8-byte long long

int main(void) {
    v4si a = {1, 2, 3, 4};
    v4di badmask = {0, 1, 2, 3}; // same lane count (4), different element size
    v4si r = __builtin_shuffle(a, badmask); // error: element size mismatch
    return r[0];
}
