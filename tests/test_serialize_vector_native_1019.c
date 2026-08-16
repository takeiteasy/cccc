// Ticket #1019: -c=native's C re-emission of GNU vector_size (tracker #715)
// expressions must be portable C, not a literal replay of the typed AST.
// Two independent gaps, both in src/serialize.c:
//
//   1. `vector op scalar` inserts an implicit scalar-broadcast ND_CAST
//      (usual_arith_conv -> get_common_type -> new_cast, src/type.c) as its
//      internal marker for "broadcast this scalar across the vector's
//      lanes" -- it is not source-level C. serialize.c's ND_CAST arm used
//      to print it as a real explicit cast (`a / (v4si)5`), which GCC and
//      clang both reject ("invalid conversion between vector type and
//      integer type of different size") -- they only accept the broadcast
//      performed implicitly inside the operator itself.
//   2. GNU per-lane `?:` (a vector-typed *condition*, typically a
//      comparison mask, where each lane independently selects its then/els
//      element) used to re-emit verbatim as `cond ? a : b`. GCC accepts
//      that directly (a GCC-only extension); clang rejects it ("used type
//      '...' where arithmetic or pointer type is required"). Fixed by
//      lowering to portable mask arithmetic instead of relying on the
//      extension.
//
// A standard C ternary with a *scalar* condition and vector arms (not the
// GNU extension above) must keep re-emitting as a plain `?:` -- verified
// against real clang, this is not the class of bug #1019 covers -- so this
// file asserts that path too, as a negative control.

typedef int v4si __attribute__((vector_size(16)));
typedef float v4sf __attribute__((vector_size(16)));

int main(void) {
    // (1) scalar-broadcast splat, integer and float lanes.
    v4si vi = {2, 4, 6, 8};
    v4si vi2 = vi / 2;
    if (vi2[0] != 1 || vi2[1] != 2 || vi2[2] != 3 || vi2[3] != 4) return 1;

    v4sf vf = {1.0f, 2.0f, 3.0f, 4.0f};
    v4sf vf2 = vf * 2.0f;
    if (vf2[0] != 2.0f || vf2[3] != 8.0f) return 2;

    // (2) GNU per-lane select, comparison-mask condition.
    v4si a = {1, 2, 3, 4};
    v4si b = {10, 20, 30, 40};
    v4si cond = (a < (v4si){3, 3, 3, 3}); // -1, -1, 0, 0
    v4si sel = cond ? a : b;
    if (sel[0] != 1 || sel[1] != 2 || sel[2] != 30 || sel[3] != 40) return 3;

    // Nonzero-but-not-all-ones condition lane still selects the then-arm --
    // GCC's rule is "nonzero", not "all bits set".
    v4si weird_cond = {1, 0, 5, 0};
    v4si sel2 = weird_cond ? a : b;
    if (sel2[0] != 1 || sel2[1] != 20 || sel2[2] != 3 || sel2[3] != 40)
        return 4;

    // Float arms selected by an int-lane mask.
    v4sf fa = {1.5f, 2.5f, 3.5f, 4.5f};
    v4sf fb = {10.5f, 20.5f, 30.5f, 40.5f};
    v4si fcond = {-1, 0, -1, 0};
    v4sf fsel = fcond ? fa : fb;
    if (fsel[0] != 1.5f || fsel[1] != 20.5f || fsel[2] != 3.5f ||
        fsel[3] != 40.5f)
        return 5;

    // Negative control: ordinary C ternary, scalar condition, vector arms --
    // must NOT go through the per-lane mask lowering.
    int flag = 1;
    v4si whole = flag ? a : b;
    if (whole[0] != 1 || whole[3] != 4) return 6;
    flag = 0;
    whole = flag ? a : b;
    if (whole[0] != 10 || whole[3] != 40) return 7;

    return 42;
}
