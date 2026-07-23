// EXPECT_COMPILE_ERROR
// vector_size(128) (1024-bit) exceeds the widest substrate width (64 bytes /
// 512-bit, tracker #722) and must be rejected with a clear diagnostic.

typedef float v32sf __attribute__((vector_size(128)));

int main(void) {
    v32sf a;
    a[0] = 1.0f;
    return (int)a[0];
}
