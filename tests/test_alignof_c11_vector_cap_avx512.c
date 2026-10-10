// CCCC_FLAGS: -mavx512f
//
// -mavx512f raises gcc's cap on C11 _Alignof(type) of a wide vector to 64.
typedef float v8f32 __attribute__((vector_size(32)));
typedef float v16f32 __attribute__((vector_size(64)));

#if defined(__x86_64__) && !defined(__APPLE__)
#if !defined(__AVX__) || !defined(__AVX2__) || !defined(__AVX512F__)
#error "-mavx512f must define __AVX__, __AVX2__ and __AVX512F__"
#endif
#define C11_32 32
#define C11_64 64
#else
#define C11_32 16
#define C11_64 16
#endif

int main(void) {
    if (_Alignof(v8f32) != C11_32)
        return 1;
    if (_Alignof(v16f32) != C11_64)
        return 2;
    return 42;
}
