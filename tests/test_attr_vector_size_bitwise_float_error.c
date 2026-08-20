// EXPECT_COMPILE_ERROR
// GNU vector_size bitwise operators (tracker #715): & | ^ ~ are rejected on
// floating-point-lane vectors, matching GCC (bitwise ops only make sense on
// integer lanes).

typedef float v4sf __attribute__((vector_size(16)));

int main(void) {
    v4sf a = {1.0f, 2.0f, 3.0f, 4.0f};
    v4sf b = {1.0f, 1.0f, 1.0f, 1.0f};
    v4sf c =
        a & b; // error: '&' is not supported on floating-point vector types
    return c[0] == 1.0f ? 42 : 1;
}
