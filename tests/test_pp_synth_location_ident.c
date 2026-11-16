// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: test_pp_synth_location_ident.c:[0-9]+: #if 1 OUTER\(y\)
// CCCC_EXPECT_STDERR: extra tokens after #if expression
// CCCC_EXPECT_STDERR: expanded from macro 'INNER'
// CCCC_REJECT_STDERR: :1: [0-9]
//
// An identifier that evaluates to 0 in an #if expression reports at the
// identifier, through the macros it came from, not at the 0 it became.
#define OUTER(m) INNER(m)
#define INNER(m) P_##m

#if 1 OUTER(y)
#endif

int main(void) {
    return 42;
}
