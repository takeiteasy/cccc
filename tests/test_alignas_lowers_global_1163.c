// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: requested alignment is less than minimum alignment
//
// #1163: same constraint violation as test_alignas_lowers_member_1163.c
// (C17 6.7.5p4), but on a file-scope global rather than a struct member --
// a separate call site (parse_decl.c) resolves the declarator's alignment,
// so it needs its own coverage.

_Alignas(1) long global_alignas_lowers_1163;

int main(void) {
    return 0;
}
