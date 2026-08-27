// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks
// The VM has no bytecode optimiser any more, so variable-index array access
// no longer fuses into an LDR_INDEX_* opcode -- every load takes emit_load's
// CHKT3 path. This verifies a --type-checks build catches type confusion on
// a variable-index load, the shape #654's fusion bypass used to slip through.
#include <stdlib.h>

int main(void) {
    int *ip   = malloc(4 * sizeof(int));
    ip[0]     = 5;
    float *fp = (float *)ip;
    fp[0]     = 1.0f; // re-stamps the allocation's effective type to float
    int idx   = 0;  // non-constant index forces the variable-index fusion path
    return ip[idx]; // load as int: mismatches the stamped float type
}
