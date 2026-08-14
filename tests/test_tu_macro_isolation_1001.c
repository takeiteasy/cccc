// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: tests/fixtures/tu_isolation_1001_defs.c
// CCCC_EXPECT_STDERR: undefined variable 'TU_ISOLATION_1001_MACRO'
//
// #1001: preprocessor macro definitions used to be shared across every
// input file one cccc invocation compiles together, rather than being
// independent per translation unit as the standard requires -- a #define
// in the fixture above (TU1, listed first via CCCC_FLAGS) was silently
// visible here (TU2) even though this file never #includes anything that
// would define TU_ISOLATION_1001_MACRO. Fixed by
// cc_reset_preprocessor_state_for_next_tu() (preprocess.c), called from
// main.c's preprocess loop between TUs, which restores the macro table to
// the -D/-U-only baseline before preprocessing the next file.
int tu_isolation_1001_use_a(void);

int main(void) {
    return tu_isolation_1001_use_a() + TU_ISOLATION_1001_MACRO;
}
