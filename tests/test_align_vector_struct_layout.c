// A struct with a 32/64-byte vector member lays out per the host ABI's vector
// alignment.
#include <stddef.h>
typedef float v8f32 __attribute__((vector_size(32)));
typedef float v16f32 __attribute__((vector_size(64)));

struct padded32 {
    char  c;
    v8f32 v;
};

struct padded64 {
    char   c;
    v16f32 v;
};

struct nested {
    char           c;
    struct padded32 arr[2];
};

int main(void) {
    if (offsetof(struct padded32, v) % _Alignof(v8f32) != 0 ||
        _Alignof(struct padded32) != _Alignof(v8f32))
        return 1;
    if (offsetof(struct padded64, v) % _Alignof(v16f32) != 0 ||
        _Alignof(struct padded64) != _Alignof(v16f32))
        return 2;
    if (offsetof(struct nested, arr) % _Alignof(v8f32) != 0 ||
        _Alignof(struct nested) != _Alignof(v8f32))
        return 3;
    return 42;
}
