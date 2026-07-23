// GNU vector_size comparison operators (tracker #715): == != < <= > >=
// producing a per-lane all-ones(-1)/all-zero mask in a same-width SIGNED
// integer vector (GCC semantics, verified against real gcc/clang). Covers
// float, signed-int, and unsigned-int lanes -- unsigned ordered comparison
// distinguishes from signed via the VCLTU/VCLEU opcode family.

typedef float v4sf __attribute__((vector_size(16)));
typedef double v2df __attribute__((vector_size(16)));
typedef long v2di __attribute__((vector_size(16)));
typedef int v4si __attribute__((vector_size(16)));
typedef unsigned int v4su __attribute__((vector_size(16)));
typedef short v8hi __attribute__((vector_size(16)));
typedef char v16qi __attribute__((vector_size(16)));

int main(void) {
    // Float lanes.
    v4sf a = {1.0f, 2.0f, 3.0f, 4.0f};
    v4sf b = {4.0f, 2.0f, 2.0f, 4.0f};
    v4si eq = (a == b);
    if (eq[0] != 0 || eq[1] != -1 || eq[2] != 0 || eq[3] != -1) return 1;
    v4si ne = (a != b);
    if (ne[0] != -1 || ne[1] != 0 || ne[2] != -1 || ne[3] != 0) return 2;
    v4si lt = (a < b);
    if (lt[0] != -1 || lt[1] != 0 || lt[2] != 0 || lt[3] != 0) return 3;
    v4si le = (a <= b);
    if (le[0] != -1 || le[1] != -1 || le[2] != 0 || le[3] != -1) return 4;
    v4si gt = (a > b);
    if (gt[0] != 0 || gt[1] != 0 || gt[2] != -1 || gt[3] != 0) return 5;
    v4si ge = (a >= b);
    if (ge[0] != 0 || ge[1] != -1 || ge[2] != -1 || ge[3] != -1) return 6;

    // Double lanes.
    v2df da = {1.0, 5.0};
    v2df db = {2.0, 5.0};
    v2di deq = (da == db);
    if (deq[0] != 0 || deq[1] != -1) return 7;

    // Signed int lanes.
    v4si sa = {-1, 2, 3, 4};
    v4si sb = {1, 2, 3, 4};
    v4si slt = (sa < sb); // -1 < 1 signed: true
    if (slt[0] != -1) return 8;

    // Unsigned int lanes: same bit patterns, opposite ordering.
    v4su ua = {1u, 2u, 3u, 4u};
    v4su ub = {0xFFFFFFFFu, 2u, 1u, 5u};
    v4su ult = (ua < ub); // 1 < 0xFFFFFFFF: true; 2<2: false; 3<1: false; 4<5: true
    if (ult[0] != -1) return 9;
    if (ult[1] != 0) return 10;
    if (ult[2] != 0) return 11;
    if (ult[3] != -1) return 12;
    v4su ule = (ua <= ub);
    if (ule[1] != -1) return 13; // 2<=2
    v4su uge = (ua >= ub); // parsed as swapped ub <= ua
    if (uge[1] != -1) return 14; // 2<=2 -> 2>=2 true
    if (uge[0] != 0) return 15;  // 1 >= 0xFFFFFFFF: false

    // Narrow lane widths (i16/i8), to exercise the width-specific
    // VCEQ/VCLT/etc. opcode families beyond i32/i64/float.
    v8hi na = {1, -1, 3, 4, 5, 6, 7, 8};
    v8hi nb = {1, 2, 2, 4, 5, 6, 7, 9};
    v8hi neq = (na == nb);
    if (neq[0] != -1) return 16;
    if (neq[1] != 0) return 17;
    v8hi nlt = (na < nb); // -1 < 2 signed: true
    if (nlt[1] != -1) return 18;

    v16qi ba = {1, -1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    v16qi bb = {1, 2, 2, 4, 5, 6, 7, 9, 9, 10, 11, 12, 13, 14, 15, 17};
    v16qi beq = (ba == bb);
    if (beq[0] != -1) return 19;
    if (beq[1] != 0) return 20;
    v16qi blt = (ba < bb);
    if (blt[1] != -1) return 21; // -1 < 2 signed: true

    return 42;
}
