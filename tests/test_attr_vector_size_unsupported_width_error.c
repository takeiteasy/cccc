// EXPECT_COMPILE_ERROR
// vector_size must be a power of two no larger than 64 bytes. A multiple of
// the element size that is not a power of two, like 24 bytes, is rejected with
// a clear diagnostic, not silently truncated or padded.

typedef float v6sf __attribute__((vector_size(24)));

int main(void) {
    v6sf a;
    a[0] = 1.0f;
    return (int)a[0];
}
