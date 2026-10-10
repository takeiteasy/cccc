// C11 _Alignof(type) of a wide vector is capped at 16 under gcc on Linux
// x86_64 (no -mavx) unless the alignment is user-specified. __alignof__,
// _Alignof expr and struct layout keep the full vector width.
#include <stddef.h>

typedef float v8f32 __attribute__((vector_size(32)));
typedef float v16f32 __attribute__((vector_size(64)));
typedef v8f32 v8f32_alias;

struct plain {
    char  c;
    v8f32 v;
};
union plain_u {
    char  c;
    v8f32 v;
};
struct __attribute__((aligned(32))) user_aligned {
    char  c;
    v8f32 v;
};
struct alignas_member {
    char c;
    _Alignas(v8f32) char b[32];
};

#if defined(__x86_64__) && !defined(__APPLE__)
#define FULL32 32
#define FULL64 64
#if __CCCC_COMPILER_FAMILY__
#define C11_32 32
#define C11_64 64
#else
#define C11_32 16
#define C11_64 16
#endif
#else
#define FULL32 16
#define FULL64 16
#define C11_32 16
#define C11_64 16
#endif

v8f32 g_vec;

int main(void) {
    if (_Alignof(v8f32) != C11_32)
        return 1;
    if (_Alignof(v16f32) != C11_64)
        return 2;
    if (__alignof__(v8f32) != FULL32 || __alignof(v16f32) != FULL64)
        return 3;
    if (_Alignof(v8f32[2]) != C11_32 || _Alignof(struct plain) != C11_32 ||
        _Alignof(union plain_u) != C11_32 || _Alignof(v8f32_alias) != C11_32 ||
        _Alignof(const v8f32) != C11_32)
        return 4;
    if (_Alignof(g_vec) != FULL32 ||
        __alignof__(struct plain) != FULL32)
        return 5;
    if (offsetof(struct plain, v) != FULL32)
        return 6;
    if (_Alignof(struct user_aligned) != 32 ||
        __alignof__(struct user_aligned) != 32)
        return 7;
    if (offsetof(struct alignas_member, b) != C11_32)
        return 8;
    return 42;
}
