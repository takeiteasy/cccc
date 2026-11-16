// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: test_pp_synth_location_paste.c:[0-9]+: +int x = C\(x\);
// CCCC_EXPECT_STDERR: pasting forms '-x', an invalid token
// CCCC_EXPECT_STDERR: expanded from macro 'C'
//
// An invalid paste inside a macro body reports at the macro use.
#define C(a) - ## a

int main(void) {
    int x = C(x);
    return 42;
}
