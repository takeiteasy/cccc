// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks --optimize=3
// Ticket #654: emit_indexed_load_if_possible (variable-index array access,
// src/codegen.c) fuses the address computation and load into a single
// LDR_INDEX_* opcode at opt_level >= 2, bypassing emit_load's CHKT3
// emission. It was only disabled when CCCC_POINTER_CHECKS |
// CCCC_INVALID_ARITH | CCCC_PROVENANCE_TRACK was set; a standalone
// --type-checks -O3 build must still catch type confusion on this path.
#include <stdlib.h>

int main(void) {
    int *ip = malloc(4 * sizeof(int));
    ip[0] = 5;
    float *fp = (float *)ip;
    fp[0] = 1.0f; // re-stamps the allocation's effective type to float
    int idx = 0;  // non-constant index forces the variable-index fusion path
    return ip[idx]; // load as int: mismatches the stamped float type
}
