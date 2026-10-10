// _Alignof and __alignof__ of a typedef report its aligned(N) request.
typedef int i64 __attribute__((aligned(64)));
typedef float f32a __attribute__((aligned(32)));
typedef int plain_t;

struct s {
    char c;
    i64  v;
};

int main(void) {
    if (_Alignof(i64) != 64 || __alignof__(i64) != 64)
        return 1;
    if (_Alignof(f32a) != 32)
        return 2;
    if (_Alignof(plain_t) != 4)
        return 3;
    if (_Alignof(struct s) != 64)
        return 4;
    i64 x = 0;
    if (_Alignof(x) != 64)
        return 5;
    return 42;
}
