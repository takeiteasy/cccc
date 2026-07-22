// EXPECT_COMPILE_ERROR
// Binary ops between incompatible vector_size types (different element type
// / lane count, even at the same total byte size) must be rejected
// (tracker #72).

typedef int v4si __attribute__((vector_size(16)));  // 4 x int32 lanes
typedef long v2di __attribute__((vector_size(16))); // 2 x int64 lanes

int main(void) {
    v4si a;
    v2di b;
    a[0] = 1;
    b[0] = 1;
    v4si c = a + b; // vector types do not match
    return (int)c[0];
}
