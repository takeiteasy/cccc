// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: test_pp_synth_location_if_eol.c:[0-9]+: #if 1 \+
// CCCC_EXPECT_STDERR: expected an expression, found end of input
//
// An #if expression that stops early reports on the #if line.
#if 1 +
#endif

int main(void) {
    return 42;
}
