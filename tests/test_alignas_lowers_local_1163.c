// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: requested alignment is less than minimum alignment
//
// #1163: same constraint violation as test_alignas_lowers_member_1163.c
// (C17 6.7.5p4), but on a block-scope local -- a third, separate call site
// (parse_init.c) resolves the declarator's alignment, so it needs its own
// coverage too.

int main(void) {
    _Alignas(1) long local_alignas_lowers_1163;
    (void)local_alignas_lowers_1163;
    return 0;
}
