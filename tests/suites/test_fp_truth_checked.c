// CCCC_FLAGS: --testing -B
// CCCC_NATIVE_SKIP: the tests come from an #include, which -c=native does not give the Assert macros; test_fp_truth.c covers native

// The float truth tests again under bounds checks, which turn the fused and
// promoted code paths off.

#include "test_fp_truth.c"
