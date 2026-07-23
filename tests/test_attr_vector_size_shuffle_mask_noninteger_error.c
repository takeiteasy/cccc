// EXPECT_COMPILE_ERROR
// __builtin_shuffle (tracker #723): a runtime/named vector mask must have
// INTEGER lanes -- a float-lane mask is rejected with a diagnostic.

typedef int v4si __attribute__((vector_size(16)));
typedef float v4sf __attribute__((vector_size(16)));

int main(void) {
    v4si a = {1, 2, 3, 4};
    v4sf badmask = {0.0f, 1.0f, 2.0f, 3.0f};
    v4si r = __builtin_shuffle(a, badmask); // error: mask must be an integer vector
    return r[0];
}
