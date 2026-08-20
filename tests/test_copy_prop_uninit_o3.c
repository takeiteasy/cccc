// CCCC_FLAGS: --uninitialized-detection --optimize=3
// CCCC_MATRIX_SKIP: depends on --optimize=3 (copy-prop specifically)
// Ticket #759: CHKI and MARKI were missing from copy-prop's
// op_operand_word_is_immediate() classifier. Both carry the low 32 bits of
// an i64 bp-relative offset in word 0 -- for a small positive offset (a
// stack-passed parameter, the 9th argument onward) that byte aliases a real
// register number, and sub-pass B's generic decode would misread it as a
// destination and NOP a still-live MOV3 crossing the check/mark site.
static int many_params(int a, int b, int c, int d, int e, int f, int g, int h,
                       int i, int j, int k) {
    int live = a + b + c + d + e + f + g + h;
    live += i; // 9th param: stack-passed, CHKI/MARKI fire at a small +offset
    live += j;
    live += k;
    return live;
}

static int repeated_checks(int seed) {
    int a = seed + 1;
    int b = seed + 2;
    int c = seed + 3;
    int d = seed + 4;
    int e = seed + 5;
    return a + b + c + d + e;
}

int main(void) {
    if (many_params(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11) != 66)
        return 1;
    if (repeated_checks(0) != 15)
        return 2;
    return 42;
}
