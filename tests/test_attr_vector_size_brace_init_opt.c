// CCCC_FLAGS: --optimize=4
// Regression coverage: run the vector brace-initializer / compound-literal
// lowering (tracker #713) through the bytecode optimizer. Vector opcodes
// have previously tripped the copy-propagation/DCE passes when they mixed
// the vreg[] namespace with real freg/greg operands (see
// test_attr_vector_size_fusion.c and op_has_vector_operand() in
// optimize.c); this exercises brace-init's per-lane comma-tree lowering and
// the ND_MEMZERO+ND_COMMA pre-zero path under -O4 specifically.

typedef float v4sf __attribute__((vector_size(16)));
typedef int v4si __attribute__((vector_size(16)));

int main(void) {
    v4sf a = {1.0f, 2.0f, 3.0f, 4.0f};
    if (a[0] != 1.0f) return 1;
    if (a[3] != 4.0f) return 2;

    v4sf partial = {40.0f}; // remaining lanes zero-initialized
    if (partial[0] != 40.0f) return 3;
    if (partial[1] != 0.0f) return 4;

    v4si vi = {1, 2, 3, 4};
    if (vi[0] + vi[1] + vi[2] + vi[3] != 10) return 5;

    v4sf lit = (v4sf){5.0f, 5.0f, 5.0f, 5.0f}; // compound literal
    if (lit[2] != 5.0f) return 6;

    return 42;
}
