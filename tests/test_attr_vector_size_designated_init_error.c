// EXPECT_COMPILE_ERROR
// A designated initializer on a vector_size vector lane
// (`v4sf a = {[2] = 3.0f};`) is rejected, matching real GCC/clang: vector
// types are non-aggregate in their model, and C's designated-initializer
// grammar only applies to aggregates (arrays/structs/unions). Verified
// against `gcc`/`clang` directly: both reject this with "initialization of
// non-aggregate type 'v4sf' ... with a designated initializer list", in
// both the direct-declaration and compound-literal forms.

typedef float v4sf __attribute__((vector_size(16)));

int main(void) {
    v4sf a = {[2] = 3.0f}; // error: designated init on non-aggregate vector
    return (int)a[2];
}
