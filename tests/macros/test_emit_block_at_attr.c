// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDOUT: ^ok 1 - emitted_at_test
// An @attr inside an emit block is rewritten like it is in ordinary code.

#pragma cccc comptime begin
#pragma cccc emit begin
@noinline int emitted_helper(void) { return 42; }

@test void emitted_at_test(void) {
    if (emitted_helper() != 42)
        __builtin_trap();
}
#pragma cccc emit end
#pragma cccc comptime end
