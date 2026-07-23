// GNU vector ?: select (tracker #715, GCC extension): per-lane
// nonzero-truthiness select, driven by a comparison-mask condition and by a
// hand-built mask with a nonzero-but-not-all-ones lane (testing the
// "nonzero", not "== -1", truthiness rule).

typedef int v4si __attribute__((vector_size(16)));
typedef float v4sf __attribute__((vector_size(16)));

int main(void) {
    v4si a = {1, 2, 3, 4};
    v4si b = {10, 20, 30, 40};

    // Comparison-mask condition.
    v4si cond = (a < (v4si){3, 3, 3, 3}); // -1,-1,0,0
    v4si sel = cond ? a : b;
    if (sel[0] != 1) return 1;
    if (sel[1] != 2) return 2;
    if (sel[2] != 30) return 3;
    if (sel[3] != 40) return 4;

    // Nonzero-but-not-all-ones condition lane (e.g. 1, not -1) still selects
    // the then-arm -- GCC's rule is "nonzero", not "all bits set".
    v4si weird_cond = {1, 0, 5, 0};
    v4si sel2 = weird_cond ? a : b;
    if (sel2[0] != 1) return 5;
    if (sel2[1] != 20) return 6;
    if (sel2[2] != 3) return 7;
    if (sel2[3] != 40) return 8;

    // Float lanes.
    v4sf fa = {1.5f, 2.5f, 3.5f, 4.5f};
    v4sf fb = {10.5f, 20.5f, 30.5f, 40.5f};
    v4si fcond = {-1, 0, -1, 0};
    v4sf fsel = fcond ? fa : fb;
    if (fsel[0] != 1.5f) return 9;
    if (fsel[1] != 20.5f) return 10;
    if (fsel[2] != 3.5f) return 11;
    if (fsel[3] != 40.5f) return 12;

    // Ordinary C ternary with vector arms and a SCALAR condition (standard
    // C, not the GNU per-lane extension above): the whole vector value is
    // selected by a runtime branch, verified against real clang.
    int flag_true = 1, flag_false = 0;
    v4si whole_a = flag_true ? a : b;
    if (whole_a[0] != 1 || whole_a[3] != 4) return 13;
    v4si whole_b = flag_false ? a : b;
    if (whole_b[0] != 10 || whole_b[3] != 40) return 14;

    return 42;
}
