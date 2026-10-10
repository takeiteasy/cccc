// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -m
// CCCC_EXPECT_STDERR: a pointer inside a struct/union with a
//
// A struct/union with a _BitInt(N>128) bit-field is emitted as opaque bytes,
// so a global initializer is its byte image -- which has no spelling for a
// pointer (a relocation).
int x;

struct WithPointer {
    int *p;
    _BitInt(256) f : 100;
};

struct WithPointer g = {&x, 1};

int main(void) {
    return (int)g.f;
}
