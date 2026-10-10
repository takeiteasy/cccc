// CCCC_FLAGS: -mavx
//
// -mavx raises gcc's cap on C11 _Alignof(type) of a wide vector to 32.
typedef float v8f32 __attribute__((vector_size(32)));
typedef float v16f32 __attribute__((vector_size(64)));

#if defined(__x86_64__) && !defined(__APPLE__)
#ifndef __AVX__
#error "-mavx must define __AVX__"
#endif
#define C11_32 32
#define C11_64 32
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
