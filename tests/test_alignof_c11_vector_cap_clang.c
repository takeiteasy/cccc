// CCCC_FLAGS: --compiler-family=clang
//
// clang never caps C11 _Alignof of a wide vector.
typedef float v8f32 __attribute__((vector_size(32)));

struct plain {
    char  c;
    v8f32 v;
};

int main(void) {
    if (_Alignof(v8f32) != __alignof__(v8f32))
        return 1;
    if (_Alignof(struct plain) != __alignof__(struct plain))
        return 2;
    return 42;
}
