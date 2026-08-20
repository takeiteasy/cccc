// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: exit=42
//
// #957: the reverse of test_cross_tu_global_offset.c's file order. That
// test alone could pass while this direction regressed -- the original bug
// was order-dependent (a declaring-first invocation read a neighbouring
// global's value; a declaring-second invocation read a constant unrelated
// to either global's value), so both orders need their own test.
// CCCC_FLAGS can only prepend a fixture ahead of the test file (see
// runner.py), so getting the test-code-first order requires invoking cccc
// directly via CaptureCommand, same technique as
// tests/test_build_bytecode_link_ffi_shadow.c.

#include <stdio.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *result =
        CaptureCommand(ctx, "sh -c '\"./cccc\" "
                            "\"tests/fixtures/global_canon_957_main.c\" "
                            "\"tests/fixtures/global_canon_957_defs.c\"; "
                            "echo \"exit=$?\"'");
    if (result)
        puts(result);
    return 0;
}
