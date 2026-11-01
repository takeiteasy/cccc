// CCCC_FLAGS: --testing -B
// CCCC_NATIVE_SKIP: -B is enforced by the VM only

// The same cases under the safety flags, where codegen keeps the unfused
// shift and mask sequences.
#include "test_bitfield_extract.c"
