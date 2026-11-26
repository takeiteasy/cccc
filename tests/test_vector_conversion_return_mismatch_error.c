// EXPECT_COMPILE_ERROR
// Returning a float vector from an int-vector function is an error under
// --compiler-family=gcc.

typedef float v4 __attribute__((vector_size(16)));
typedef int   i4 __attribute__((vector_size(16)));

i4 f(v4 x) {
    return x;
}

int main(void) {
    return 42;
}
