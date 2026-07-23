// GNU vector_size brace-initializer syntax (tracker #713 follow-up to #72):
// `v4sf a = {1.0f, 2.0f, 3.0f, 4.0f};` now lowers to per-lane assignment
// (see init_desg_expr's vector lane lvalue in parse.c). Covers: full
// float/int brace-init, a partial initializer (remaining lanes zeroed, like
// any other partial aggregate init), and nesting inside a struct and an
// array of vectors.

typedef float v4sf __attribute__((vector_size(16)));
typedef int v4si __attribute__((vector_size(16)));

struct WithVec {
    v4sf v;
};

int main(void) {
    // Full float brace-init.
    v4sf a = {1.0f, 2.0f, 3.0f, 4.0f};
    if (a[0] != 1.0f) return 1;
    if (a[1] != 2.0f) return 2;
    if (a[2] != 3.0f) return 3;
    if (a[3] != 4.0f) return 4;

    // Full int brace-init.
    v4si vi = {10, -20, 30, -40};
    if (vi[0] != 10) return 5;
    if (vi[1] != -20) return 6;
    if (vi[2] != 30) return 7;
    if (vi[3] != -40) return 8;

    // Partial brace-init: remaining lanes zero-initialized.
    v4sf p = {40.0f};
    if (p[0] != 40.0f) return 9;
    if (p[1] != 0.0f) return 10;
    if (p[2] != 0.0f) return 11;
    if (p[3] != 0.0f) return 12;

    // Nested inside a struct.
    struct WithVec s = {{1.0f, 2.0f, 3.0f, 4.0f}};
    if (s.v[0] != 1.0f) return 13;
    if (s.v[3] != 4.0f) return 14;
    s.v[1] = 100.0f; // lane lvalue still works after brace-init
    if (s.v[1] != 100.0f) return 15;

    // Array of vectors.
    v4sf arr[2] = {{1.0f, 2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f, 8.0f}};
    if (arr[0][0] != 1.0f) return 16;
    if (arr[1][3] != 8.0f) return 17;

    return 42;
}
