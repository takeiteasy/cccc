// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: redefinition of 'g'
//
// #957: two full definitions of the same global (not a tentative
// definition followed by a real one -- see
// tests/suites/test_suite_global_canonicalization.c for that accepted
// case) must be a redefinition error, matching the cross-TU case already
// enforced by cc_link_progs (src/linker.c). Before #957's canonicalization,
// each declaration got its own Obj and this silently accepted the second
// initializer, running with g == 2.
int g = 1;
int g = 2;

int main(void) {
    return g;
}
