// CCCC_FLAGS: --optimize=4
// CCCC_MATRIX_SKIP: depends specifically on -O4 (copy-prop+dce+fuse together)
// Regression test (tracker #72): the bytecode optimizer's copy-propagation
// pass (opt_copy_prop, -O3+) generically decodes every instruction's operand
// word as rd/rs1/rs2 living in the int register file (regs[]) or the float
// register file (fregs[]). Vector opcodes introduce a third namespace
// (vregs[]) and mix it with real freg/greg operands in ways that decode
// cannot express -- discovered when this exact scalar/vector splat
// interleaving corrupted VSPLAT_F64's source register under -O3/-O4 (the
// optimizer substituted it via the wrong copy map) and separately caused a
// live FMOV3 feeding a VSPLAT to be eliminated as dead. See
// op_has_vector_operand() in optimize.c for the fix. Forced to -O4 via
// CCCC_FLAGS above, since the default test run is unoptimized.
//
// Broadcasting uses `ones * scalar` / `ones + scalar` (a vector operand
// times/plus a scalar) rather than a bare `v4sf v = scalar;`, matching real
// GCC/clang vector_size semantics (a bare scalar cannot initialize a whole
// vector -- see the ND_ASSIGN check in type.c).

typedef float v4sf __attribute__((vector_size(16)));

static float scalar_chain(float x) {
    float y = x * 2.0f;
    float z = y + 1.0f;
    return z;
}

int main(void) {
    v4sf ones;
    ones[0] = 1.0f;
    ones[1] = 1.0f;
    ones[2] = 1.0f;
    ones[3] = 1.0f;

    float s = scalar_chain(3.0f); // 7.0
    v4sf  v = ones * s;           // scalar broadcast via vector*scalar
    if (v[0] != 7.0f)
        return 1;
    if (v[3] != 7.0f)
        return 2;

    // Reuse s in further scalar math after the splat consumed it, forcing
    // register reuse/aliasing across the vector-opcode boundary.
    float t = s + 10.0f; // 17.0
    if (t != 17.0f)
        return 3;

    v4sf w = ones * t; // splat via arithmetic broadcast
    if (w[0] != 17.0f)
        return 4;
    if (w[2] != 17.0f)
        return 5;

    float u = t - s; // 10.0
    if (u != 10.0f)
        return 6;

    v4sf combined = v + w; // 7+17 = 24 in every lane
    if (combined[1] != 24.0f)
        return 7;

    return 42;
}
