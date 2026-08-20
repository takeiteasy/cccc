// CCCC_FLAGS: --optimize=4
// CCCC_MATRIX_SKIP: --optimize=4 already forces the highest optimization
// level directly; running this file again through the full opt-level matrix
// would be redundant.
//
// GNU vector comparison + select (tracker #715) under --optimize=4:
// regression guard for the optimizer's copy-prop/DCE passes. VSEL reads its
// destination (the else-arm is pre-loaded into rd before VSEL overwrites
// only the true lanes) -- if op_has_vector_operand() ever failed to list a
// new opcode, copy-prop could substitute a stale register for one a vector
// op just redefined, or (per the #713-class hazard) corrupt an operand
// straddling the vreg/real-register namespace split. This mirrors
// test_attr_vector_size_fusion.c's role for the arithmetic opcodes.

typedef int v4si __attribute__((vector_size(16)));

static v4si compute(v4si a, v4si b, v4si limit) {
    v4si cond = (a < limit);
    v4si sel  = cond ? a : b;
    return sel;
}

int main(void) {
    v4si a     = {1, 2, 3, 4};
    v4si b     = {100, 200, 300, 400};
    v4si limit = {3, 3, 3, 3};

    v4si r     = compute(a, b, limit);
    // else-arm (b) must survive in the untouched lanes -- if the pre-store
    // into rd were dead-code-eliminated, lanes 2/3 would read garbage/0
    // instead of b's values.
    if (r[0] != 1)
        return 1;
    if (r[1] != 2)
        return 2;
    if (r[2] != 300)
        return 3;
    if (r[3] != 400)
        return 4;

    // Repeat in a loop so copy-prop/DCE have a control-flow join point to
    // reason (or mis-reason) about.
    int total = 0;
    for (int i = 0; i < 4; i++) {
        v4si eq      = (a == (v4si){1, 1, 1, 1});
        v4si picked  = eq ? a : limit;
        total       += picked[0] + picked[1] + picked[2] + picked[3];
    }
    if (total != 4 * (1 + 3 + 3 + 3))
        return 5;

    return 42;
}
