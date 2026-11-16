// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: test_pp_synth_location_defined.c:[0-9]+: #if 1 defined\(X\)
// CCCC_EXPECT_STDERR: extra tokens after #if expression
// CCCC_REJECT_STDERR: :1: [0-9]
//
// A defined() result reports at the defined it replaced.
#if 1 defined(X)
#endif

int main(void) {
    return 42;
}
