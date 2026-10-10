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
    if (offsetof(struct padded32, v) % __alignof__(v8f32) != 0 ||
        __alignof__(struct padded32) != __alignof__(v8f32))
        return 1;
    if (offsetof(struct padded64, v) % __alignof__(v16f32) != 0 ||
        __alignof__(struct padded64) != __alignof__(v16f32))
        return 2;
    if (offsetof(struct nested, arr) % __alignof__(v8f32) != 0 ||
        __alignof__(struct nested) != __alignof__(v8f32))
        return 3;
    return 42;
}
