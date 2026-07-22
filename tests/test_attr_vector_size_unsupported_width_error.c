// EXPECT_COMPILE_ERROR
// The VM substrate currently supports only 128-bit (16-byte) vector
// registers (tracker #72/#463 follow-up: wider vectors). vector_size(32)
// must be rejected with a clear diagnostic, not silently truncated.

typedef float v8sf __attribute__((vector_size(32)));

int main(void) {
    v8sf a;
    a[0] = 1.0f;
    return (int)a[0];
}
