// EXPECT_COMPILE_ERROR
// Vector-by-value through the native FFI marshalling path is not supported
// (tracker #714): the FFI call convention marshals args/return as plain
// 64-bit slots and has no by-memory (RETBUF/pointer-arg) machinery for a
// vregs[]-resident value. Must be rejected with a clear diagnostic rather
// than silently mis-marshalling.

typedef float v4sf __attribute__((vector_size(16)));

extern int strcmp(v4sf a);

int main(void) {
    v4sf a;
    a[0] = 1.0f;
    return strcmp(a);
}
