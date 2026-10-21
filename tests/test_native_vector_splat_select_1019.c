// -c=native, GNU vector_size expressions (tracker) serialize to portable C
// instead of a literal AST replay. Two gaps: (1) `vector op scalar`'s implicit
// scalar-broadcast ND_CAST (usual_arith_conv's internal marker, type.c) used to
// print as a real explicit cast (`a / (v4si)5`), which GCC/clang reject
// ('invalid conversion between vector type and integer type of different size')
// -- they only accept the broadcast performed implicitly inside the operator.
// (2) GNU per-lane `?:` (a vector-typed condition) used to re-emit verbatim as
// `cond ? a : b`, a GCC-only extension clang rejects ('used type ... where
// arithmetic or pointer type is required') -- now lowered to portable mask
// arithmetic. A scalar-condition ternary with vector arms (standard C, not this
// extension) still emits as a plain `?:`. Asserts VM 42 -> native 42

typedef int   v4si __attribute__((vector_size(16)));
typedef float v4sf __attribute__((vector_size(16)));

int main(void) {
    v4si vi  = {2, 4, 6, 8};
    v4si vi2 = vi / 2;
    if (vi2[0] != 1 || vi2[3] != 4)
        return 1;

    v4sf vf  = {1.0f, 2.0f, 3.0f, 4.0f};
    v4sf vf2 = vf * 2.0f;
    if (vf2[0] != 2.0f || vf2[3] != 8.0f)
        return 2;

    v4si a    = {1, 2, 3, 4};
    v4si b    = {10, 20, 30, 40};
    v4si cond = (a < (v4si){3, 3, 3, 3});
    v4si sel  = cond ? a : b;
    if (sel[0] != 1 || sel[2] != 30)
        return 3;

    v4si weird_cond = {1, 0, 5, 0};
    v4si sel2       = weird_cond ? a : b;
    if (sel2[1] != 20 || sel2[2] != 3)
        return 4;

    int  flag  = 1;
    v4si whole = flag ? a : b;
    if (whole[0] != 1)
        return 5;

    return 42;
}
