// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: test_pp_synth_location_line.c:[0-9]+: #if 1 __LINE__
// CCCC_EXPECT_STDERR: extra tokens after #if expression
// CCCC_REJECT_STDERR: :1: [0-9]
//
// A __LINE__ number reports at the __LINE__ it replaced.
#if 1 __LINE__
#endif

int main(void) {
    return 42;
}
