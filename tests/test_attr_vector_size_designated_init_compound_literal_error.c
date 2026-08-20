// EXPECT_COMPILE_ERROR
// Companion to test_attr_vector_size_designated_init_error.c: the same
// rejection in compound-literal form (`(v4sf){[2] = 3.0f}`), verified
// against real gcc/clang identically to the direct-declaration case.

typedef float v4sf __attribute__((vector_size(16)));

int main(void) {
    v4sf a =
        (v4sf){[2] = 3.0f}; // error: designated init on non-aggregate vector
    return (int)a[2];
}
