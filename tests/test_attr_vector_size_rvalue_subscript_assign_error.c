// EXPECT_COMPILE_ERROR
// A lane of a vector rvalue is not an lvalue: gcc rejects assigning to it.

typedef int v4 __attribute__((vector_size(16)));

int main(void) {
    v4 a = {0}, b = {0};
    (a + b)[0] = 1; // error: not an lvalue
    return 0;
}
