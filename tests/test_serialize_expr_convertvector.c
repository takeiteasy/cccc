// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __builtin_convertvector\(
// CCCC_REJECT_STDOUT: unsupported expr kind
//
// `__builtin_convertvector` serializes back to itself, with the target type
// spelled through the vector type serializer (`float
// __attribute__((vector_size(16)))`). gcc and clang both accept an
// attributed type name in that argument position, so no intermediate
// typedef has to be emitted.

typedef int   v4i __attribute__((vector_size(16)));
typedef float v4f __attribute__((vector_size(16)));

int main(void) {
    v4i a = {10, 11, 10, 11};
    v4f b = __builtin_convertvector(a, v4f);
    return (int)(b[0] + b[1] + b[2] + b[3]);
}
