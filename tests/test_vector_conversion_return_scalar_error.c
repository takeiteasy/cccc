// EXPECT_COMPILE_ERROR
// A scalar does not convert to a vector return type.

typedef float v4 __attribute__((vector_size(16)));

v4 f(void) {
    return 1.0f;
}

int main(void) {
    return 42;
}
