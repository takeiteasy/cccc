// CCCC_FLAGS: -Wcpp
// CCCC_EXPECT_STDERR: unknown pragma ignored
// CCCC_REJECT_STDERR: unknown pragma ignored[\s\S]*unknown pragma ignored

// Regression test for #686: a warning raised during preprocessing was
// printed once at the "after preprocessing" checkpoint and then reprinted
// at the "after macro expansion" checkpoint, because cc_print_all_errors
// never truncates vm->errors. Must appear exactly once.
#pragma made_up_unknown_pragma

int main(void) { return 42; }
