// EXPECT_COMPILE_ERROR
// Matching real GCC/clang: a bare scalar cannot initialize or be assigned
// to a whole vector_size vector -- only an arithmetic operator broadcasts
// a scalar (`v + 5.0f`), not plain assignment/initialization (tracker #72).
// Verified against `gcc`/`clang` directly: both reject this with
// "initializing 'v4sf' ... with an expression of incompatible type 'float'".

typedef float v4sf __attribute__((vector_size(16)));

int main(void) {
    v4sf v = 5.0f; // error: bare scalar, not a vector
    return (int)v[0];
}
