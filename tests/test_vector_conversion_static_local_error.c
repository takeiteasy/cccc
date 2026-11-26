// EXPECT_COMPILE_ERROR
// A static local initializer converts like an automatic one.

typedef float v4 __attribute__((vector_size(16)));
typedef int   i4 __attribute__((vector_size(16)));

int main(void) {
    static i4 s = (v4){1, 2, 3, 4};
    return (int)s[0];
}
