// EXPECT_COMPILE_ERROR
// The VM substrate supports 16-, 32-, and 64-byte vector registers
// (tracker #72/#463, widened to 256/512-bit by #722). Any other width --
// including a non-power-of-two multiple like 24 bytes, or a vector wider
// than the 64-byte substrate -- must be rejected with a clear diagnostic,
// not silently truncated or padded.

typedef float v6sf __attribute__((vector_size(24)));

int main(void) {
    v6sf a;
    a[0] = 1.0f;
    return (int)a[0];
}
