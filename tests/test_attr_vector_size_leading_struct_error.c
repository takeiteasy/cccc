// EXPECT_COMPILE_ERROR
// A leading vector_size applies to the base type, which must be a 1/2/4/8-byte
// integer or floating-point scalar; a struct base is rejected as in gcc.

struct S {
    int x;
};

int main(void) {
    __attribute__((vector_size(16))) struct S s;
    return 0;
}
