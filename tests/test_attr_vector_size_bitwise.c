// GNU vector_size bitwise operators (tracker #715): & | ^ ~ on integer-lane
// vectors, across all four integer lane widths. Whole-128-bit opcodes
// (VAND/VOR/VXOR/VNOT), width-agnostic. Float-lane bitwise ops are rejected
// separately (see test_attr_vector_size_bitwise_float_error.c).

typedef int v4si __attribute__((vector_size(16)));
typedef long v2di __attribute__((vector_size(16)));
typedef short v8hi __attribute__((vector_size(16)));
typedef char v16qi __attribute__((vector_size(16)));

int main(void) {
    v4si a = {0xF0, 0x0F, 0xFF, 0x00};
    v4si b = {0x0F, 0xF0, 0x0F, 0xFF};

    v4si c_and = a & b;
    if (c_and[0] != 0x00) return 1;
    if (c_and[1] != 0x00) return 2;
    if (c_and[2] != 0x0F) return 3;
    if (c_and[3] != 0x00) return 4;

    v4si c_or = a | b;
    if (c_or[0] != 0xFF) return 5;
    if (c_or[1] != 0xFF) return 6;
    if (c_or[2] != 0xFF) return 7;
    if (c_or[3] != 0xFF) return 8;

    v4si c_xor = a ^ b;
    if (c_xor[0] != 0xFF) return 9;
    if (c_xor[1] != 0xFF) return 10;
    if (c_xor[2] != 0xF0) return 11;
    if (c_xor[3] != 0xFF) return 12;

    v4si c_not = ~a;
    if (c_not[0] != ~0xF0) return 13;
    if (c_not[3] != ~0x00) return 14;

    // Scalar broadcast operand.
    v4si c_and_scalar = a & 0xFF;
    if (c_and_scalar[0] != 0xF0) return 15;
    if (c_and_scalar[2] != 0xFF) return 16;

    // Other integer lane widths.
    v2di la = {0xF0, 0x0F};
    v2di lb = {0x0F, 0xF0};
    v2di lc = la | lb;
    if (lc[0] != 0xFF) return 17;
    if (lc[1] != 0xFF) return 18;

    v8hi sa = {1, 2, 4, 8, 16, 32, 64, 128};
    v8hi sb = {1, 1, 1, 1, 1, 1, 1, 1};
    v8hi sc = sa & sb;
    if (sc[0] != 1) return 19;
    if (sc[1] != 0) return 20;
    if (sc[3] != 0) return 21;

    v16qi ba = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    v16qi bb = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    v16qi bc = ba ^ bb;
    if (bc[0] != 0) return 22;
    if (bc[1] != 3) return 23;
    if (bc[15] != 17) return 24;

    return 42;
}
