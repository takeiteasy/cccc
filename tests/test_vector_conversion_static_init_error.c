// EXPECT_COMPILE_ERROR
// A file-scope initializer converts like a local one: float4 does not
// initialize an int4 under --compiler-family=gcc.

typedef float v4 __attribute__((vector_size(16)));
typedef int   i4 __attribute__((vector_size(16)));

i4            g = (v4){1, 2, 3, 4};

int main(void) {
    return 42;
}
