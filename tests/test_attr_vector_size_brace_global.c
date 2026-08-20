// GNU vector_size brace-initializer at file scope (tracker #713 follow-up to
// #72). Covers write_gvar_data's TY_VECTOR branch: a global vector's braced
// initializer is serialized as per-lane constant data at compile time,
// unlike the local case which lowers to per-lane runtime assignment.

typedef float v4sf __attribute__((vector_size(16)));
typedef int   v4si __attribute__((vector_size(16)));

v4sf          g         = {1.0f, 2.0f, 3.0f, 4.0f};
static v4si   gs        = {10, 20, -3, 5};
static v4sf   g_partial = {7.0f}; // remaining lanes zero-initialized

int main(void) {
    if (g[0] != 1.0f)
        return 1;
    if (g[1] != 2.0f)
        return 2;
    if (g[2] != 3.0f)
        return 3;
    if (g[3] != 4.0f)
        return 4;

    if (gs[0] != 10)
        return 5;
    if (gs[1] != 20)
        return 6;
    if (gs[2] != -3)
        return 7;
    if (gs[3] != 5)
        return 8;

    if (g_partial[0] != 7.0f)
        return 9;
    if (g_partial[1] != 0.0f)
        return 10;
    if (g_partial[2] != 0.0f)
        return 11;
    if (g_partial[3] != 0.0f)
        return 12;

    return 42;
}
